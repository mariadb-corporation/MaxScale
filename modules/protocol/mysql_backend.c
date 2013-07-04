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

#include "mysql_client_server_protocol.h"
#if defined(SS_DEBUG)
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#endif
/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 * 17/06/2013	Massimiliano Pinto	Added Gateway To Backends routines
 * 27/06/2013	Vilho Raatikka  Added skygw_log_write command as an example
 *                          and necessary headers.
 * 01/07/2013	Massimiliano Pinto	Put Log Manager example code behind SS_DEBUG macros.
 * 03/07/2013	Massimiliano Pinto	Added delayq for incoming data before mysql connection
 * 04/07/2013	Massimiliano Pinto	Added asyncrhronous MySQL protocol connection to backend
 */

static char *version_str = "V1.0.0";
int gw_mysql_connect(char *host, int port, char *dbname, char *user, uint8_t *passwd, MySQLProtocol *conn);
static int gw_create_backend_connection(DCB *client_dcb, SERVER *server, SESSION *in_session);
static int gw_read_backend_event(DCB* dcb);
static int gw_write_backend_event(DCB *dcb);
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
static int gw_error_backend_event(DCB *dcb);
static int gw_backend_close(DCB *dcb);
static int gw_backend_hangup(DCB *dcb);
static int backend_write_delayqueue(DCB *dcb);
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue);

extern char *gw_strend(register const char *s);

static GWPROTOCOL MyObject = { 
	gw_read_backend_event,			/* Read - EPOLLIN handler	 */
	gw_MySQLWrite_backend,			/* Write - data from gateway	 */
	gw_write_backend_event,			/* WriteReady - EPOLLOUT handler */
	gw_error_backend_event,			/* Error - EPOLLERR handler	 */
	gw_backend_hangup,			/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	gw_create_backend_connection,		/* Connect			 */
	gw_backend_close,			/* Close			 */
	NULL					/* Listen			 */
	};

/*
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/*
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
#if defined(SS_DEBUG)
        skygw_log_write(NULL,
                        LOGFILE_MESSAGE,
                        strdup("Initial MySQL Backend Protcol module."));
#endif
	fprintf(stderr, "Initial MySQL Backend Protcol module.\n");
}

/*
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWPROTOCOL *
GetModuleObject()
{
	return &MyObject;
}


/**
 * Backend Read Event for EPOLLIN on the MySQL backend protocol module
 * @param dcb   The backend Descriptor Control Block
 * @return 1 on operation, 0 for no action
 */

static int gw_read_backend_event(DCB *dcb) {
	MySQLProtocol *client_protocol = NULL;
	MySQLProtocol *backend_protocol = NULL;
	MYSQL_session *current_session = NULL;

	if(dcb->session) {
		client_protocol = SESSION_PROTOCOL(dcb->session, MySQLProtocol);
	}

	backend_protocol = (MySQLProtocol *) dcb->protocol;
	current_session = (MYSQL_session *)dcb->session->data;

	//fprintf(stderr, ">>> backend EPOLLIN from %i, protocol state [%s]\n", dcb->fd, gw_mysql_protocol_state2string(backend_protocol->state));

	// backend is connected: read server handshake and write auth request and return
	if (backend_protocol->state == MYSQL_CONNECTED) {

		gw_read_backend_handshake(backend_protocol);

		gw_send_authentication_to_backend(current_session->db, current_session->user, current_session->client_sha1, backend_protocol);
		return 1;
	}

	// ready to check the authentication reply
	if (backend_protocol->state == MYSQL_AUTH_RECV) {
		int rv = -1;
		rv = gw_receive_backend_auth(backend_protocol);

		switch (rv) {
			case MYSQL_FAILED_AUTHENTICATION:
				backend_protocol->state = MYSQL_AUTH_FAILED;

				// this will close the opened backend socket
				dcb_close(dcb);

				return 1;

			case MYSQL_SUCCESFUL_AUTHENTICATION:
				spinlock_acquire(&dcb->authlock);

				backend_protocol->state = MYSQL_IDLE;

				// check the delay queue
				if(dcb->delayq) {
					backend_write_delayqueue(dcb);
					spinlock_release(&dcb->authlock);
					return 1;
				}
				spinlock_release(&dcb->authlock);

				return 1;

			default:
				// no other authentication state here right now, so just return
				return 0;
		}
	}

	// reading MySQL command output from backend and writing to the client

	if ((client_protocol->state == MYSQL_WAITING_RESULT) || (client_protocol->state == MYSQL_IDLE)) {
		GWBUF   *head = NULL;

		// read data
		dcb_read(dcb, &head);

		// write the gwbuffer to client
		dcb->session->client->func.write(dcb->session->client, head);

		return 1;
	}

	return 0;
}

/*
 * EPOLLOUT handler for the MySQL Backend protocol module.
 *
 * @param dcb   The descriptor control block
 * @return      The number of bytes written
 */
static int gw_write_backend_event(DCB *dcb) {
	MySQLProtocol *backend_protocol = dcb->protocol;

	//fprintf(stderr, ">>> backend EPOLLOUT %i, protocol state [%s]\n", backend_protocol->fd, gw_mysql_protocol_state2string(backend_protocol->state));

	// spinlock_acquire(&dcb->connectlock);

	if (backend_protocol->state == MYSQL_PENDING_CONNECT) {
		//fprintf(stderr, ">>>> Now the backend %i is CONNECTED\n", backend_protocol->fd);
		backend_protocol->state = MYSQL_CONNECTED;

		// spinlock_release(&dcb->connectlock);

		return 1;
	}

	// spinlock_release(&dcb->connectlock);

        return dcb_drain_writeq(dcb);
}

/*
 * Write function for backend DCB
 *
 * @param dcb	The DCB of the backend
 * @param queue	Queue of buffers to write
 * @return	0 on failure, 1 on success
 */
static int
gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue)
{
	MySQLProtocol *backend_protocol = dcb->protocol;

	spinlock_acquire(&dcb->authlock);

	// put incoming data to the delay queue unless backend is connected with auth ok
	if (backend_protocol->state != MYSQL_IDLE) {
		fprintf(stderr, ">>> Writing in the backend %i delay queue\n", dcb->fd);

		backend_set_delayqueue(dcb, queue);
		spinlock_release(&dcb->authlock);
		return 1;
	}

	spinlock_release(&dcb->authlock);

	// Normal flow of backend write;
	return dcb_write(dcb, queue);
}

/**
 * Backend Error Handling
 *
 */
static int gw_error_backend_event(DCB *dcb) {

        fprintf(stderr, "#### Handle Backend error function for %i\n", dcb->fd);

	dcb_close(dcb);

	return 1;
}

/*
 * Create a new ackend connection.
 *
 * This routine will connect to a backend server
 *
 * - backend dcb allocation
 * - MySQL session data fetch
 * - backend connection using data in MySQL session
 *
 * @param backend The Backend DCB allocated from dcb_connect
 * @param server  The selected server to connect to
 * @param session The current session from Client DCB
 * @return 0 on Success or 1 on Failure.
 */

static int gw_create_backend_connection(DCB *backend, SERVER *server, SESSION *session) {
	MySQLProtocol *protocol = NULL;
	MYSQL_session *s_data = NULL;
	int rv = -1;

	//fprintf(stderr, "HERE, the server to connect is [%s]:[%i]\n", server->name, server->port);

	protocol = (MySQLProtocol *) calloc(1, sizeof(MySQLProtocol));
	protocol->state = MYSQL_ALLOC;

	backend->protocol = protocol;

	// put the backend dcb in the protocol struct
	protocol->descriptor = backend;

	s_data = (MYSQL_session *)session->client->data;

	// let's try to connecte to a backend server, only connect sys call
	// The socket descriptor is in Non Blocking status, this is set in the function
	rv = gw_do_connect_to_backend(server->name, server->port, protocol);

	// we could also move later, this in to the gw_do_connect_to_backend using protocol->descriptor

	memcpy(&backend->fd,  &protocol->fd, sizeof(backend->fd));

	switch (rv) {

		case 0:
			fprintf(stderr, "Connected to backend mysql server: fd is %i\n", backend->fd);
			protocol->state = MYSQL_CONNECTED;

			break;

		case 1:
			fprintf(stderr, ">>> Connection is PENDING to backend mysql server: fd is %i\n", backend->fd);
			protocol->state = MYSQL_PENDING_CONNECT;	

			break;

		default:
			fprintf(stderr, ">>> ERROR: NOT Connected to the backend mysql server!!!\n");
			backend->fd = -1;

			break;
	}

	fprintf(stderr, "--> Backend conn added [%i], in the client session [%i]\n", backend->fd, session->client->fd);

	backend->state = DCB_STATE_POLLING;

	return backend->fd;
}

/**
 * Hangup routine the backend dcb: it does nothing right now
 *
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
gw_backend_hangup(DCB *dcb)
{
	return 1;
}

/**
 * Close the backend dcb
 *
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
gw_backend_close(DCB *dcb)
{
        dcb_close(dcb);
	return 1;
}

/**
 * This routine put into the delay queue the input queue
 * The input is what backend DCB is receiving
 * The routine is called from func.write() when mysql backend connection
 * is not yet complete buu there are inout data from client
 *
 * @param dcb   The current backend DCB
 * @param queue Input data in the GWBUF struct
 */
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue) {
	spinlock_acquire(&dcb->delayqlock);

	if (dcb->delayq) {
		/* Append data */
		dcb->delayq = gwbuf_append(dcb->delayq, queue);
	} else {
		if (queue != NULL) {
			/* create the delay queue */
			dcb->delayq = queue;
		}
	}

	spinlock_release(&dcb->delayqlock);
}

/**
 * This routine writes the delayq via dcb_write
 * The dcb->delayq contains data received from the client before
 * mysql backend authentication succeded
 *
 * @param dcb The current backend DCB
 * @return The dcb_write status
 */
static int backend_write_delayqueue(DCB *dcb)
{
	GWBUF *localq = NULL;

	spinlock_acquire(&dcb->delayqlock);

	localq = dcb->delayq;
	dcb->delayq = NULL;

	spinlock_release(&dcb->delayqlock);

	return dcb_write(dcb, localq);
}
/////
