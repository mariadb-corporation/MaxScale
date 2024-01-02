/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-11-30
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
#include <maxbase/pretty_print.hh>
#include <libmarias3/marias3.h>

#include <utility>

namespace
{
const char* CN_S3_KEY = "@maxscale.ldi.s3_key";
const char* CN_S3_SECRET = "@maxscale.ldi.s3_secret";
const char* CN_S3_REGION = "@maxscale.ldi.s3_region";
const char* CN_S3_HOST = "@maxscale.ldi.s3_host";
const char* CN_S3_PORT = "@maxscale.ldi.s3_port";
const char* CN_S3_PROTOCOL_VERSION = "@maxscale.ldi.s3_protocol_version";

void no_delete(LDISession* ignored)
{
}

std::string_view unquote(const char* begin, const char* end)
{
    std::string_view str(begin, std::distance(begin, end));

    if ((str.front() == '"' || str.front() == '\'') && str.front() == str.back())
    {
        str = str.substr(1, str.length() - 2);
    }

    return str;
}
}

// static
char* LDISession::set_key(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.key.assign(unquote(begin, end));
    return nullptr;
}

// static
char* LDISession::set_secret(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.secret.assign(unquote(begin, end));
    return nullptr;
}

// static
char* LDISession::set_region(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.region.assign(unquote(begin, end));
    return nullptr;
}

// static
char* LDISession::set_host(void* self, const char* key, const char* begin, const char* end)
{
    static_cast<LDISession*>(self)->m_config.host.assign(unquote(begin, end));
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
}

// static
LDISession* LDISession::create(MXS_SESSION* pSession, SERVICE* pService, LDI* pFilter)
{
    return new LDISession(pSession, pService, pFilter);
}

bool LDISession::routeQuery(GWBUF&& buffer)
{
    if (m_state == IDLE)
    {
        auto res = parse_ldi(parser().get_sql(buffer));

        if (auto parsed = std::get_if<LoadDataInfile>(&res))
        {
            // This is a LOAD DATA INFILE command. See if the filename is a S3 URL.
            auto url_res = parse_s3_url(parsed->filename);

            if (auto parsed_url = std::get_if<S3URL>(&url_res))
            {
                m_bucket = parsed_url->bucket;
                m_file = parsed_url->filename;

                if (missing_required_params())
                {
                    return true;
                }

                // Normal MariaDB or an unknown server type. Use LOAD DATA LOCAL INFILE to
                MXB_INFO("Starting S3 data import from '%s/%s' into table '%s' "
                         "using LOAD DATA LOCAL INFILE.",
                         m_bucket.c_str(), m_file.c_str(), parsed->table.c_str());

                // stream the data.
                auto maybe_db = !parsed->db.empty() ? mxb::cat("`", parsed->db, "` ") : ""s;
                auto new_sql = mxb::cat("LOAD DATA LOCAL INFILE 'data.csv' INTO TABLE ",
                                        maybe_db, "`", parsed->table, "` ",
                                        parsed->remaining_sql);
                buffer = protocol().make_query(new_sql);
                m_state = PREPARE;
            }
            else if (auto err = std::get_if<ParseError>(&url_res))
            {
                if (mxb_log_should_log(LOG_INFO))
                {
                    MXB_INFO("Not a S3 URL.");

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

bool LDISession::missing_required_params()
{
    std::vector<std::string> errors;

    auto check_value = [&](std::string_view name, std::string_view value){
        if (value.empty())
        {
            errors.push_back(mxb::cat("Variable '", name, "' doesn't have a default value."));
        }
    };

    check_value(CN_S3_KEY, m_config.key);
    check_value(CN_S3_SECRET, m_config.secret);
    check_value(CN_S3_HOST, m_config.host);

    if (!errors.empty())
    {
        int errnum = 1230;      // ER_NO_DEFAULT
        set_response(protocol().make_error(errnum, "42000", mxb::join(errors, " ")));
    }

    return !errors.empty();
}

//
// UploadTracker
//

UploadTracker::UploadTracker()
    : m_start(mxb::Clock::now())
{
}

void UploadTracker::bytes_uploaded(size_t bytes)
{
    m_bytes += bytes;
    m_chunk += bytes;

    if (mxb_log_should_log(LOG_INFO))
    {
        auto now = mxb::Clock::now();

        if (now - m_start > 5s)
        {
            // Cap the upload speed at the actual number bytes per second if it would otherwise be faster.
            size_t clamped_bytes = m_chunk / std::max(1.0, mxb::to_secs(now - m_start));

            MXB_INFO("%s processed (%s/s).", mxb::pretty_size(m_bytes).c_str(),
                     mxb::pretty_size(clamped_bytes).c_str());

            m_start = now;
            m_chunk = 0;
        }
    }
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
    return static_cast<S3Download*>(userdata)->handle_read(buffer, length);
}

size_t S3Download::handle_read(void* buffer, size_t length)
{
    m_tracker.bytes_uploaded(length);

    // Returning something other than the number of bytes that's available for processing will cause
    // libmarias3 (curl behind the scenes) to stop reading data.
    return process((const char*)buffer, length) ? length : 0;
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
