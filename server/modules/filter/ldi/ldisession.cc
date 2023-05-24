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
#include <maxscale/threadpool.hh>
#include <maxscale/routingworker.hh>
#include <libmarias3/marias3.h>

#include <utility>

namespace
{
void no_delete(LDISession* ignored)
{
}
}

LDISession::LDISession(MXS_SESSION* pSession, SERVICE* pService, const LDI* pFilter)
    : mxs::FilterSession(pSession, pService)
    , m_config(pFilter->m_config.values())
    , m_self(std::shared_ptr<LDISession>(this, no_delete))
{
}

// static
LDISession* LDISession::create(MXS_SESSION* pSession, SERVICE* pService, const LDI* pFilter)
{
    return new LDISession(pSession, pService, pFilter);
}

bool LDISession::routeQuery(GWBUF&& buffer)
{
    if (m_state == IDLE)
    {
        if (auto sql = parser().get_sql(buffer); !sql.empty())
        {
            const std::string_view PREFIX = "LOAD DATA INFILE '";

            if (auto pos = mxb::sv_strcasestr(sql, PREFIX); pos != std::string_view::npos)
            {
                pos += PREFIX.size();

                if (auto end_pos = sql.find('\'', pos); end_pos != std::string_view::npos)
                {
                    auto url_prefix = sql.substr(pos, 5);

                    if (mxb::sv_case_eq(url_prefix, "S3://") || mxb::sv_case_eq(url_prefix, "gs://"))
                    {
                        auto url = sql.substr(pos + 5, end_pos - pos - 5);
                        std::tie(m_bucket, m_file) = mxb::split(url, "/");

                        const std::string_view NEW_PREFIX = "LOAD DATA LOCAL INFILE 'data.csv' ";
                        buffer = protocol().make_query(mxb::cat(NEW_PREFIX, sql.substr(end_pos + 1)));
                        m_state = PREPARE;
                    }
                }
            }
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
    return static_cast<S3Download*>(userdata)->process(buffer, length) ? length : 0;
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

//
// MariaDBLoader
//

bool MariaDBLoader::process(void* data, size_t len)
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
            const uint8_t data[4]{0, 0, 0, m_sequence++};
            ok = route_end(GWBUF(data, 4));
        });
    }

    return ok;
}
