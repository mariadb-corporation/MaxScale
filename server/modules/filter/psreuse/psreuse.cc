/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "psreuse.hh"

namespace
{
mxs::config::Specification s_spec(MXB_MODULE_NAME, mxs::config::Specification::FILTER);
}

PsReuseSession::PsReuseSession(MXS_SESSION* pSession, SERVICE* pService, PsReuse& filter)
    : mxs::FilterSession(pSession, pService)
    , m_filter(filter)
{
}

bool PsReuseSession::routeQuery(GWBUF&& packet)
{
    m_tracker.track_query(packet);

    if (m_tracker.should_ignore())
    {
        return mxs::FilterSession::routeQuery(std::move(packet));
    }

    uint8_t cmd = mariadb::get_command(packet);

    if (cmd == MXS_COM_STMT_CLOSE)
    {
        uint32_t id = mxs_mysql_extract_ps_id(packet);

        if (auto it = m_ids.find(id); it != m_ids.end())
        {
            CacheEntry& e = it->second;
            e.active = false;
        }

        return true;
    }
    else if (cmd == MXS_COM_STMT_PREPARE)
    {
        // The PS ID always has to be captured so that a COM_STMT_EXECUTE with an ID of -1 will work.
        m_prev_id = packet.id();
        mxb_assert(m_prev_id != 0);

        if (!m_tracker.is_multipart())
        {
            std::string sql(get_sql(packet));

            if (auto it = m_ps_cache.find(sql); it != m_ps_cache.end())
            {
                MXB_INFO("Found in cache: %s", sql.c_str());

                if (it->second.active)
                {
                    int errnum = 1461;      // ER_MAX_PREPARED_STMT_COUNT_REACHED ;
                    set_response(mariadb::create_error_packet(
                        0, errnum, "HY000", "Cannot prepare the same statement multiple times"));
                    return true;
                }

                m_filter.hit();
                it->second.active = true;
                m_prev_id = it->second.id;
                set_response(it->second.buffer.shallow_clone());
                return true;
            }
            else
            {
                MXB_INFO("Not found in cache: %s", sql.c_str());
                m_filter.miss();
                m_current_sql = std::move(sql);
            }
        }
    }
    else if (mxs_mysql_is_ps_command(cmd))
    {
        uint32_t id = mxs_mysql_extract_ps_id(packet);

        // If m_prev_id is zero, the connector sent a malformed packet.
        if (id == MARIADB_PS_DIRECT_EXEC_ID && m_prev_id != 0)
        {
            packet.ensure_unique();
            mariadb::set_byte4(packet.data() + MYSQL_PS_ID_OFFSET, m_prev_id);
        }
    }

    return mxs::FilterSession::routeQuery(std::move(packet));
}

bool PsReuseSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    m_tracker.track_reply(reply);

    // If m_current_sql is empty when the command is a COM_STMT_PREPARE, it means that it was split across
    // multiple packets.
    if (reply.command() == MXS_COM_STMT_PREPARE && !m_current_sql.empty())
    {
        auto [it, _] = m_ps_cache.emplace(m_current_sql, CacheEntry {});
        it->second.buffer.append(packet.shallow_clone());

        if (reply.is_complete())
        {
            it->second.id = reply.generated_id();
            m_ids.emplace(it->second.id, std::ref(it->second));
            m_current_sql.clear();
        }
    }

    return mxs::FilterSession::clientReply(std::move(packet), down, reply);
}

PsReuse::PsReuse(const std::string& name)
    : m_config(name, &s_spec)
{
}

json_t* PsReuse::diagnostics() const
{
    json_t* js = json_object();
    json_object_set_new(js, "hits", json_integer(m_hits.load(std::memory_order_relaxed)));
    json_object_set_new(js, "misses", json_integer(m_misses.load(std::memory_order_relaxed)));
    return js;
}

extern "C" MXB_API MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "Prepared statement reuse filter",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::FilterApi<PsReuse>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        &s_spec
    };

    return &info;
}
