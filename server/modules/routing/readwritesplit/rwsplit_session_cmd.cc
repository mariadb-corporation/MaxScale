/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
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

void RWSplitSession::process_sescmd_response(RWBackend* backend, GWBUF** ppPacket, const mxs::Reply& reply)
{
    mxb_assert(backend->has_session_commands());
    bool discard = true;
    mxs::SSessionCommand sescmd = backend->next_session_command();
    uint8_t command = sescmd->get_command();
    uint64_t id = sescmd->get_position();


    if (m_recv_sescmd < m_sent_sescmd && id == m_recv_sescmd + 1)
    {
        mxb_assert_message(m_sescmd_replier, "New session commands must have a pre-assigned replier");

        if (m_sescmd_replier == backend)
        {
            discard = false;

            if (m_config.reuse_ps && command == MXS_COM_STMT_PREPARE)
            {
                m_ps_cache[sescmd->to_string()].append(gwbuf_clone(*ppPacket));
            }

            if (reply.is_complete())
            {
                /** First reply to this session command, route it to the client */
                ++m_recv_sescmd;
                --m_expected_responses;
                mxb_assert(m_expected_responses == 0);

                // TODO: This would make more sense if it was done at the client protocol level
                session_book_server_response(m_pSession, (SERVER*)backend->target(), true);

                /** Store the master's response so that the slave responses can be compared to it */
                m_sescmd_responses[id] = std::make_pair(backend, reply.error());

                constexpr const char* LEVEL = "SERIALIZABLE";

                if (reply.get_variable("trx_characteristics").find(LEVEL) != std::string::npos
                    || reply.get_variable("tx_isolation").find(LEVEL) != std::string::npos)
                {
                    MXS_INFO("Transaction isolation level set to %s, locking session to master", LEVEL);
                    m_locked_to_master = true;
                    lock_to_master();
                }

                if (reply.error())
                {
                    MXS_INFO("Session command no. %lu returned an error: %s",
                             id, reply.error().message().c_str());
                }
                else if (command == MXS_COM_STMT_PREPARE)
                {
                    /** Map the returned response to the internal ID */
                    m_qc.ps_store_response(reply.generated_id(), reply.param_count());
                }

                // Discard any slave connections that did not return the same result
                for (auto& a : m_slave_responses)
                {
                    const auto& replier = m_sescmd_responses[id];
                    discard_if_response_differs(a.first, replier.first, replier.second, a.second, sescmd);
                }

                m_slave_responses.clear();

                if (!m_config.disable_sescmd_history
                    && (command == MXS_COM_CHANGE_USER || command == MXS_COM_RESET_CONNECTION))
                {
                    mxb_assert_message(!m_sescmd_list.empty(), "Must have stored session commands");
                    MXS_INFO("Resetting session command history to position %lu", id);
                    m_sescmd_prune_pos = id;
                }
            }
            else
            {
                MXS_INFO("Session command response from %s not yet complete", backend->name());
            }
        }
        else
        {
            /** Record slave command so that the response can be validated
             * against the master's response when it arrives. */
            m_slave_responses[backend] = reply.error();
        }
    }
    else
    {
        const auto& replier = m_sescmd_responses[id];
        discard_if_response_differs(backend, replier.first, replier.second, reply.error(), sescmd);
    }

    if (discard)
    {
        gwbuf_free(*ppPacket);
        *ppPacket = NULL;
    }

    if (reply.is_complete() && backend->in_use())
    {
        // The backend can be closed in discard_if_response_differs if the response differs which is why
        // we need to check it again here
        backend->complete_session_command();
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

/**
 * Compress session command history
 *
 * This function removes data duplication by sharing buffers between session
 * commands that have identical data. Only one copy of the actual data is stored
 * for each unique session command.
 *
 * @param sescmd Executed session command
 */
void RWSplitSession::compress_history(mxs::SSessionCommand& sescmd)
{
    auto eq = [&](mxs::SSessionCommand& scmd) {
            return scmd->eq(*sescmd);
        };

    auto first = std::find_if(m_sescmd_list.begin(), m_sescmd_list.end(), eq);

    if (first != m_sescmd_list.end())
    {
        // Duplicate command, use a reference of the old command instead of duplicating it
        sescmd->mark_as_duplicate(**first);
    }
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
