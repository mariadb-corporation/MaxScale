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

void process_sescmd_response(ROUTER_CLIENT_SES* rses, SRWBackend& backend,
                             GWBUF** ppPacket, bool* pReconnect)
{
    if (backend->session_command_count())
    {
        /** We are executing a session command */
        if (GWBUF_IS_TYPE_SESCMD_RESPONSE((*ppPacket)))
        {
            uint8_t cmd;
            gwbuf_copy_data(*ppPacket, MYSQL_HEADER_LEN, 1, &cmd);
            uint64_t id = backend->complete_session_command();

            if (rses->recv_sescmd < rses->sent_sescmd &&
                id == rses->recv_sescmd + 1 &&
                (!rses->current_master || // Session doesn't have a master
                 rses->current_master == backend)) // This is the master's response
            {
                /** First reply to this session command, route it to the client */
                ++rses->recv_sescmd;

                /** Store the master's response so that the slave responses can
                 * be compared to it */
                rses->sescmd_responses[id] = cmd;
            }
            else
            {
                /** The reply to this session command has already been sent to
                 * the client, discard it */
                gwbuf_free(*ppPacket);
                *ppPacket = NULL;

                if (rses->sescmd_responses[id] != cmd)
                {
                    MXS_ERROR("Slave server '%s': response differs from master's response. "
                              "Closing connection due to inconsistent session state.",
                              backend->name());
                    backend->close(mxs::Backend::CLOSE_FATAL);
                    *pReconnect = true;
                }
            }
        }
    }
}
