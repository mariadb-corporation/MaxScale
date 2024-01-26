/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/ps_to_text.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxbase/string.hh>
#include <maxsql/mariadb.hh>

namespace mariadb
{
void PsToText::track_query(const GWBUF& buffer)
{
    switch (get_command(buffer))
    {
    case MXS_COM_STMT_PREPARE:
        // Technically we could parse the COM_STMT_PREPARE here and not have to do anything in track_reply().
        // The only problem is that there's a corner case where a client repeatedly executes prepared
        // statements that end up failing. In this case the PS map would keep growing. This could be solved by
        // optimistically storing the PS and then in track_reply() only removing failed ones but the practical
        // difference in it is not significant enough to warrant it.
        m_queue.push_back(buffer.shallow_clone());
        break;

    case MXS_COM_STMT_CLOSE:
        m_ps.erase(mxs_mysql_extract_ps_id(buffer));
        break;

    case MXS_COM_STMT_RESET:
        // TODO: This should reset any data that was read from a COM_STMT_SEND_LONG_DATA
        break;

    default:
        break;
    }
}

void PsToText::track_reply(const mxs::Reply& reply)
{
    if (reply.is_complete() && reply.command() == MXS_COM_STMT_PREPARE)
    {
        mxb_assert(!m_queue.empty());
        const GWBUF& buffer = m_queue.front();
        mxb_assert(get_command(buffer) == MXS_COM_STMT_PREPARE);

        if (!reply.error())
        {
            // Calculate the parameter offsets that are used by maxsimd::canonical_args_to_sql().
            std::string sql(get_sql(buffer));
            const char* ptr = sql.c_str();
            const char* end = ptr + sql.size();

            std::vector<uint32_t> param_offsets;
            param_offsets.reserve(reply.param_count());

            while ((ptr = mxb::strnchr_esc_mariadb(ptr, end, '?')))
            {
                param_offsets.push_back(std::distance(sql.c_str(), ptr++));
            }

            mxb_assert(reply.param_count() == param_offsets.size());

            if (reply.param_count() == param_offsets.size())
            {
                m_ps.emplace(buffer.id(), Prepare {std::move(sql), std::move(param_offsets)});
            }
            else
            {
                MXB_ERROR("Placeholder count in '%s' was calculated as %lu "
                          "but the server reports it as %hu.", sql.c_str(),
                          param_offsets.size(), reply.param_count());
            }
        }

        m_queue.pop_front();
    }
}

std::string PsToText::to_sql(const GWBUF& buffer) const
{
    std::string rval;

    switch (get_command(buffer))
    {
    case MXS_COM_QUERY:
        rval.assign(get_sql(buffer));
        break;

    case MXS_COM_STMT_EXECUTE:
        if (auto it = m_ps.find(mxs_mysql_extract_ps_id(buffer)); it != m_ps.end())
        {
            rval = "TODO: Implement this";
        }
        break;

    default:
        break;
    }

    return rval;
}

std::pair<std::string_view, maxsimd::CanonicalArgs> PsToText::get_args(const GWBUF& buffer) const
{
    // TODO: Implement this
    return {};
}

std::string PsToText::get_prepare(const GWBUF& buffer) const
{
    std::string rval;

    if (get_command(buffer) == MXS_COM_STMT_EXECUTE)
    {
        if (auto it = m_ps.find(mxs_mysql_extract_ps_id(buffer)); it != m_ps.end())
        {
            rval = it->second.sql;
        }
    }

    return rval;
}
}
