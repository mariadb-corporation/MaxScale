/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
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
static void discard_if_response_differs(RWBackend* backend, bool master_ok, bool slave_ok,
                                        SSessionCommand sescmd)
{
    if (master_ok != slave_ok)
    {
        uint8_t cmd = sescmd->get_command();
        std::string query = sescmd->to_string();
        MXS_WARNING("Slave server '%s': response (%s) differs from master's response (%s) to %s: `%s`. "
                    "Closing slave connection due to inconsistent session state.",
                    backend->name(), slave_ok ? "OK" : "ERROR", master_ok ? "OK" : "ERROR",
                    STRPACKETTYPE(cmd), query.empty() ? "<no query>" : query.c_str());
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

    if (command == MXS_COM_STMT_PREPARE && !reply.error())
    {
        backend->add_ps_handle(id, reply.generated_id());
    }

    if (m_recv_sescmd < m_sent_sescmd && id == m_recv_sescmd + 1)
    {
        mxb_assert_message(m_sescmd_replier, "New session commands must have a pre-assigned replier");

        if (m_sescmd_replier == backend)
        {
            discard = false;

            if (reply.is_complete())
            {
                /** First reply to this session command, route it to the client */
                ++m_recv_sescmd;
                --m_expected_responses;
                mxb_assert(m_expected_responses == 0);

                /** Store the master's response so that the slave responses can be compared to it */
                m_sescmd_responses[id] = std::make_pair(backend, !reply.error());

                if (reply.error())
                {
                    MXS_INFO("Session command no. %lu returned an error: %s",
                             id, reply.error().message().c_str());
                }
                else if (command == MXS_COM_STMT_PREPARE)
                {
                    /** Map the returned response to the internal ID */
                    MXS_INFO("PS ID %u maps to internal ID %lu", reply.generated_id(), id);
                    m_qc.ps_store_response(id, reply.generated_id(), reply.param_count());
                }

                // Discard any slave connections that did not return the same result
                for (auto& a : m_slave_responses)
                {
                    discard_if_response_differs(a.first, m_sescmd_responses[id].second, a.second, sescmd);
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
            m_slave_responses.push_back(std::make_pair(backend, !reply.error()));
        }
    }
    else
    {
        if (reply.error() && m_sescmd_responses[id].second)
        {
            MXS_WARNING("Session command returned an error on slave '%s': %s",
                        backend->name(), reply.error().message().c_str());
        }

        discard_if_response_differs(backend, m_sescmd_responses[id].second, !reply.error(), sescmd);
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
