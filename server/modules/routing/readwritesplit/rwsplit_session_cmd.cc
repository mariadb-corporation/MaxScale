/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplit_internal.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>

/**
 * Functions for session command handling
 */


static std::string extract_error(GWBUF* buffer)
{
    std::string rval;

    if (MYSQL_IS_ERROR_PACKET(((uint8_t *)GWBUF_DATA(buffer))))
    {
        size_t replylen = MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buffer));
        char replybuf[replylen];
        gwbuf_copy_data(buffer, 0, sizeof(replybuf), (uint8_t*)replybuf);
        std::string err;
        std::string msg;
        err.append(replybuf + 8, 5);
        msg.append(replybuf + 13, replylen - 4 - 5);
        rval = err + ": " + msg;
    }

    return rval;
}

/**
 * Discards the slave connection if its response differs from the master's response
 *
 * @param backend    The slave Backend
 * @param master_cmd Master's reply
 * @param slave_cmd  Slave's reply
 *
 * @return True if the responses were different and connection was discarded
 */
static bool discard_if_response_differs(SRWBackend backend, uint8_t master_response,
                                        uint8_t slave_response, mxs::SSessionCommand sescmd)
{
    bool rval = false;

    if (master_response != slave_response)
    {
        uint8_t cmd = sescmd->get_command();
        std::string query = sescmd->to_string();
        MXS_WARNING("Slave server '%s': response (0x%02hhx) differs "
                    "from master's response (0x%02hhx) to %s: `%s`. "
                    "Closing slave connection due to inconsistent session state.",
                    backend->name(), slave_response, master_response, STRPACKETTYPE(cmd),
                    query.empty() ? "<no query>" : query.c_str());
        backend->close(mxs::Backend::CLOSE_FATAL);
        rval = true;
    }

    return rval;
}

void process_sescmd_response(RWSplitSession* rses, SRWBackend& backend,
                             GWBUF** ppPacket, bool* pReconnect)
{
    if (backend->session_command_count())
    {
        /** We are executing a session command */
        if (GWBUF_IS_TYPE_SESCMD_RESPONSE((*ppPacket)))
        {
            uint8_t cmd;
            gwbuf_copy_data(*ppPacket, MYSQL_HEADER_LEN, 1, &cmd);
            uint8_t command = backend->next_session_command()->get_command();
            mxs::SSessionCommand sescmd = backend->next_session_command();
            uint64_t id = backend->complete_session_command();
            MXS_PS_RESPONSE resp = {};
            bool discard = true;

            if (command == MXS_COM_STMT_PREPARE && cmd != MYSQL_REPLY_ERR)
            {
                // This should never fail or the backend protocol is broken
                ss_debug(bool b = )mxs_mysql_extract_ps_response(*ppPacket, &resp);
                ss_dassert(b);
                backend->add_ps_handle(id, resp.id);
            }

            if (rses->recv_sescmd < rses->sent_sescmd && id == rses->recv_sescmd + 1)
            {
                if (!rses->current_master || !rses->current_master->in_use() || // Session doesn't have a master
                    rses->current_master == backend) // This is the master's response
                {
                    /** First reply to this session command, route it to the client */
                    ++rses->recv_sescmd;
                    discard = false;

                    /** Store the master's response so that the slave responses can
                     * be compared to it */
                    rses->sescmd_responses[id] = cmd;

                    if (cmd == MYSQL_REPLY_ERR)
                    {
                        MXS_INFO("Session command no. %lu failed: %s",
                                 id, extract_error(*ppPacket).c_str());
                    }
                    else if (command == MXS_COM_STMT_PREPARE)
                    {
                        /** Map the returned response to the internal ID */
                        MXS_INFO("PS ID %u maps to internal ID %lu", resp.id, id);
                        rses->ps_handles[resp.id] = (id << 32) + resp.parameters;
                    }

                    // Discard any slave connections that did not return the same result
                    for (SlaveResponseList::iterator it = rses->slave_responses.begin();
                         it != rses->slave_responses.end(); it++)
                    {
                        if (discard_if_response_differs(it->first, cmd, it->second, sescmd))
                        {
                            *pReconnect = true;
                        }
                    }

                    rses->slave_responses.clear();
                }
                else
                {
                    /** Record slave command so that the response can be validated
                     * against the master's response when it arrives. */
                    rses->slave_responses.push_back(std::make_pair(backend, cmd));
                }
            }
            else if (discard_if_response_differs(backend, rses->sescmd_responses[id], cmd, sescmd))
            {
                *pReconnect = true;
            }

            if (discard)
            {
                gwbuf_free(*ppPacket);
                *ppPacket = NULL;
            }
        }
    }
}
