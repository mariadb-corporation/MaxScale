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

#include "mysql_client_server_protocol.h"

static char *version_str = "V1.0.0";

//static int gw_create_backend_connection(DCB *client_dcb, int efd);
static MySQLProtocol *gw_mysql_init(MySQLProtocol *data);
static void gw_mysql_close(MySQLProtocol **ptr);

extern gw_read_backend_event(DCB* dcb, int epfd);
extern gw_write_backend_event(DCB *dcb, int epfd);
extern int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
extern int gw_error_backend_event(DCB *dcb, int epfd, int event);

///////////////////////////////
// Initialize mysql protocol struct
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
// free data scructure for MySQLProtocol
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

/*
 * Create a new MySQL backend connection.
 *
 * This routine performs the MySQL connection to the backend and fills the session->backends of the callier dcb
 * with the new allocatetd dcb and adds the new socket to the epoll set
 *
 * - backend dcb allocation
 * - MySQL session data fetch
 * - backend connection using data in MySQL session
 *
 * @param client_dcb The client DCB struct
 * @param epfd The epoll set to add the new connection
 * @return 0 on Success or 1 on Failure.
 */

/*
 * This function cannot work as it will be called from mysql_client.c but it needs function pointers from mysql_backend.c
 * They are modules loaded separately!!
 *
int gw_create_backend_connection(DCB *client_dcb, int efd) {
	struct epoll_event ee;
	DCB *backend = NULL;
	MySQLProtocol *ptr_proto = NULL;
	MySQLProtocol *client_protocol = NULL;
	SESSION *session = NULL;
	MYSQL_session *s_data = NULL;

	backend = (DCB *) calloc(1, sizeof(DCB));
	backend->state = DCB_STATE_ALLOC;
	backend->session = NULL;
	backend->protocol = (MySQLProtocol *)gw_mysql_init(NULL);

	ptr_proto = (MySQLProtocol *)backend->protocol;
	client_protocol = (MySQLProtocol *)client_dcb->protocol;
	session = DCB_SESSION(client_dcb);
	s_data = (MYSQL_session *)session->data;

	// this is blocking until auth done
	if (gw_mysql_connect("127.0.0.1", 3306, s_data->db, s_data->user, s_data->client_sha1, backend->protocol) == 0) {
		fprintf(stderr, "Connected to backend mysql server\n");
		backend->fd = ptr_proto->fd;
		setnonblocking(backend->fd);
	} else {
		fprintf(stderr, "<<<< NOT Connected to backend mysql server!!!\n");
		backend->fd = -1;
	}

	// edge triggering flag added
	ee.events = EPOLLIN | EPOLLET | EPOLLOUT;
	ee.data.ptr = backend;

	// if connected, add it to the epoll
	if (backend->fd > 0) {
		if (epoll_ctl(efd, EPOLL_CTL_ADD, backend->fd, &ee) == -1) {
			perror("epoll_ctl: backend sock");
		} else {
			fprintf(stderr, "--> Backend conn added, bk_fd [%i], scramble [%s], is session with client_fd [%i]\n", ptr_proto->fd, ptr_proto->scramble, client_dcb->fd);
			backend->state = DCB_STATE_POLLING;
			backend->session = DCB_SESSION(client_dcb);
			(backend->func).read = gw_read_backend_event;
			(backend->func).write = gw_MySQLWrite_backend;
			(backend->func).write_ready = gw_write_backend_event;
			(backend->func).error = gw_error_backend_event;

			// assume here one backend only.
			// in session.h
			// struct dcb      *backends;
			// instead of a list **backends;
			client_dcb->session->backends = backend;
		}
		return 0;
	}
	return 1;
}
*/
