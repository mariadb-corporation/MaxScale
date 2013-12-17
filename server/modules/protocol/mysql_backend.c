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
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
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
 * 05/07/2013	Massimiliano Pinto	Added closeSession if backend auth fails
 * 12/07/2013	Massimiliano Pinto	Added Mysql Change User via dcb->func.auth()
 * 15/07/2013	Massimiliano Pinto	Added Mysql session change via dcb->func.session()
 * 17/07/2013	Massimiliano Pinto	Added dcb->command update from gwbuf->command for proper routing
					server replies to client via router->clientReply
 * 04/09/2013	Massimiliano Pinto	Added dcb->session and dcb->session->client checks for NULL
 * 12/09/2013	Massimiliano Pinto	Added checks in gw_read_backend_event() for gw_read_backend_handshake
 * 27/09/2013	Massimiliano Pinto	Changed in gw_read_backend_event the check for dcb_read(), now is if rc < 0
 *
 */

extern int lm_enabled_logfiles_bitmask;

static char *version_str = "V2.0.0";
static int gw_create_backend_connection(DCB *backend, SERVER *server, SESSION *in_session);
static int gw_read_backend_event(DCB* dcb);
static int gw_write_backend_event(DCB *dcb);
static int gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue);
static int gw_error_backend_event(DCB *dcb);
static int gw_backend_close(DCB *dcb);
static int gw_backend_hangup(DCB *dcb);
static int backend_write_delayqueue(DCB *dcb);
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue);
static int gw_change_user(DCB *backend_dcb, SERVER *server, SESSION *in_session, GWBUF *queue);
static int gw_session(DCB *backend_dcb, void *data);
static MYSQL_session* gw_get_shared_session_auth_info(DCB* dcb);

static GWPROTOCOL MyObject = { 
	gw_read_backend_event,			/* Read - EPOLLIN handler	 */
	gw_MySQLWrite_backend,			/* Write - data from gateway	 */
	gw_write_backend_event,			/* WriteReady - EPOLLOUT handler */
	gw_error_backend_event,			/* Error - EPOLLERR handler	 */
	gw_backend_hangup,			/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	gw_create_backend_connection,		/* Connect                       */
	gw_backend_close,			/* Close			 */
	NULL,					/* Listen			 */
	gw_change_user,				/* Authentication		 */
	gw_session				/* Session			 */
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


static MYSQL_session* gw_get_shared_session_auth_info(
        DCB* dcb)
{
        MYSQL_session* auth_info = NULL;
        CHK_DCB(dcb);
        CHK_SESSION(dcb->session);

        spinlock_acquire(&dcb->session->ses_lock);

        if (dcb->session->state != SESSION_STATE_ALLOC) { 
                auth_info = dcb->session->data;
        } else {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "%lu [gw_get_shared_session_auth_info] Couldn't get "
                        "session authentication info. Session in a wrong state %d.",
                        pthread_self(),
                        dcb->session->state)));
        }
        spinlock_release(&dcb->session->ses_lock);

        return auth_info;
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
        int            rc = 0;

        CHK_DCB(dcb);        
	CHK_SESSION(dcb->session);
                
        /*< return only with complete session */
        current_session = gw_get_shared_session_auth_info(dcb);
        ss_dassert(current_session != NULL);

        backend_protocol = (MySQLProtocol *) dcb->protocol;
        CHK_PROTOCOL(backend_protocol);

        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [gw_read_backend_event] Read dcb %p fd %d protocol "
                "state %d, %s.",
                pthread_self(),
                dcb,
                dcb->fd,
                backend_protocol->state,
                STRPROTOCOLSTATE(backend_protocol->state))));
        
        
	/* backend is connected:
	 *
	 * 1. read server handshake
	 * 2. if (success) write auth request
	 * 3.  and return
	 */

        /*<
         * If starting to auhenticate with backend server, lock dcb
         * to prevent overlapping processing of auth messages.
         */
        if (backend_protocol->state == MYSQL_CONNECTED) {

                spinlock_acquire(&dcb->authlock);

                backend_protocol = (MySQLProtocol *) dcb->protocol;
                CHK_PROTOCOL(backend_protocol);

                if (backend_protocol->state == MYSQL_CONNECTED) {
       
                        if (gw_read_backend_handshake(backend_protocol) != 0) {
                                backend_protocol->state = MYSQL_AUTH_FAILED;
                        } else {
                                /* handshake decoded, send the auth credentials */
                                if (gw_send_authentication_to_backend(
                                            current_session->db,
                                            current_session->user,
                                            current_session->client_sha1,
                                            backend_protocol) != 0)
                                {
                                        backend_protocol->state = MYSQL_AUTH_FAILED;
                                } else {
                                        backend_protocol->state = MYSQL_AUTH_RECV;
                                }
                        }
                }
                spinlock_release(&dcb->authlock);
	}

        
	/*
	 * Now:
	 *  -- check the authentication reply from backend
	 * OR
	 * -- handle a previous handshake error
	 */
	if (backend_protocol->state == MYSQL_AUTH_RECV ||
            backend_protocol->state == MYSQL_AUTH_FAILED)
        {
                spinlock_acquire(&dcb->authlock);

                backend_protocol = (MySQLProtocol *) dcb->protocol;
                CHK_PROTOCOL(backend_protocol);

                if (backend_protocol->state == MYSQL_AUTH_RECV ||
                    backend_protocol->state == MYSQL_AUTH_FAILED)
                {
                        ROUTER_OBJECT   *router = NULL;
                        ROUTER          *router_instance = NULL;
                        void            *rsession = NULL;
                        SESSION         *session = dcb->session;
                        int             receive_rc = 0;

                        CHK_SESSION(session);
	
                        router = session->service->router;
                        router_instance = session->service->router_instance;

                        if (backend_protocol->state == MYSQL_AUTH_RECV) {
                                /*<
                                 * Read backed auth reply
                                 */                        
                                receive_rc =
                                        gw_receive_backend_auth(backend_protocol);

                                switch (receive_rc) {
                                case -1:
                                        backend_protocol->state = MYSQL_AUTH_FAILED;
                                
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : backend server didn't "
                                                "accept authentication for user "
                                                "%s.", 
                                                current_session->user)));
                                        break;
                                case 1:
                                        backend_protocol->state = MYSQL_IDLE;
                                        
                                        LOGIF(LD, (skygw_log_write_flush(
                                                LOGFILE_DEBUG,
                                                "%lu [gw_read_backend_event] "
                                                "gw_receive_backend_auth succeed. "
                                                "dcb %p fd %d, user %s.",
                                                pthread_self(),
                                                dcb,
                                                dcb->fd,
                                                current_session->user)));
                                        break;
                                default:
                                        ss_dassert(receive_rc == 0);
                                        LOGIF(LD, (skygw_log_write_flush(
                                                LOGFILE_DEBUG,
                                                "%lu [gw_read_backend_event] "
                                                "gw_receive_backend_auth read "
                                                "successfully "
                                                "nothing. dcb %p fd %d, user %s.",
                                                pthread_self(),
                                                dcb,
                                                dcb->fd,
                                                current_session->user)));
                                        rc = 0;
                                        goto return_with_lock;
                                        break;
                                } /* switch */
                        }

                        if (backend_protocol->state == MYSQL_AUTH_FAILED) {
                                /*<
                                 * vraa : errorHandle
                                 * check the delayq before the reply
                                 */
                                if (dcb->delayq != NULL) {
                                        /* send an error to the client */
                                        mysql_send_custom_error(
                                                dcb->session->client,
                                                1,
                                                0,
                                                "Connection to backend lost.");
                                        // consume all the delay queue
                                        dcb->delayq = gwbuf_consume(
                                                dcb->delayq,
                                                gwbuf_length(dcb->delayq));
                                }
                                rsession = session->router_session;
                                ss_dassert(rsession != NULL);
                                /*<
                                 * vraa : errorHandle
                                 * rsession should never be NULL here.
                                 **/
                                ss_dassert(rsession != NULL);

                                LOGIF(LD, (skygw_log_write_flush(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_backend_event] "
                                        "Call closeSession for backend's "
                                        "router client session.",
                                        pthread_self())));
                                /* close router_session */
                                router->closeSession(router_instance, rsession);
                                rc = 1;
                                goto return_with_lock;
                        }
                        else
                        {
                                ss_dassert(backend_protocol->state == MYSQL_IDLE);
                                LOGIF(LD, (skygw_log_write_flush(
                                        LOGFILE_DEBUG,
                                        "%lu [gw_read_backend_event] "
                                        "gw_receive_backend_auth succeed. Fd %d, "
                                        "user %s.",
                                        pthread_self(),
                                        dcb->fd,
                                        current_session->user)));

                                /* check the delay queue and flush the data */
                                if (dcb->delayq)
                                {
                                        backend_write_delayqueue(dcb);
                                        rc = 1;
                                        goto return_with_lock;
                                }
                        }
                } /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED */
                
                spinlock_release(&dcb->authlock);

        }  /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED */
        
	/* reading MySQL command output from backend and writing to the client */
        {
		GWBUF		*writebuf = NULL;
		ROUTER_OBJECT	*router = NULL;
		ROUTER		*router_instance = NULL;
		void		*rsession = NULL;
		SESSION		*session = dcb->session;

                CHK_SESSION(session);
		/* read available backend data */
		rc = dcb_read(dcb, &writebuf);
                
                if (rc < 0) {
                        /*< vraa : errorHandle */
                        /*<
                         * Backend generated EPOLLIN event and if backend has
                         * failed, connection must be closed to avoid backend
                         * dcb from getting  hanged.
                         */
                        (dcb->func).close(dcb);
                        rc = 0;
                        goto return_rc;
                }

                if (writebuf == NULL) {
                        rc = 0;
                        goto return_rc;
                }
                router = session->service->router;
                router_instance = session->service->router_instance;
                rsession = session->router_session;

		/* Note the gwbuf doesn't have here a valid queue->command
                 * descriptions as it is a fresh new one!
                 * We only have the copied value in dcb->command from
                 * previuos func.write() and this will be used by the
                 * router->clientReply
                 * and pass now the  gwbuf to the router
                 */

                /*<
                 * If dcb->session->client is freed already it may be NULL.
                 */
                if (dcb->session->client != NULL) {
                        client_protocol = SESSION_PROTOCOL(dcb->session,
                                                           MySQLProtocol);
                }
                
                if (client_protocol != NULL) {
                        CHK_PROTOCOL(client_protocol);
                        
                        if (client_protocol->state == MYSQL_IDLE)
                        {
                                router->clientReply(router_instance,
                                                    rsession,
                                                    writebuf,
                                                    dcb);
                                rc = 1;
                        }
                        goto return_rc;
                }
        }
        
return_rc:
        return rc;

return_with_lock:
        spinlock_release(&dcb->authlock);
        goto return_rc;
}

/*
 * EPOLLOUT handler for the MySQL Backend protocol module.
 *
 * @param dcb   The descriptor control block
 * @return      1 in success, 0 in case of failure, 
 */
static int gw_write_backend_event(DCB *dcb) {
        int rc = 0;
	MySQLProtocol *backend_protocol = dcb->protocol;
        
        /*<
         * Don't write to backend if backend_dcb is not in poll set anymore.
         */
        if (dcb->state != DCB_STATE_POLLING) {
                if (dcb->writeq != NULL) {
                        /*< vraa : errorHandle */
                        mysql_send_custom_error(
                                dcb->session->client,
                                1,
                                0,
                                "Writing to backend failed due invalid Maxscale "
                                "state.");
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_write_backend_event] Write to backend "
                                "dcb %p fd %d "
                                "failed due invalid state %s.",
                                pthread_self(),
                                dcb,
                                dcb->fd,
                                STRDCBSTATE(dcb->state))));
                
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Attempt to write buffered data to backend "
                                "failed "
                                "due internal inconsistent state.")));
                        
                        rc = 0;
                } else {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_write_backend_event] Dcb %p in state %s "
                                "but there's nothing to write either.",
                                pthread_self(),
                                dcb,
                                STRDCBSTATE(dcb->state))));
                        rc = 1;
                }
                goto return_rc;                
        }

        if (backend_protocol->state == MYSQL_PENDING_CONNECT) {
                backend_protocol->state = MYSQL_CONNECTED;
                rc = 1;
                goto return_rc;
        }
        dcb_drain_writeq(dcb);
        rc = 1;
return_rc:
        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [gw_write_backend_event] "
                "wrote to dcb %p fd %d, return %d",
                pthread_self(),
                dcb,
                dcb->fd,
                rc)));
        
        return rc;
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
        int rc;

        /*<
         * Don't write to backend if backend_dcb is not in poll set anymore.
         */
	spinlock_acquire(&dcb->authlock);
        if (dcb->state != DCB_STATE_POLLING) {
                /*< vraa : errorHandle */
                /*< Free buffer memory */
                gwbuf_consume(queue, GWBUF_LENGTH(queue));
                
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [gw_MySQLWrite_backend] Write to backend failed. "
                        "Backend dcb %p fd %d is %s.",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(dcb->state))));

		spinlock_release(&dcb->authlock);
                return 0;
        }

	/*<
	 * Now put the incoming data to the delay queue unless backend is
         * connected with auth ok
	 */
	if (backend_protocol->state != MYSQL_IDLE) {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [gw_MySQLWrite_backend] dcb %p fd %d protocol "
                        "state %s.",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRPROTOCOLSTATE(backend_protocol->state))));
                
		backend_set_delayqueue(dcb, queue);
		spinlock_release(&dcb->authlock);
		return 1;
	}

	/*<
	 * Now we set the last command received, from the current queue
	 */
        memcpy(&dcb->command, &queue->command, sizeof(dcb->command));

	spinlock_release(&dcb->authlock);
        rc = dcb_write(dcb, queue);
	return rc;
}

/**
 * Backend Error Handling for EPOLLER
 *
 */
static int gw_error_backend_event(DCB *dcb) {
	SESSION		*session;
	void		*rsession;
	ROUTER_OBJECT	*router;
	ROUTER		*router_instance;
	int		rc = 0;

	CHK_DCB(dcb);
	session = dcb->session;
	CHK_SESSION(session);

	router = session->service->router;
	router_instance = session->service->router_instance;
        
        if (dcb->state != DCB_STATE_POLLING) {
                /*< vraa : errorHandle */
		/*<
		 * if client is not available it needs to be handled in send
		 * function. Session != NULL, that is known.
		 */
                mysql_send_custom_error(
                        dcb->session->client,
                        1,
                        0,
                        "Writing to backend failed.");
                
                rc = 0;
        } else {
                /*< vraa : errorHandle */
        	mysql_send_custom_error(
			dcb->session->client,
			1,
			0,
			"Closed backend connection.");
		rc = 1;
	}
	LOGIF(LE, (skygw_log_write_flush(
		LOGFILE_ERROR,
		"%lu [gw_error_backend_event] Some error occurred in backend. "
                "rc = %d",
		pthread_self(),
                rc)));
        rsession = session->router_session;
        ss_dassert(rsession != NULL);
        /*<
         * vraa : errorHandle
         * rsession should never be NULL here.
         */
        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [gw_error_backend_event] "
                "Call closeSession for backend "
                "session.",
                pthread_self())));
        
        router->closeSession(router_instance, rsession);
        
	return rc;
}

/*
 * Create a new backend connection.
 *
 * This routine will connect to a backend server and it is called by dbc_connect
 * in router->newSession
 *
 * @param backend_dcb, in, out, use - backend DCB allocated from dcb_connect
 * @param server, in, use - server to connect to
 * @param session, in use - current session from client DCB
 * @return 0/1 on Success and -1 on Failure.
 * If succesful, returns positive fd to socket which is connected to
 *  backend server. Positive fd is copied to protocol and to dcb.
 * If fails, fd == -1 and socket is closed.
 */
static int gw_create_backend_connection(
        DCB     *backend_dcb,
        SERVER  *server,
        SESSION *session)
{
        MySQLProtocol *protocol = NULL;        
	int           rv = -1;
        int           fd = -1;

        protocol = mysql_protocol_init(backend_dcb, -1);
        ss_dassert(protocol != NULL);
        
        if (protocol == NULL) {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [gw_create_backend_connection] Failed to create "
                        "protocol object for backend connection.",
                        pthread_self())));
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error: Failed to create "
                        "protocol object for backend connection.")));
                goto return_fd;
        }
        
        /*< if succeed, fd > 0, -1 otherwise */
        rv = gw_do_connect_to_backend(server->name, server->port, &fd);
        /*< Assign protocol with backend_dcb */
        backend_dcb->protocol = protocol;

        /*< Set protocol state */
	switch (rv) {
		case 0:
                        ss_dassert(fd > 0);
                        protocol->fd = fd;
			protocol->state = MYSQL_CONNECTED;
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_create_backend_connection] Established "
                                "connection to %s:%i, protocol fd %d client "
                                "fd %d.",
                                pthread_self(),
                                server->name,
                                server->port,
                                protocol->fd,
                                session->client->fd)));
			break;

		case 1:
                        ss_dassert(fd > 0);
                        protocol->state = MYSQL_PENDING_CONNECT;
                        protocol->fd = fd;
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_create_backend_connection] Connection "
                                "pending to %s:%i, protocol fd %d client fd %d.",
                                pthread_self(),
                                server->name,
                                server->port,
                                protocol->fd,
                                session->client->fd)));
			break;

		default:
                        ss_dassert(fd == -1);
                        ss_dassert(protocol->state == MYSQL_ALLOC);
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [gw_create_backend_connection] Connection "
                                "failed to %s:%i, protocol fd %d client fd %d.",
                                pthread_self(),
                                server->name,
                                server->port,
                                protocol->fd,
                                session->client->fd)));
			break;
	} /*< switch */
        
return_fd:
	return fd;
}


/**
 * Hangup routine the backend dcb: it does nothing
 *
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
gw_backend_hangup(DCB *dcb)
{
        /*< vraa : errorHandle */
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
        /*< vraa : errorHandle */
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
        int   rc;

	spinlock_acquire(&dcb->delayqlock);

	localq = dcb->delayq;
	dcb->delayq = NULL;

	/*<
	 * Now we set the last command received, from the delayed queue
	 */

        memcpy(&dcb->command, &localq->command, sizeof(dcb->command));

	spinlock_release(&dcb->delayqlock);
        rc = dcb_write(dcb, localq);

        if (rc == 0) {
                /*< vraa : errorHandle */
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "%lu [backend_write_delayqueue] Some error occurred in "
                        "backend.",
                        pthread_self())));
                
                mysql_send_custom_error(
                        dcb->session->client,
                        1,
                        0,
                        "Unable to write to backend server. Connection was "
                        "closed.");
                dcb_close(dcb);
        }
        return rc;
}
        


static int gw_change_user(DCB *backend, SERVER *server, SESSION *in_session, GWBUF *queue) {
	MYSQL_session *current_session = NULL;
	MySQLProtocol *backend_protocol = NULL;
	MySQLProtocol *client_protocol = NULL;
	char username[MYSQL_USER_MAXLEN+1]="";
	char database[MYSQL_DATABASE_MAXLEN+1]="";
	uint8_t client_sha1[MYSQL_SCRAMBLE_LEN]="";
	uint8_t *client_auth_packet = GWBUF_DATA(queue);
	unsigned int auth_token_len = 0;
	uint8_t *auth_token = NULL;
	int rv = -1;
	int len = 0;
	int auth_ret = 1;

	current_session = (MYSQL_session *)in_session->client->data;
	backend_protocol = backend->protocol;
	client_protocol = in_session->client->protocol;

	queue->command = ROUTER_CHANGE_SESSION;

	// now get the user, after 4 bytes header and 1 byte command
	client_auth_packet += 5;
	strcpy(username,  (char *)client_auth_packet);
	client_auth_packet += strlen(username) + 1;

	// get the auth token len
	memcpy(&auth_token_len, client_auth_packet, 1);
        ss_dassert(auth_token_len >= 0);
        
	client_auth_packet++;

        // allocate memory for token only if auth_token_len > 0
        if (auth_token_len > 0) {
                auth_token = (uint8_t *)malloc(auth_token_len);
                ss_dassert(auth_token != NULL);
                
                if (auth_token == NULL) 
			return rv;
                memcpy(auth_token, client_auth_packet, auth_token_len);
		client_auth_packet += auth_token_len;
        }
        // decode the token and check the password
        // Note: if auth_token_len == 0 && auth_token == NULL, user is without password
        auth_ret = gw_check_mysql_scramble_data(backend->session->client, auth_token, auth_token_len, client_protocol->scramble, sizeof(client_protocol->scramble), username, client_sha1);

        // let's free the auth_token now
        if (auth_token)
                free(auth_token);

        if (auth_ret != 0) {
                /*< vraa : errorHandle */

		// send the error packet
		mysql_send_auth_error(backend->session->client, 1, 0, "Authorization failed on change_user");

        } else {
		// get db name
		strcpy(database, (char *)client_auth_packet);

		rv = gw_send_change_user_to_backend(database, username, client_sha1, backend_protocol);

		/*<
		 * The current queue was not handled by func.write() in gw_send_change_user_to_backend()
		 * We wrote a new gwbuf
		 * Set backend command here!
		 */
		memcpy(&backend->command, &queue->command, sizeof(backend->command));

		/*<
		 * Now copy new data into user session
		 */		
		strcpy(current_session->user, username);
		strcpy(current_session->db, database);
		memcpy(current_session->client_sha1, client_sha1, sizeof(current_session->client_sha1));

	}
	
	// consume all the data received from client

	spinlock_acquire(&backend->writeqlock);

	len = gwbuf_length(queue);
	queue = gwbuf_consume(queue, len);
	
	spinlock_release(&backend->writeqlock);

	return rv;
}

/**
 * Session Change wrapper for func.write
 * The reply packet will be back routed to the right server
 * in the gw_read_backend_event checking the ROUTER_CHANGE_SESSION command in dcb->command
 * 
 * @param
 * @return always 1
 */
static int gw_session(DCB *backend_dcb, void *data) {

	GWBUF *queue = NULL;

	queue = (GWBUF *) data;
	queue->command = ROUTER_CHANGE_SESSION;
	backend_dcb->func.write(backend_dcb, queue);

	return 1;
}
