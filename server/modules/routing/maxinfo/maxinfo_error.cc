/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file maxinfo_error.c - Handle error reporting for the maxinfo router
 *
 * @verbatim
 * Revision History
 *
 * Date     Who          Description
 * 17/02/15 Mark Riddoch Initial implementation
 *
 * @endverbatim
 */

#include "maxinfo.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/router.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxbase/atomic.h>
#include <maxscale/dcb.hh>
#include <maxscale/poll.hh>


/**
 * Process a parse error and send error report to client
 *
 * @param dcb   The DCB to send to error
 * @param sql   The SQL that had the parse error
 * @param err   The parse error code
 */
void maxinfo_send_parse_error(DCB* dcb, char* sql, PARSE_ERROR err)
{
    const char* desc = "";
    char* msg;
    int len;

    switch (err)
    {
    case PARSE_NOERROR:
        desc = "No error";
        break;

    case PARSE_MALFORMED_SHOW:
        desc = "Expected show <command> [like <pattern>]";
        break;

    case PARSE_EXPECTED_LIKE:
        desc = "Expected LIKE <pattern>";
        break;

    case PARSE_SYNTAX_ERROR:
        desc = "Syntax error";
        break;
    }

    len = strlen(sql) + strlen(desc) + 20;
    msg = (char*)MXS_MALLOC(len);
    MXS_ABORT_IF_NULL(msg);
    sprintf(msg, "%s in query '%s'", desc, sql);
    maxinfo_send_error(dcb, 1149, msg);
    MXS_FREE(msg);
}

/**
 * Construct an error response
 *
 * @param dcb       The DCB to send the error packet to
 * @param msg       The slave server instance
 */
void maxinfo_send_error(DCB* dcb, int errcode, const char* msg)
{
    GWBUF* pkt;
    unsigned char* data;
    int len;

    len = strlen(msg) + 9;
    if ((pkt = gwbuf_alloc(len + 4)) == NULL)
    {
        return;
    }
    data = GWBUF_DATA(pkt);
    data[0] = len & 0xff;           // Payload length
    data[1] = (len >> 8) & 0xff;
    data[2] = (len >> 16) & 0xff;
    data[3] = 1;                    // Sequence id
    // Payload
    data[4] = 0xff;                 // Error indicator
    data[5] = errcode & 0xff;       // Error Code
    data[6] = (errcode >> 8) & 0xff;// Error Code
    memcpy(&data[7], "#42000", 6);
    memcpy(&data[13], msg, strlen(msg));    // Error Message
    dcb->func.write(dcb, pkt);
}
