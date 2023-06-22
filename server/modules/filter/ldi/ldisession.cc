/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "ldi"
#include "ldi.hh"
#include "ldisession.hh"
#include <maxscale/session.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/threadpool.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/service.hh>
#include <maxscale/secrets.hh>
#include <libmarias3/marias3.h>

#include <utility>

namespace
{
const char* CN_S3_KEY = "@maxscale.s3_key";
const char* CN_S3_SECRET = "@maxscale.s3_secret";
const char* CN_S3_REGION = "@maxscale.s3_region";
const char* CN_S3_HOST = "@maxscale.s3_host";
const char* CN_S3_PORT = "@maxscale.s3_port";
const char* CN_S3_PROTOCOL_VERSION = "@maxscale.s3_protocol_version";
const char* CN_IMPORT_USER = "@maxscale.import_user";
const char* CN_IMPORT_PASSWORD = "@maxscale.import_password";

void no_delete(LDISession* ignored)
{
}

std::string escape_single_quotes(std::string_view str)
{
    std::string sql(str);
    size_t pos = sql.find('\'');

    while (pos != std::string::npos)
    {
        sql.replace(pos, 1, "\\'");
        pos = sql.find('\'', pos + 2);
    }

    return sql;
}
}

// static
char* LDISession::set_key(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.key.assign(begin, end);
    return nullptr;
}

// static
char* LDISession::set_secret(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.secret.assign(begin, end);
    return nullptr;
}

// static
char* LDISession::set_region(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.region.assign(begin, end);
    return nullptr;
}

// static
char* LDISession::set_host(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.host.assign(begin, end);
    return nullptr;
}

// static
char* LDISession::set_port(void* self, const char* key, const char* begin, const char* end)
{
    if (int port = atoi(std::string(begin, end).c_str()); port > 0)
    {
        static_cast<LDISession*>(self)->m_config.port = port;
    }

    return nullptr;
}

// static
char* LDISession::set_protocol_version(void* self, const char* key, const char* begin, const char* end)
{
    if (int protocol_version = atoi(std::string(begin, end).c_str());
        protocol_version >= 0 && protocol_version <= 2)
    {
        static_cast<LDISession*>(self)->m_config.protocol_version = protocol_version;
    }

    return nullptr;
}

// static
char* LDISession::set_import_user(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.import_user.assign(begin, end);
    return nullptr;
}

// static
char* LDISession::set_import_password(void* self, const char* key, const char* begin, const char* end)
{
    auto pw = mxs::decrypt_password(std::string(begin, end));
    static_cast<LDISession*>(self)->m_config.import_password = std::move(pw);
    return nullptr;
}

LDISession::LDISession(MXS_SESSION* pSession, SERVICE* pService, LDI* pFilter)
    : mxs::FilterSession(pSession, pService)
    , m_config(pFilter->m_config.values())
    , m_filter(*pFilter)
    , m_self(std::shared_ptr<LDISession>(this, no_delete))
{
    pSession->add_variable(CN_S3_KEY, &LDISession::set_key, this);
    pSession->add_variable(CN_S3_SECRET, &LDISession::set_secret, this);
    pSession->add_variable(CN_S3_REGION, &LDISession::set_region, this);
    pSession->add_variable(CN_S3_HOST, &LDISession::set_host, this);
    pSession->add_variable(CN_S3_PORT, &LDISession::set_port, this);
    pSession->add_variable(CN_S3_PROTOCOL_VERSION, &LDISession::set_protocol_version, this);
    pSession->add_variable(CN_IMPORT_USER, &LDISession::set_import_user, this);
    pSession->add_variable(CN_IMPORT_PASSWORD, &LDISession::set_import_password, this);
}

// static
LDISession* LDISession::create(MXS_SESSION* pSession, SERVICE* pService, LDI* pFilter)
{
    return new LDISession(pSession, pService, pFilter);
}

SERVER* LDISession::get_xpand_node() const
{
    for (const auto& b : m_pSession->service->reachable_servers())
    {
        if (b->info().type() == SERVER::VersionInfo::Type::XPAND)
        {
            return b;
        }
    }

    return nullptr;
}

std::unique_ptr<ExternalCmd> LDISession::create_import_cmd(SERVER* node, LoadDataResult* parsed)
{
    // TODO: The import will fail if the table has a fully-qualified name with the database in it.
    auto mdb = static_cast<MYSQL_session*>(m_pSession->protocol_data());
    std::string cmd = mxb::cat(
        "/usr/bin/env xpand_import",
        " --skip-gui",
        " --host ", node->address(), ":", std::to_string(node->port()),
        " --user ", m_config.import_user,
        " --passwd ", m_config.import_password,
        " --db ", (parsed->db.empty() ? mdb->current_db : parsed->db),
        " --error-file /dev/null",
        " --log-file /dev/null",
        (node->ssl_config().enabled ? " --ssl" : ""),
        " --ldi \"'-' INTO TABLE ", parsed->table, " ", escape_single_quotes(parsed->remaining_sql), "\"");

    MXB_INFO("CMD: %s", cmd.c_str());

    return ExternalCmd::create(cmd, 120, [](auto cmd, auto line){
        MXB_INFO("%s: %s", cmd.c_str(), line.c_str());
    });
}

bool LDISession::routeQuery(GWBUF&& buffer)
{
    if (m_state == IDLE)
    {
        auto res = parse_ldi(parser().get_sql(buffer));

        if (auto parsed = std::get_if<LoadDataResult>(&res))
        {
            m_bucket = parsed->s3.bucket;
            m_file = parsed->s3.filename;
            auto server = get_xpand_node();

            if (server && m_filter.have_xpand_import())
            {
                if (missing_required_params(ServerType::XPAND))
                {
                    return true;
                }

                // We have at least one Xpand node, load the data there
                if (auto cmd = create_import_cmd(server, parsed); cmd->start())
                {
                    m_state = LOAD;
                    mxs::thread_pool().execute(
                        [dl = std::make_shared<CmdLoader>(this, std::move(cmd))](){
                        dl->load_data();
                    }, "ldi");
                    return true;
                }
            }
            else
            {
                if (missing_required_params(ServerType::MARIADB))
                {
                    return true;
                }

                if (server)
                {
                    mxb_assert(!m_filter.have_xpand_import());
                    m_filter.warn_about_missing_xpand_import(m_pService);
                }

                // Normal MariaDB or an unknown server type. Use LOAD DATA LOCAL INFILE to stream the data.
                auto maybe_db = !parsed->db.empty() ? mxb::cat("`", parsed->db, "` ") : ""s;
                auto new_sql = mxb::cat("LOAD DATA LOCAL INFILE 'data.csv' INTO TABLE ",
                                        maybe_db, "`", parsed->table, "` ",
                                        parsed->remaining_sql);
                buffer = protocol().make_query(new_sql);
                m_state = PREPARE;
            }
        }
        else if (auto err = std::get_if<ParseError>(&res))
        {
            if (mxb_log_should_log(LOG_INFO))
            {
                MXB_INFO("Not a LOAD DATA INFILE statement.");

                for (auto line : mxb::strtok(err->message, "\n"))
                {
                    MXB_INFO("%s", line.c_str());
                }
            }
        }
        else
        {
            mxb_assert_message(!true, "Valueless variant, things are really broken.");
            return false;
        }
    }
    else if (m_state == LOAD)
    {
        MXB_ERROR("Cannot route query while data load is in progress.");
        return false;
    }

    return mxs::FilterSession::routeQuery(std::move(buffer));
}

bool LDISession::clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (m_state == PREPARE)
    {
        if (reply.state() == mxs::ReplyState::LOAD_DATA)
        {
            m_state = LOAD;
            mxs::thread_pool().execute([dl = std::make_shared<MariaDBLoader>(this)](){
                dl->load_data();
            }, "ldi");

            return true;
        }
        else
        {
            m_state = IDLE;
        }
    }

    return mxs::FilterSession::clientReply(std::move(buffer), down, reply);
}

bool LDISession::route_data(GWBUF&& buffer)
{
    return mxs::FilterSession::routeQuery(std::move(buffer));
}

bool LDISession::route_end(GWBUF&& buffer)
{
    m_state = IDLE;
    return mxs::FilterSession::routeQuery(std::move(buffer));
}

bool LDISession::send_ok(int64_t rows)
{
    m_state = IDLE;
    mxs::ReplyRoute down;
    mxs::Reply reply;
    return mxs::FilterSession::clientReply(mariadb::create_ok_packet(0, rows), down, reply);
}

bool LDISession::missing_required_params(ServerType type)
{
    auto check_value = [&](std::string_view name, std::string_view value){
        if (value.empty())
        {
            int errnum = 1230;      // ER_NO_DEFAULT
            set_response(protocol().make_error(
                errnum, "42000", mxb::cat("Variable '", name, "' doesn't have a default value")));
        }

        return !value.empty();
    };

    bool ok = check_value(CN_S3_KEY, m_config.key)
        && check_value(CN_S3_SECRET, m_config.secret)
        && check_value(CN_S3_HOST, m_config.host)
        && (type == ServerType::MARIADB
            || (check_value(CN_IMPORT_USER, m_config.import_user)
                && check_value(CN_IMPORT_PASSWORD, m_config.import_password)));

    return !ok;
}

//
// S3Download
//

S3Download::S3Download(LDISession* ldi)
    : m_session(session_get_ref(ldi->m_pSession))
    , m_ldi(ldi->m_self)
    , m_config(ldi->m_config)
    , m_file(ldi->m_file)
    , m_bucket(ldi->m_bucket)
{
}

S3Download::~S3Download()
{
    session()->worker()->call([&](){
        session_put_ref(m_session);
    });
}

void S3Download::load_data()
{
    MXS_SESSION::Scope scope(session());
    ms3_st* ms3 = ms3_init(m_config.key.c_str(),
                           m_config.secret.c_str(),
                           m_config.region.c_str(),
                           m_config.host.c_str());

    if (m_config.no_verify)
    {
        ms3_set_option(ms3, MS3_OPT_DISABLE_SSL_VERIFY, NULL);
    }

    if (m_config.use_http)
    {
        ms3_set_option(ms3, MS3_OPT_USE_HTTP, NULL);
    }

    if (int port = m_config.port)
    {
        ms3_set_option(ms3, MS3_OPT_PORT_NUMBER, (void*)&port);
    }

    if (int protocol_version = m_config.protocol_version)
    {
        ms3_set_option(ms3, MS3_OPT_FORCE_PROTOCOL_VERSION, (void*)&protocol_version);
    }

    size_t buffer_size = 0xfffff0;
    ms3_set_option(ms3, MS3_OPT_BUFFER_CHUNK_SIZE, &buffer_size);
    ms3_set_option(ms3, MS3_OPT_READ_CB, (void*)read_callback);
    ms3_set_option(ms3, MS3_OPT_USER_DATA, this);

    int rc = ms3_get(ms3, m_bucket.c_str(), m_file.c_str(), nullptr, nullptr);
    std::string errmsg;

    if (rc != 0)
    {
        const char* srverr = ms3_server_error(ms3);
        const char* err = ms3_error(rc);
        errmsg = mxb::cat("Error: ", srverr ? srverr : "", srverr && err ? ". " : "", err ? err : "");
    }

    ms3_deinit(ms3);

    if (rc == 0 && !complete())
    {
        errmsg = "Failed to process data";
    }

    if (!errmsg.empty())
    {
        session()->worker()->call([&](){
            session()->kill(errmsg);
        });
    }
}

// static
size_t S3Download::read_callback(void* buffer, size_t size, size_t nitems, void* userdata)
{
    size_t length = size * nitems;

    // Returning something other than the number of bytes that's available for processing will cause
    // libmarias3 (curl behind the scenes) to stop reading data.
    return static_cast<S3Download*>(userdata)->process((const char*)buffer, length) ? length : 0;
}

bool S3Download::route_data(GWBUF&& buffer)
{
    mxb_assert(mxs::RoutingWorker::get_current());
    bool ok = false;

    if (auto ldi = filter_session())
    {
        ok = ldi->route_data(std::move(buffer));
    }

    return ok;
}

bool S3Download::route_end(GWBUF&& buffer)
{

    mxb_assert(mxs::RoutingWorker::get_current());
    bool ok = false;

    if (auto ldi = filter_session())
    {
        ok = ldi->route_end(std::move(buffer));
    }

    return ok;
}

bool S3Download::send_ok(int64_t rows)
{
    mxb_assert(mxs::RoutingWorker::get_current());
    bool ok = false;

    if (auto ldi = filter_session())
    {
        ok = ldi->send_ok(rows);
    }

    return ok;
}

//
// MariaDBLoader
//

bool MariaDBLoader::process(const char* data, size_t len)
{
    bool ok = true;

    if (m_payload.length() + len > 0xfffffe)
    {
        // We've collected as much as we can send in one packet, route it and prepare a new one.
        ok = send_packet();
    }

    auto [ptr, _] = m_payload.prepare_to_write(len);
    memcpy(ptr, data, len);
    m_payload.write_complete(len);

    return ok;
}

bool MariaDBLoader::send_packet()
{
    mariadb::write_header(m_payload.data(), m_payload.length() - 4, m_sequence++);
    auto buffer = std::exchange(m_payload, GWBUF(4));
    bool slow_down = false;
    bool ok = false;

    session()->worker()->call([&](){
        MXS_SESSION::Scope scope(session());

        if (route_data(std::move(buffer)))
        {
            ok = true;
            slow_down = going_too_fast();
        }
    });

    if (slow_down)
    {
        MXB_INFO("Going too fast, waiting for DCBs to drain before continuing");
        auto sleep = 1ms;

        while (slow_down)
        {
            std::this_thread::sleep_for(sleep);
            sleep = std::min(sleep + 100ms, 5000ms);

            session()->worker()->call([&](){
                MXS_SESSION::Scope scope(session());
                slow_down = going_too_fast();
            });
        }
    }

    return ok;
}

bool MariaDBLoader::going_too_fast()
{
    for (const auto& conn : session()->backend_connections())
    {
        if (conn->dcb()->writeq_len() > 0xffffff)
        {
            return true;
        }
    }

    return false;
}

bool MariaDBLoader::complete()
{
    mxb_assert(m_payload.length() > 4);

    // Some data is always left over after the last chunk is read. The data is flushed only if the data would
    // not fit into a single packet.
    bool ok = send_packet();

    if (ok)
    {
        // Write the final empty packet to finalize the LOAD DATA LOCAL INFILE
        session()->worker()->call([&](){
            MXS_SESSION::Scope scope(session());
            const uint8_t data[4]{0, 0, 0, m_sequence++};
            ok = route_end(GWBUF(data, 4));
        });
    }

    return ok;
}

//
// CmdLoader
//

CmdLoader::CmdLoader(LDISession* ldi, std::unique_ptr<ExternalCmd> cmd)
    : S3Download(ldi)
    , m_cmd(std::move(cmd))
{
}

bool CmdLoader::process(const char* ptr, size_t len)
{
    for (size_t i = 0; i < len; i++)
    {
        // TODO: Parse the delimiter. Xpand only allows single-character line terminators so it should be
        // pretty straightforward.
        m_rows += ptr[i] == '\n';
    }

    return m_cmd->write(ptr, len);
}

bool CmdLoader::complete()
{
    bool ok = false;
    m_cmd->close_output();

    if (m_cmd->wait() == 0)
    {
        session()->worker()->call([&](){
            MXS_SESSION::Scope scope(session());
            ok = send_ok(m_rows);
        });
    }

    return ok;
}
