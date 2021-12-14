/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/rwbackend.hh>

#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/router.hh>

using Iter = mxs::Buffer::iterator;
using std::chrono::seconds;

namespace maxscale
{

RWBackend::RWBackend(mxs::Endpoint* ref)
    : mxs::Backend(ref)
    , m_response_stat(target(), 9, std::chrono::milliseconds(250))
    , m_last_write(maxbase::Clock::now(maxbase::NowType::EPollTick))
{
}

bool RWBackend::execute_session_command()
{
    const auto& sescmd = next_session_command();
    const char* cmd = STRPACKETTYPE(sescmd->command());
    MXS_INFO("Execute sescmd #%lu on '%s': [%s] %s",
             sescmd->get_position(), name(), cmd, sescmd->to_string().c_str());

    m_last_write = maxbase::Clock::now(maxbase::NowType::EPollTick);
    return mxs::Backend::execute_session_command();
}

bool RWBackend::continue_session_command(GWBUF* buffer)
{
    m_last_write = maxbase::Clock::now(maxbase::NowType::EPollTick);
    return Backend::write(buffer, NO_RESPONSE);
}

void RWBackend::add_ps_handle(uint32_t id, uint32_t handle)
{
    m_ps_handles[id] = handle;
    MXS_INFO("PS response for %s: %u -> %u", name(), id, handle);
}

uint32_t RWBackend::get_ps_handle(uint32_t id) const
{
    BackendHandleMap::const_iterator it = m_ps_handles.find(id);

    if (it != m_ps_handles.end())
    {
        return it->second;
    }

    return 0;
}

bool RWBackend::write(GWBUF* buffer, response_type type)
{
    m_last_write = maxbase::Clock::now(maxbase::NowType::EPollTick);
    uint32_t len = mxs_mysql_get_packet_len(buffer);
    bool was_large_query = m_large_query;
    m_large_query = len == MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN;

    if (was_large_query)
    {
        return mxs::Backend::write(buffer, Backend::NO_RESPONSE);
    }

    uint8_t cmd = mxs_mysql_get_command(buffer);

    if (mxs_mysql_is_ps_command(cmd))
    {
        // We need to completely separate the buffer this backend owns and the one that the caller owns to
        // prevent any modifications from affecting the one that was written through this backend. If the
        // buffer gets placed into the write queue of the DCB, subsequent modifications to the original buffer
        // would be propagated to the one this backend owns.
        GWBUF* tmp = gwbuf_deep_clone(buffer);
        gwbuf_free(buffer);
        buffer = tmp;

        uint32_t id = mxs_mysql_extract_ps_id(buffer);
        auto it = m_ps_handles.find(id);

        if (it != m_ps_handles.end())
        {
            /** Replace the client handle with the real PS handle */
            uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
            mariadb::set_byte4(ptr, it->second);

            if (cmd == MXS_COM_STMT_CLOSE)
            {
                m_ps_handles.erase(it);
            }
        }
    }

    return mxs::Backend::write(buffer, type);
}

void RWBackend::close(close_type type)
{
    mxs::Backend::close(type);
}

void RWBackend::sync_averages()
{
    m_response_stat.sync();
}

mxs::SRWBackends RWBackend::from_endpoints(const Endpoints& endpoints)
{
    SRWBackends backends;
    backends.reserve(endpoints.size());

    for (auto e : endpoints)
    {
        backends.emplace_back(new mxs::RWBackend(e));
    }

    return backends;
}

void RWBackend::select_started()
{
    Backend::select_started();
    m_response_stat.query_started();
}

void RWBackend::select_finished()
{
    Backend::select_finished();

    m_response_stat.query_finished();
}
}
