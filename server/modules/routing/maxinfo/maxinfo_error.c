/*
 * This file is distributed as part of MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file maxinfo_error.c - Handle error reporting for the maxinfo router
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/02/15	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <session.h>
#include <router.h>
#include <modules.h>
#include <modinfo.h>
#include <modutil.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <maxscale/poll.h>
#include <maxinfo.h>
#include <skygw_utils.h>
#include <log_manager.h>


/**
 * Process a parse error and send error report to client
 *
 * @param dcb	The DCB to send to error
 * @param sql	The SQL that had the parse error
 * @param err	The parse error code
 */
void
maxinfo_send_parse_error(DCB *dcb, char *sql, PARSE_ERROR err)
{
char	*desc = "";
char	*msg;
int	len;

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
	if ((msg = (char *)malloc(len)) == NULL)
		return;
	sprintf(msg, "%s in query '%s'", desc, sql);
	maxinfo_send_error(dcb, 1149, msg);
	free(msg);
}

/**
 * Construct an error response
 *
 * @param dcb		The DCB to send the error packet to
 * @param msg		The slave server instance
 */
void
maxinfo_send_error(DCB *dcb, int errcode, char  *msg)
{
GWBUF		*pkt;
unsigned char   *data;
int             len;

        len = strlen(msg) + 9;
        if ((pkt = gwbuf_alloc(len + 4)) == NULL)
                return;
        data = GWBUF_DATA(pkt);
	data[0] = len & 0xff;			// Payload length
	data[1] = (len >> 8) & 0xff;
	data[2] = (len >> 16) & 0xff;
        data[3] = 1;				// Sequence id
						// Payload
        data[4] = 0xff;				// Error indicator
	data[5] = errcode & 0xff;		// Error Code
	data[6] = (errcode >> 8) & 0xff;	// Error Code
	memcpy(&data[7], "#42000", 6);
        memcpy(&data[13], msg, strlen(msg));	// Error Message
	dcb->func.write(dcb, pkt);
}
