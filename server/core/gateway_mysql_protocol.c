/*
 * This file is distributed as part of the SkySQL Gateway. It is free
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
 * Copyright SkySQL Ab 2013
 * 
 */

/*
 * Revision History
 *
 * Date		Who			Description
 * 23-05-2013	Massimiliano Pinto	Empty mysql_protocol_handling
 * 						1)send handshake in accept
 *						2) read data
 * 						3) alway send OK
 * 12-06-2013	Mark Riddoch		Move mysql_send_ok and MySQLSendHandshake
 *					to use the new buffer management scheme
 * 13-06-2013	Massimiliano Pinto	Added mysql_authentication check
 * 14-06-2013	Massimiliano Pinto	gw_mysql_do_authentication puts user, db, and client_sha1 in the
					(MYSQL_session *) session->data of client DCB.
					gw_mysql_connect can now access this session->data for
					transparent authentication
 */


#include <gw.h>
#include <dcb.h>
#include <session.h>
#include <buffer.h>
#include <openssl/sha.h>


///////////////////////////////////////
// MYSQL_conn structure setup
///////////////////////////////////////
MySQLProtocol *gw_mysql_init(MySQLProtocol *data) {

        MySQLProtocol *input = NULL;

        if (input == NULL) {
                // structure allocation
                input = calloc(1, sizeof(MySQLProtocol));

                if (input == NULL)
                        return NULL;

        }

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "gw_mysql_init() called\n");
#endif

        return input;
}



//////////////////////////////////////
// close a connection if opened
// free data scructure
//////////////////////////////////////
void gw_mysql_close(MySQLProtocol **ptr) {
	MySQLProtocol *conn = *ptr;

	if (*ptr == NULL)
		return;

	fprintf(stderr, "Closing MySQL connection %i, [%s]\n", conn->fd, conn->scramble);

	if (conn->fd > 0) {
		//write COM_QUIT
		//write

#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "mysqlgw_mysql_close() called for %i\n", conn->fd);
#endif
		close(conn->fd);
	} else {
#ifdef MYSQL_CONN_DEBUG
		fprintf(stderr, "mysqlgw_mysql_close() called, no socket %i\n", conn->fd);
#endif
	}

	free(*ptr);

	*ptr = NULL;

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "mysqlgw_mysql_close() free(conn)\n");
#endif
}
