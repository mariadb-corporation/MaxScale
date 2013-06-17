/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 */

/*
 * MySQL Protocol common routines for client to gateway and gateway to backend
 *
 * Revision History
 * Date         Who                     Description
 * 17/06/2013   Massimiliano Pinto      Common MySQL protocol routines
 */

#include "mysql_protocol.h"

static char *version_str = "V1.0.0";

///////////////////////////////////////
MySQLProtocol *gw_mysql_init(MySQLProtocol *data) {
        int rv = -1;

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

#ifdef MYSQL_CONN_DEBUG
	fprintf(stderr, "Closing MySQL connection %i, [%s]\n", conn->fd, conn->scramble);
#endif

	if (conn->fd > 0) {
		//COM_QUIT will not be sent here, but from the caller of this routine!
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
	fprintf(stderr, "mysqlgw_mysql_close() free(conn) done\n");
#endif
}
