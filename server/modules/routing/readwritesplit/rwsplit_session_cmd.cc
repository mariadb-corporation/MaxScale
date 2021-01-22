/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplitsession.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.hh>

using namespace maxscale;

/**
 * Discards the slave connection if its response differs from the master's response
 *
 * @param backend    The slave Backend
 * @param master_ok  Master's reply was OK
 * @param slave_ok   Slave's reply was OK
 * @param sescmd     The executed session command
 */
static void discard_if_response_differs(RWBackend* backend, RWBackend* master,
                                        const mxs::Error& master_err,
                                        const mxs::Error& slave_err,
                                        SSessionCommand sescmd)
{
    if (!master_err != !slave_err && backend->in_use())
    {
        uint8_t cmd = sescmd->get_command();
        auto sql = sescmd->to_string();
        std::string query = sql.empty() ? "<no query>" : sql;

        MXS_WARNING("Slave server '%s': response (%s) differs from master's response (%s) to %s: `%s`. "
                    "Closing slave connection due to inconsistent session state.",
                    backend->name(),
                    slave_err ? slave_err.message().c_str() : "OK",
                    master_err ? master_err.message().c_str() : "OK",
                    STRPACKETTYPE(cmd), query.c_str());
        backend->close(mxs::Backend::CLOSE_FATAL);
        backend->set_close_reason("Invalid response to: " + query);
    }
}

mxs::SSessionCommand RWSplitSession::create_sescmd(GWBUF* buffer)
{
    uint8_t cmd = route_info().command();

    if (mxs_mysql_is_ps_command(cmd))
    {
        if (cmd == MXS_COM_STMT_CLOSE)
        {
            // Remove the command from the PS mapping
            m_qc.ps_erase(buffer);
            m_exec_map.erase(route_info().stmt_id());
        }
    }

    /** The SessionCommand takes ownership of the buffer */
    mxs::SSessionCommand sescmd(new mxs::SessionCommand(buffer, m_sescmd_count++));
    auto type = route_info().type_mask();

    if (qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT)
        || qc_query_is_type(type, QUERY_TYPE_PREPARE_STMT))
    {
        mxb_assert(gwbuf_get_id(buffer) != 0 || qc_query_is_type(type, QUERY_TYPE_PREPARE_NAMED_STMT));
        m_qc.ps_store(buffer, gwbuf_get_id(buffer));
    }
    else if (qc_query_is_type(type, QUERY_TYPE_DEALLOC_PREPARE))
    {
        mxb_assert(!mxs_mysql_is_ps_command(route_info().command()));
        m_qc.ps_erase(buffer);
    }

    return sescmd;
}

void RWSplitSession::continue_large_session_write(GWBUF* querybuf, uint32_t type)
{
    for (auto it = m_raw_backends.begin(); it != m_raw_backends.end(); it++)
    {
        RWBackend* backend = *it;

        if (backend->in_use())
        {
            backend->continue_session_command(gwbuf_clone(querybuf));
        }
    }
}

void RWSplitSession::discard_responses(uint64_t pos)
{
    /** Prune all completed responses before a certain position */
    ResponseMap::iterator it = m_sescmd_responses.lower_bound(pos);

    if (it != m_sescmd_responses.end())
    {
        // Found newer responses that were returned after this position
        m_sescmd_responses.erase(m_sescmd_responses.begin(), it);
    }
    else
    {
        // All responses are older than the requested position
        m_sescmd_responses.clear();
    }
}

void RWSplitSession::discard_old_history(uint64_t lowest_pos)
{
    if (m_sescmd_prune_pos)
    {
        if (m_sescmd_prune_pos < lowest_pos)
        {
            discard_responses(m_sescmd_prune_pos);
        }

        auto it = std::find_if(m_sescmd_list.begin(), m_sescmd_list.end(),
                               [this](const SSessionCommand& s) {
                                   return s->get_position() > m_sescmd_prune_pos;
                               });

        if (it != m_sescmd_list.begin() && it != m_sescmd_list.end())
        {
            MXS_INFO("Pruning from %lu to %lu", m_sescmd_prune_pos, it->get()->get_position());
            m_sescmd_list.erase(m_sescmd_list.begin(), it);
            m_sescmd_prune_pos = 0;
        }
    }
}

bool RWSplitSession::create_one_connection_for_sescmd()
{
    mxb_assert(can_recover_servers());

    // Try to first find a master if we are allowed to connect to one
    if (m_config.lazy_connect || m_config.master_reconnection)
    {
        for (auto backend : m_raw_backends)
        {
            if (backend->can_connect() && backend->is_master())
            {
                if (prepare_target(backend, TARGET_MASTER))
                {
                    if (!m_current_master)
                    {
                        MXS_INFO("Chose '%s' as master due to session write", backend->name());
                        m_current_master = backend;
                    }

                    return true;
                }
            }
        }
    }

    // If no master was found, find a slave
    for (auto backend : m_raw_backends)
    {
        if (backend->can_connect() && backend->is_slave() && prepare_target(backend, TARGET_SLAVE))
        {
            return true;
        }
    }

    // No servers are available
    return false;
}
