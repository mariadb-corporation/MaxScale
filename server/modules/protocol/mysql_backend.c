/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

#include "mysql_client_server_protocol.h"
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <modutil.h>

/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 * 17/06/2013	Massimiliano Pinto	Added MaxScale To Backends routines
 * 01/07/2013	Massimiliano Pinto	Put Log Manager example code behind SS_DEBUG macros.
 * 03/07/2013	Massimiliano Pinto	Added delayq for incoming data before mysql connection
 * 04/07/2013	Massimiliano Pinto	Added asynchronous MySQL protocol connection to backend
 * 05/07/2013	Massimiliano Pinto	Added closeSession if backend auth fails
 * 12/07/2013	Massimiliano Pinto	Added Mysql Change User via dcb->func.auth()
 * 15/07/2013	Massimiliano Pinto	Added Mysql session change via dcb->func.session()
 * 17/07/2013	Massimiliano Pinto	Added dcb->command update from gwbuf->command for proper routing
					server replies to client via router->clientReply
 * 04/09/2013	Massimiliano Pinto	Added dcb->session and dcb->session->client checks for NULL
 * 12/09/2013	Massimiliano Pinto	Added checks in gw_read_backend_event() for gw_read_backend_handshake
 * 27/09/2013	Massimiliano Pinto	Changed in gw_read_backend_event the check for dcb_read(), now is if rc < 0
 * 24/10/2014	Massimiliano Pinto	Added Mysql user@host @db authentication support
 * 10/11/2014	Massimiliano Pinto	Client charset is passed to backend
 * 19/06/2015   Martin Brampton		Persistent connection handling
 * 07/10/2015   Martin Brampton         Remove calls to dcb_close - should be done by routers
 * 27/10/2015   Martin Brampton         Test for RCAP_TYPE_NO_RSESSION before calling clientReply
 *
 */
#include <modinfo.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_GA,
	GWPROTOCOL_VERSION,
	"The MySQL to backend server protocol"
};

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
static GWBUF* process_response_data (DCB* dcb, GWBUF* readbuf, int nbytes_to_process); 
extern char* create_auth_failed_msg( GWBUF* readbuf, char*  hostaddr, uint8_t*  sha1);
extern char* create_auth_fail_str(char *username, char *hostaddr, char *sha1, char *db, int errcode);
static bool sescmd_response_complete(DCB* dcb);


#if defined(NOT_USED)
  static int gw_session(DCB *backend_dcb, void *data);
#endif
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
        NULL                                    /* Session                       */
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

        if (dcb->session->state != SESSION_STATE_ALLOC && dcb->session->state != SESSION_STATE_DUMMY) { 
                auth_info = dcb->session->data;
        } else {
            MXS_ERROR("%lu [gw_get_shared_session_auth_info] Couldn't get "
                      "session authentication info. Session in a wrong state %d.",
                      pthread_self(),
                      dcb->session->state);
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
        if (dcb->persistentstart)
        {
            dcb->dcb_errhandle_called = true;
            goto return_rc;
        }

	if(dcb->session == NULL)
	{
	    goto return_rc;
	}

	CHK_SESSION(dcb->session);
                
        /*< return only with complete session */
        current_session = gw_get_shared_session_auth_info(dcb);
        ss_dassert(current_session != NULL);

        backend_protocol = (MySQLProtocol *) dcb->protocol;
        CHK_PROTOCOL(backend_protocol);

        MXS_DEBUG("%lu [gw_read_backend_event] Read dcb %p fd %d protocol "
                  "state %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  backend_protocol->protocol_auth_state,
                  STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));
        
        
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
        if (backend_protocol->protocol_auth_state == MYSQL_CONNECTED)
        {
                spinlock_acquire(&dcb->authlock);

                backend_protocol = (MySQLProtocol *) dcb->protocol;
                CHK_PROTOCOL(backend_protocol);

                if (backend_protocol->protocol_auth_state == MYSQL_CONNECTED) 
                {
			/** Read cached backend handshake */
                        if (gw_read_backend_handshake(backend_protocol) != 0) 
                        {
                                backend_protocol->protocol_auth_state = MYSQL_HANDSHAKE_FAILED;

                                MXS_DEBUG("%lu [gw_read_backend_event] after "
                                          "gw_read_backend_handshake, fd %d, "
                                          "state = MYSQL_HANDSHAKE_FAILED.",
                                          pthread_self(),
                                          backend_protocol->owner_dcb->fd);
                        } 
                        else
                        {
                                /** 
				 * Decode password and send the auth credentials
				 * to backend.
				 */
                                if (gw_send_authentication_to_backend(
                                            current_session->db,
                                            current_session->user,
                                            current_session->client_sha1,
                                            backend_protocol) != 0)
                                {
                                        backend_protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
                                        MXS_DEBUG("%lu [gw_read_backend_event] after "
                                                  "gw_send_authentication_to_backend "
                                                  "fd %d, state = MYSQL_AUTH_FAILED.",
                                                  pthread_self(),
                                                  backend_protocol->owner_dcb->fd);
                                }
                                else
                                {
                                        backend_protocol->protocol_auth_state = MYSQL_AUTH_RECV;
                                }
                        }
                }
                spinlock_release(&dcb->authlock);
	} /*< backend_protocol->protocol_auth_state == MYSQL_CONNECTED */
	/*
	 * Now:
	 *  -- check the authentication reply from backend
	 * OR
	 * -- handle a previous handshake error
	 */
	if (backend_protocol->protocol_auth_state == MYSQL_AUTH_RECV ||
            backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED ||
            backend_protocol->protocol_auth_state == MYSQL_AUTH_FAILED)
        {
                spinlock_acquire(&dcb->authlock);

                backend_protocol = (MySQLProtocol *) dcb->protocol;
                CHK_PROTOCOL(backend_protocol);

                if (backend_protocol->protocol_auth_state == MYSQL_AUTH_RECV ||
                    backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED ||
                    backend_protocol->protocol_auth_state == MYSQL_AUTH_FAILED)
                {
                        ROUTER_OBJECT   *router = NULL;
                        ROUTER          *router_instance = NULL;
                        void            *rsession = NULL;
                        SESSION         *session = dcb->session;
                        int             receive_rc = 0;
	
			if (SESSION_STATE_DUMMY == session->state)
			{
				rc = 0;
				goto return_with_lock;
			}
                        CHK_SESSION(session);
                        router = session->service->router;
                        router_instance = session->service->router_instance;
                        rsession = session->router_session;

			if (backend_protocol->protocol_auth_state == MYSQL_AUTH_RECV) 
			{
                                /**
                                 * Read backend's reply to authentication message
                                 */                        
                                receive_rc =
                                        gw_receive_backend_auth(backend_protocol);

                                switch (receive_rc) {
                                case -1:
                                        backend_protocol->protocol_auth_state = MYSQL_AUTH_FAILED;
                                        MXS_DEBUG("%lu [gw_read_backend_event] after "
                                                  "gw_receive_backend_authentication "
                                                  "fd %d, state = MYSQL_AUTH_FAILED.",
                                                  pthread_self(),
                                                  backend_protocol->owner_dcb->fd);

                                        MXS_ERROR("Backend server didn't "
                                                  "accept authentication for user "
                                                  "%s.",
                                                  current_session->user);
                                        break;
                                case 1:
                                        backend_protocol->protocol_auth_state = MYSQL_IDLE;
                                        
                                        MXS_DEBUG("%lu [gw_read_backend_event] "
                                                  "gw_receive_backend_auth succeed. "
                                                  "dcb %p fd %d, user %s.",
                                                  pthread_self(),
                                                  dcb,
                                                  dcb->fd,
                                                  current_session->user);
                                        break;
                                default:
                                        ss_dassert(receive_rc == 0);
                                        MXS_DEBUG("%lu [gw_read_backend_event] "
                                                  "gw_receive_backend_auth read "
                                                  "successfully "
                                                  "nothing. dcb %p fd %d, user %s.",
                                                  pthread_self(),
                                                  dcb,
                                                  dcb->fd,
                                                  current_session->user);
                                        rc = 0;
                                        goto return_with_lock;
                                        break;
                                } /* switch */
                        }

                        if (backend_protocol->protocol_auth_state == MYSQL_AUTH_FAILED ||
                            backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED)
                        {
				GWBUF* errbuf;
				bool   succp;
				/** 
                                 * protocol state won't change anymore, 
                                 * lock can be freed 
                                 */
                                spinlock_release(&dcb->authlock);
                                spinlock_acquire(&dcb->delayqlock);
                                
                                if (dcb->delayq != NULL) 
                                {                                        
                                        while ((dcb->delayq = gwbuf_consume(
                                                dcb->delayq,
                                                GWBUF_LENGTH(dcb->delayq))) != NULL);
                                }
                                spinlock_release(&dcb->delayqlock);

				/* try reload users' table for next connection */
				if (backend_protocol->protocol_auth_state == 
					MYSQL_AUTH_FAILED) 
				{
					service_refresh_users(dcb->session->service);
				}
#if defined(SS_DEBUG)
				MXS_DEBUG("%lu [gw_read_backend_event] "
                                          "calling handleError. Backend "
                                          "DCB %p, session %p",
                                          pthread_self(),
                                          dcb,
                                          dcb->session);
#endif
                                        
				errbuf = mysql_create_custom_error(
					1, 
					0, 
					"Authentication with backend failed. "
					"Session will be closed.");

                                if (rsession)
                                {
                                    router->handleError(router_instance,
						rsession, 
						errbuf, 
						dcb,
						ERRACT_REPLY_CLIENT,
						&succp);
                                }
                                else
                                {
                                    gwbuf_free(errbuf);
                                    dcb->dcb_errhandle_called = true;
                                    /*
                                     * I'm pretty certain this is best removed and
                                     * causes trouble if present, but have left it
                                     * here just for now as a comment. Martin
                                     */
                                    /* dcb_close(dcb); */
				    rc = 1;
				    goto return_rc;
                                }
				gwbuf_free(errbuf);
				
				spinlock_acquire(&session->ses_lock);
				session->state = SESSION_STATE_STOPPING;
				spinlock_release(&session->ses_lock);
				ss_dassert(dcb->dcb_errhandle_called);
                                rc = 1;
                                goto return_rc;
                        }
                        else
                        {
                                ss_dassert(backend_protocol->protocol_auth_state == MYSQL_IDLE);
                                MXS_DEBUG("%lu [gw_read_backend_event] "
                                          "gw_receive_backend_auth succeed. Fd %d, "
                                          "user %s.",
                                          pthread_self(),
                                          dcb->fd,
                                          current_session->user);
                               
                                /* check the delay queue and flush the data */
                                if (dcb->delayq)
                                {
                                        rc = backend_write_delayqueue(dcb);
                                        goto return_with_lock;
                                }
                        }
                } /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED */

                spinlock_release(&dcb->authlock);

        }  /* MYSQL_AUTH_RECV || MYSQL_AUTH_FAILED || MYSQL_HANDSHAKE_FAILED */
        
	/* reading MySQL command output from backend and writing to the client */
        {
		GWBUF         *read_buffer = NULL;
		ROUTER_OBJECT *router = NULL;
		ROUTER        *router_instance = NULL;
		SESSION       *session = dcb->session;
                int           nbytes_read = 0;
                
                CHK_SESSION(session);
                router = session->service->router;
                router_instance = session->service->router_instance;

                /* read available backend data */
                rc = dcb_read(dcb, &read_buffer, 0);

                if (rc < 0) 
                {
                        GWBUF* errbuf;
                        bool   succp;                        
#if defined(SS_DEBUG)
                        MXS_ERROR("Backend read error handling #2.");
#endif
                        errbuf = mysql_create_custom_error(
                                1, 
                                0, 
                                "Read from backend failed");
                        
                        router->handleError(
				router_instance, 
                                session->router_session, 
                                errbuf, 
                                dcb,
                                ERRACT_NEW_CONNECTION,
                                &succp);
			gwbuf_free(errbuf);
			
                        if (!succp)
                        {
                                spinlock_acquire(&session->ses_lock);
                                session->state = SESSION_STATE_STOPPING;
                                spinlock_release(&session->ses_lock);
                        }
                        rc = 0;
                        goto return_rc;
                }
                nbytes_read = gwbuf_length(read_buffer);

                if (nbytes_read == 0 && dcb->dcb_readqueue == NULL)
                {
                        goto return_rc;
                }
                else
                {
                        ss_dassert(read_buffer != NULL || dcb->dcb_readqueue != NULL);
                }
		
		if(dcb->dcb_readqueue)
		{
		    read_buffer = gwbuf_append(dcb->dcb_readqueue,read_buffer);
		}

		nbytes_read = gwbuf_length(read_buffer);
		
		if (nbytes_read < 3)
		{
		    dcb->dcb_readqueue = read_buffer;
		    rc = 0;
		    goto return_rc;
		}

                {
		    GWBUF *tmp = modutil_get_complete_packets(&read_buffer);
		    
		    if(tmp == NULL)
		    {
			/** No complete packets */
			dcb->dcb_readqueue = read_buffer;
			rc = 0;
			goto return_rc;
			
		    }
		    
		    dcb->dcb_readqueue = read_buffer;
		    read_buffer = tmp;
		}

                /** 
                 * If protocol has session command set, concatenate whole 
                 * response into one buffer.
                 */
                if (protocol_get_srv_command((MySQLProtocol *)dcb->protocol, false) != 
                        MYSQL_COM_UNDEFINED)
                {
                        read_buffer = process_response_data(dcb, read_buffer, gwbuf_length(read_buffer));
			/** 
			 * Received incomplete response to session command.
			 * Store it to readqueue and return.
			 */
			if (!sescmd_response_complete(dcb))
			{
				rc = 0;
				goto return_rc;
			}

			if (!read_buffer) {
                            MXS_NOTICE("%lu [gw_read_backend_event] "
                                       "Read buffer unexpectedly null, even though response "
                                       "not marked as complete. User: %s",
                                       pthread_self(),
                                       current_session->user);
				rc = 0;
				goto return_rc;
			}
                }
                /**
                 * Check that session is operable, and that client DCB is 
		 * still listening the socket for replies.
                 */
		if (dcb->session->state == SESSION_STATE_ROUTER_READY &&
			dcb->session->client != NULL && 
			dcb->session->client->state == DCB_STATE_POLLING &&
                        (session->router_session || router->getCapabilities() & RCAP_TYPE_NO_RSESSION))
                {
                        client_protocol = SESSION_PROTOCOL(dcb->session,
                                                           MySQLProtocol);
                	if (client_protocol != NULL)
                        {
				CHK_PROTOCOL(client_protocol);

                                if (client_protocol->protocol_auth_state == 
                                        MYSQL_IDLE)
				{
                                        gwbuf_set_type(read_buffer, GWBUF_TYPE_MYSQL);
                                        
                                        router->clientReply(
                                                router_instance,
                                                session->router_session,
                                                read_buffer,
                                                dcb);
					rc = 1;
				}
				goto return_rc;
                	} 
                	else if (dcb->session->client->dcb_role == DCB_ROLE_INTERNAL) 
                        {
                                gwbuf_set_type(read_buffer, GWBUF_TYPE_MYSQL);
                                router->clientReply(router_instance, session->router_session, read_buffer, dcb);
				rc = 1;
			}
		}
		else /*< session is closing; replying to client isn't possible */
		{
			gwbuf_free(read_buffer);
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
                uint8_t* data;
                
                if (dcb->writeq != NULL)
                {
                        data = (uint8_t *)GWBUF_DATA(dcb->writeq);

			if(dcb->session->client == NULL)
			{
				rc = 0;
			}
                        else if (!(MYSQL_IS_COM_QUIT(data)))
                        {

                                /*< vraa : errorHandle */
                                mysql_send_custom_error(
                                        dcb->session->client,
                                        1,
                                        0,
                                        "Writing to backend failed due invalid Maxscale "
                                        "state.");
                                MXS_DEBUG("%lu [gw_write_backend_event] Write to backend "
                                          "dcb %p fd %d "
                                          "failed due invalid state %s.",
                                          pthread_self(),
                                          dcb,
                                          dcb->fd,
                                          STRDCBSTATE(dcb->state));
                        
                                MXS_ERROR("Attempt to write buffered data to backend "
                                          "failed "
                                          "due internal inconsistent state.");
                                
                                rc = 0;
                        } 
                }
                else
                {
                    MXS_DEBUG("%lu [gw_write_backend_event] Dcb %p in state %s "
                              "but there's nothing to write either.",
                              pthread_self(),
                              dcb,
                              STRDCBSTATE(dcb->state));
                        rc = 1;
                }
                goto return_rc;                
        }

        if (backend_protocol->protocol_auth_state == MYSQL_PENDING_CONNECT) {
                backend_protocol->protocol_auth_state = MYSQL_CONNECTED;
                rc = 1;
                goto return_rc;
        }
        dcb_drain_writeq(dcb);
        rc = 1;
return_rc:
        MXS_DEBUG("%lu [gw_write_backend_event] "
                  "wrote to dcb %p fd %d, return %d",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  rc);
        
        return rc;
}

/*
 * Write function for backend DCB. Store command to protocol.
 *
 * @param dcb	The DCB of the backend
 * @param queue	Queue of buffers to write
 * @return	0 on failure, 1 on success
 */
static int
gw_MySQLWrite_backend(DCB *dcb, GWBUF *queue)
{
	MySQLProtocol *backend_protocol = dcb->protocol;
        int rc = 0; 

        CHK_DCB(dcb);
        spinlock_acquire(&dcb->authlock);
        /**
         * Pick action according to state of protocol. 
         * If auth failed, return value is 0, write and buffered write 
         * return 1.
         */
        switch (backend_protocol->protocol_auth_state) {
                case MYSQL_HANDSHAKE_FAILED:
                case MYSQL_AUTH_FAILED:
                    MXS_ERROR("Unable to write to backend '%s' due to "
                              "%s failure. Server in state %s.",
                              dcb->server->unique_name,
                              backend_protocol->protocol_auth_state == MYSQL_HANDSHAKE_FAILED ?
                              "handshake" : "authentication",
                              STRSRVSTATUS(dcb->server));
                        /** Consume query buffer */
                        while ((queue = gwbuf_consume(
                                                queue,
                                                GWBUF_LENGTH(queue))) != NULL);
                        rc = 0;
                        spinlock_release(&dcb->authlock);
                        goto return_rc;
                        break;
			
                case MYSQL_IDLE:
                {
                        uint8_t* ptr = GWBUF_DATA(queue);
                        int      cmd = MYSQL_GET_COMMAND(ptr);
                        
                        MXS_DEBUG("%lu [gw_MySQLWrite_backend] write to dcb %p "
                                  "fd %d protocol state %s.",
                                  pthread_self(),
                                  dcb,
                                  dcb->fd,
                                  STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));
                        
                        spinlock_release(&dcb->authlock);
                        /**
                         * Statement type is used in readwrite split router. 
                         * Command is *not* set for readconn router.
                         * 
                         * Server commands are stored to MySQLProtocol structure 
                         * if buffer always includes a single statement. 
                         */
                        if (GWBUF_IS_TYPE_SINGLE_STMT(queue) &&
                                GWBUF_IS_TYPE_SESCMD(queue))
                        {
                                /** Record the command to backend's protocol */
                                protocol_add_srv_command(backend_protocol, cmd);
                        }
                        /** Write to backend */
                        rc = dcb_write(dcb, queue);
                        goto return_rc;
                        break;
                }
                
                default:
                {
                    MXS_DEBUG("%lu [gw_MySQLWrite_backend] delayed write to "
                              "dcb %p fd %d protocol state %s.",
                              pthread_self(),
                              dcb,
                              dcb->fd,
                              STRPROTOCOLSTATE(backend_protocol->protocol_auth_state));
                        /** 
                         * In case of session commands, store command to DCB's 
                         * protocol struct.
                         */
                        if (GWBUF_IS_TYPE_SINGLE_STMT(queue) &&
                                GWBUF_IS_TYPE_SESCMD(queue))
                        {
				uint8_t* ptr = GWBUF_DATA(queue);
				int      cmd = MYSQL_GET_COMMAND(ptr);
				
                                /** Record the command to backend's protocol */
                                protocol_add_srv_command(backend_protocol, cmd);
                        }
                        /*<
                         * Now put the incoming data to the delay queue unless backend is
                         * connected with auth ok
                         */
                        backend_set_delayqueue(dcb, queue);
                        spinlock_release(&dcb->authlock);
                        rc = 1;
                        goto return_rc;
                        break;
                }
        }
return_rc:
	return rc;
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error 
 * handler fails in providing enough backend servers, mark session being 
 * closed and call DCB close function which triggers closing router session 
 * and related backends (if any exists.
 */
static int gw_error_backend_event(DCB *dcb) 
{
	SESSION*        session;
	void*           rsession;
	ROUTER_OBJECT*  router;
	ROUTER*         router_instance;
        GWBUF*          errbuf;
        bool            succp;
        session_state_t ses_state;
        
	CHK_DCB(dcb);
	session = dcb->session;
	CHK_SESSION(session);
    if (SESSION_STATE_DUMMY == session->state)
    {
        dcb_close(dcb);
        return 1;
    }
        rsession = session->router_session;
        router = session->service->router;
        router_instance = session->service->router_instance;

        /**
         * Avoid running redundant error handling procedure.
         * dcb_close is already called for the DCB. Thus, either connection is
         * closed by router and COM_QUIT sent or there was an error which
         * have already been handled.
         */
        if (dcb->state != DCB_STATE_POLLING)
        {
		int	error, len;

		len = sizeof(error);
		
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0)
			{
                                char errbuf[STRERROR_BUFLEN];
				MXS_ERROR("DCB in state %s got error '%s'.",
                                          STRDCBSTATE(dcb->state),
                                          strerror_r(error, errbuf, sizeof(errbuf)));
			}
		}
                return 1;
        }
        errbuf = mysql_create_custom_error(
                1, 
                0, 
                "Lost connection to backend server.");
        
        spinlock_acquire(&session->ses_lock);
        ses_state = session->state;
        spinlock_release(&session->ses_lock);
        
        /**
         * Session might be initialized when DCB already is in the poll set.
         * Thus hangup can occur in the middle of session initialization.
         * Only complete and successfully initialized sessions allow for
         * calling error handler.
         */
        while (ses_state == SESSION_STATE_READY)
        {
                spinlock_acquire(&session->ses_lock);
                ses_state = session->state;
                spinlock_release(&session->ses_lock);
        }
        
        if (ses_state != SESSION_STATE_ROUTER_READY)
        {
		int	error, len;

		len = sizeof(error);
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0)
			{
                                char errbuf[STRERROR_BUFLEN];
				MXS_ERROR("Error '%s' in session that is not ready for routing.",
                                          strerror_r(error, errbuf, sizeof(errbuf)));
			}
		}		
                gwbuf_free(errbuf);
                goto retblock;
        }
        
#if defined(SS_DEBUG)
        MXS_INFO("Backend error event handling.");
#endif
        router->handleError(router_instance,
                            rsession,
                            errbuf, 
                            dcb,
                            ERRACT_NEW_CONNECTION,
                            &succp);
        gwbuf_free(errbuf);
	
        /** 
	 * If error handler fails it means that routing session can't continue
	 * and it must be closed. In success, only this DCB is closed.
	 */
        if (!succp)
	{
                spinlock_acquire(&session->ses_lock);
                session->state = SESSION_STATE_STOPPING;
                spinlock_release(&session->ses_lock);
        }
        
retblock:
        return 1;        
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
            MXS_DEBUG("%lu [gw_create_backend_connection] Failed to create "
                      "protocol object for backend connection.",
                      pthread_self());
            MXS_ERROR("Failed to create protocol object for backend connection.");
            goto return_fd;
        }
        
        /** Copy client flags to backend protocol */
	if (backend_dcb->session->client->protocol)
	{ 
		/** Copy client flags to backend protocol */
		protocol->client_capabilities = 
		((MySQLProtocol *)(backend_dcb->session->client->protocol))->client_capabilities;
		/** Copy client charset to backend protocol */
		protocol->charset =
		((MySQLProtocol *)(backend_dcb->session->client->protocol))->charset;
	}
	else
	{
		protocol->client_capabilities = GW_MYSQL_CAPABILITIES_CLIENT;
		protocol->charset = 0x08;
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
			protocol->protocol_auth_state = MYSQL_CONNECTED;
                        MXS_DEBUG("%lu [gw_create_backend_connection] Established "
                                  "connection to %s:%i, protocol fd %d client "
                                  "fd %d.",
                                  pthread_self(),
                                  server->name,
                                  server->port,
                                  protocol->fd,
                                  session->client->fd);
			break;

		case 1:
                        ss_dassert(fd > 0);
                        protocol->protocol_auth_state = MYSQL_PENDING_CONNECT;
                        protocol->fd = fd;
                        MXS_DEBUG("%lu [gw_create_backend_connection] Connection "
                                  "pending to %s:%i, protocol fd %d client fd %d.",
                                  pthread_self(),
                                  server->name,
                                  server->port,
                                  protocol->fd,
                                  session->client->fd);
			break;

		default:
                        ss_dassert(fd == -1);
                        ss_dassert(protocol->protocol_auth_state == MYSQL_ALLOC);
                        MXS_DEBUG("%lu [gw_create_backend_connection] Connection "
                                  "failed to %s:%i, protocol fd %d client fd %d.",
                                  pthread_self(),
                                  server->name,
                                  server->port,
                                  protocol->fd,
                                  session->client->fd);
			break;
	} /*< switch */
        
return_fd:
	return fd;
}


/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error 
 * handler fails in providing enough backend servers, mark session being 
 * closed and call DCB close function which triggers closing router session 
 * and related backends (if any exists.
 *
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
gw_backend_hangup(DCB *dcb)
{
        SESSION*        session;
        void*           rsession;
        ROUTER_OBJECT*  router;
        ROUTER*         router_instance;
        bool            succp;
        GWBUF*          errbuf;
        session_state_t ses_state;
        
        CHK_DCB(dcb);
        if (dcb->persistentstart)
        {
            dcb->dcb_errhandle_called = true;
            goto retblock;
        }
        session = dcb->session;

	if(session == NULL)
	{
	    goto retblock;
	}

	CHK_SESSION(session);
        
        rsession = session->router_session;
        router = session->service->router;
        router_instance = session->service->router_instance;        
        
        errbuf = mysql_create_custom_error(
                1, 
                0, 
                "Lost connection to backend server.");
        
        spinlock_acquire(&session->ses_lock);
        ses_state = session->state;
        spinlock_release(&session->ses_lock);
        
        /**
         * Session might be initialized when DCB already is in the poll set.
         * Thus hangup can occur in the middle of session initialization.
         * Only complete and successfully initialized sessions allow for
         * calling error handler.
         */
        while (ses_state == SESSION_STATE_READY)
        {
                spinlock_acquire(&session->ses_lock);
                ses_state = session->state;
                spinlock_release(&session->ses_lock);
        }
        
        if (ses_state != SESSION_STATE_ROUTER_READY)
        {
		int	error, len;

		len = sizeof(error);
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0 && ses_state != SESSION_STATE_STOPPING)
			{
                                char errbuf[STRERROR_BUFLEN];
				MXS_ERROR("Hangup in session that is not ready for routing, "
                                          "Error reported is '%s'.",
                                          strerror_r(error, errbuf, sizeof(errbuf)));
			}
		}
                gwbuf_free(errbuf);
                /*
                 * I'm pretty certain this is best removed and
                 * causes trouble if present, but have left it
                 * here just for now as a comment. Martin
                 */
                /* dcb_close(dcb); */
                goto retblock;
        }
#if defined(SS_DEBUG)
	if(ses_state != SESSION_STATE_STOPPING)
	{
	    MXS_ERROR("Backend hangup error handling.");
	}
#endif
        
        router->handleError(router_instance,
                            rsession,
                            errbuf, 
                            dcb,
                            ERRACT_NEW_CONNECTION,
                            &succp);
        
	gwbuf_free(errbuf);
        /** There are no required backends available, close session. */
        if (!succp)
        {
#if defined(SS_DEBUG)                
            MXS_ERROR("Backend hangup -> closing session.");
#endif
                spinlock_acquire(&session->ses_lock);
                session->state = SESSION_STATE_STOPPING;
                spinlock_release(&session->ses_lock);
        }
        
retblock:
        return 1;
}

/**
 * Send COM_QUIT to backend so that it can be closed. 
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
gw_backend_close(DCB *dcb)
{
        DCB*     client_dcb;
        SESSION* session;
        GWBUF*   quitbuf;
        
        CHK_DCB(dcb);
        session = dcb->session;

	MXS_DEBUG("%lu [gw_backend_close]",
                  pthread_self());
	
        quitbuf = mysql_create_com_quit(NULL, 0);
        gwbuf_set_type(quitbuf, GWBUF_TYPE_MYSQL);

        /** Send COM_QUIT to the backend being closed */
        mysql_send_com_quit(dcb, 0, quitbuf);
        
        mysql_protocol_done(dcb);
        
        if (session)
        {
            CHK_SESSION(session);
            /** 
             * The lock is needed only to protect the read of session->state and 
             * session->client values. Client's state may change by other thread
             * but client's close and adding client's DCB to zombies list is executed
             * only if client's DCB's state does _not_ change in parallel.
             */
            spinlock_acquire(&session->ses_lock);
            /** 
             * If session->state is STOPPING, start closing client session. 
             * Otherwise only this backend connection is closed.
             */
            if (session->state == SESSION_STATE_STOPPING &&
		session->client != NULL)
            {		
                if (session->client->state == DCB_STATE_POLLING)
                {
                    DCB *temp;
			spinlock_release(&session->ses_lock);
			
                        /** Close client DCB */
                        temp = session->client;
                        session->client = NULL;
                        dcb_close(temp);
                }
                else 
		{
			spinlock_release(&session->ses_lock);
		}
            }
            else
            {
		spinlock_release(&session->ses_lock);
            }
        }
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

        if (dcb->delayq == NULL)
        {
                spinlock_release(&dcb->delayqlock);
                rc = 1;
        }
        else
        {
                localq = dcb->delayq;
                dcb->delayq = NULL;
                spinlock_release(&dcb->delayqlock);
		
		if (MYSQL_IS_CHANGE_USER(((uint8_t *)GWBUF_DATA(localq))))
		{
			MYSQL_session* mses;
			GWBUF*         new_packet;
			
			mses = (MYSQL_session *)dcb->session->client->data;
			new_packet = gw_create_change_user_packet(
					mses,
					(MySQLProtocol *)dcb->protocol);
			/** 
			* Remove previous packet which lacks scramble 
			* and append the new.
			*/
			localq = gwbuf_consume(localq, GWBUF_LENGTH(localq));
			localq = gwbuf_append(localq, new_packet);
		}
		rc = dcb_write(dcb, localq);
        }

        if (rc == 0)
        {
                GWBUF* errbuf;
                bool   succp;
                ROUTER_OBJECT   *router = NULL;
                ROUTER          *router_instance = NULL;
                void            *rsession = NULL;
                SESSION         *session = dcb->session;
                
                CHK_SESSION(session);
 
		if (session != NULL)
		{
			router = session->service->router;
			router_instance = session->service->router_instance;
			rsession = session->router_session;
#if defined(SS_DEBUG)                
			MXS_INFO("Backend write delayqueue error handling.");
#endif
			errbuf = mysql_create_custom_error(
				1, 
				0, 
				"Failed to write buffered data to back-end server. "
				"Buffer was empty or back-end was disconnected during "
				"operation. Attempting to find a new backend.");
			
			router->handleError(router_instance, 
					rsession, 
					errbuf, 
					dcb,
					ERRACT_NEW_CONNECTION,
					&succp);
			gwbuf_free(errbuf);
		
			if (!succp)
			{
                                spinlock_acquire(&session->ses_lock);
                                session->state = SESSION_STATE_STOPPING;
                                spinlock_release(&session->ses_lock);
			}                
		}
	}
        return rc;
}

/**
 * This routine handles the COM_CHANGE_USER command
 *
 * @param dcb		The current backend DCB
 * @param server	The backend server pointer
 * @param in_session	The current session data (MYSQL_session)
 * @param queue		The GWBUF containing the COM_CHANGE_USER receveid
 * @return 1 on success and 0 on failure
 */
static int gw_change_user(
        DCB     *backend, 
        SERVER  *server, 
        SESSION *in_session, 
        GWBUF   *queue) 
{
	MYSQL_session *current_session = NULL;
	MySQLProtocol *backend_protocol = NULL;
	MySQLProtocol *client_protocol = NULL;
	char username[MYSQL_USER_MAXLEN+1]="";
	char database[MYSQL_DATABASE_MAXLEN+1]="";
	char current_database[MYSQL_DATABASE_MAXLEN+1]="";
	uint8_t client_sha1[MYSQL_SCRAMBLE_LEN]="";
	uint8_t *client_auth_packet = GWBUF_DATA(queue);
	unsigned int auth_token_len = 0;
	uint8_t *auth_token = NULL;
	int rv = -1;
	int auth_ret = 1;

	current_session = (MYSQL_session *)in_session->client->data;
	backend_protocol = backend->protocol;
	client_protocol = in_session->client->protocol;

	/* now get the user, after 4 bytes header and 1 byte command */
	client_auth_packet += 5;
	strncpy(username,  (char *)client_auth_packet,MYSQL_USER_MAXLEN);
	client_auth_packet += strlen(username) + 1;

	/* get the auth token len */
	memcpy(&auth_token_len, client_auth_packet, 1);
        
	client_auth_packet++;

        /* allocate memory for token only if auth_token_len > 0 */
        if (auth_token_len > 0) {
                auth_token = (uint8_t *)malloc(auth_token_len);
                ss_dassert(auth_token != NULL);
                
                if (auth_token == NULL) 
			return rv;
                memcpy(auth_token, client_auth_packet, auth_token_len);
		client_auth_packet += auth_token_len;
        }

	/* get new database name */
		strncpy(database, (char *)client_auth_packet,MYSQL_DATABASE_MAXLEN);

	/* get character set */
	if (strlen(database)) {
		client_auth_packet += strlen(database) + 1;
	} else {
		client_auth_packet++;
	}

	if (client_auth_packet && *client_auth_packet)
		memcpy(&backend_protocol->charset, client_auth_packet, sizeof(int));

	/* save current_database name */
	strncpy(current_database, current_session->db,MYSQL_DATABASE_MAXLEN);

	/*
	 * Now clear database name in dcb as we don't do local authentication on db name for change user.
	 * Local authentication only for user@host and if successful the database name change is sent to backend.
	 */
	strcpy(current_session->db, "");

        /*
	 *  decode the token and check the password.
         * Note: if auth_token_len == 0 && auth_token == NULL, user is without password
	 */
        auth_ret = gw_check_mysql_scramble_data(backend->session->client, 
						auth_token, 
						auth_token_len, 
						client_protocol->scramble, 
						sizeof(client_protocol->scramble), 
						username, 
						client_sha1);

	if (auth_ret != 0) {
		if (!service_refresh_users(backend->session->client->service)) {
			/* Try authentication again with new repository data */
			/* Note: if no auth client authentication will fail */
        		auth_ret = gw_check_mysql_scramble_data(
					backend->session->client, 
					auth_token, auth_token_len, 
					client_protocol->scramble, 
					sizeof(client_protocol->scramble), 
					username, 
					client_sha1);
		}
	}

	/* copy back current datbase to client session */
	strcpy(current_session->db, current_database);

        /* let's free the auth_token now */
        if (auth_token)
                free(auth_token);

        if (auth_ret != 0) {
		char *password_set = NULL;
		char *message = NULL;
		GWBUF* 		buf;

		if (auth_token_len > 0)
			password_set = (char *)client_sha1;
		else
			password_set = "";

		/** 
		 * Create an error message and make it look like legit reply
		 * from backend server. Then make it look like an incoming event
		 * so that thread gets new task of it, calls clientReply
		 * which filters out duplicate errors from same cause and forward
		 * reply to the client.
		 */
		message = create_auth_fail_str(username,
						backend->session->client->remote,
						password_set,
						"",
                        auth_ret);
		if (message == NULL)
		{
                    MXS_ERROR("Creating error message failed."); 
                    rv = 0;
                    goto retblock;
		}
		/**
		 * Add command to backend's protocol, create artificial reply
		 * packet and add it to client's read buffer.
		 */
		protocol_add_srv_command((MySQLProtocol*)backend->protocol, 
					 MYSQL_COM_CHANGE_USER);
		modutil_reply_auth_error(backend, message, 0);
		rv = 1;
        } else {
		rv = gw_send_change_user_to_backend(database, username, client_sha1, backend_protocol);
		/*
		 * Now copy new data into user session
		 */
		strcpy(current_session->user, username);
		strcpy(current_session->db, database);
		memcpy(current_session->client_sha1, client_sha1, sizeof(current_session->client_sha1));
        }
        
retblock:
        gwbuf_free(queue);

	return rv;
}


/** 
 * Move packets or parts of packets from readbuf to outbuf as the packet headers
 * and lengths have been noticed and counted.
 * Session commands need to be marked so that they can be handled properly in 
 * the router's clientReply.
 * 
 * @param dcb			Backend's DCB where data was read from
 * @param readbuf		GWBUF where data was read to
 * @param nbytes_to_process	Number of bytes that has been read and need to be processed
 * 
 * @return GWBUF which includes complete MySQL packet
 */
static GWBUF* process_response_data (
        DCB*   dcb,
        GWBUF* readbuf,
        int    nbytes_to_process)
{
        int            npackets_left = 0; /*< response's packet count */
        ssize_t        nbytes_left   = 0; /*< nbytes to be read for the packet */
        MySQLProtocol* p;
        GWBUF*         outbuf = NULL;
	int initial_packets = npackets_left;
	ssize_t initial_bytes = nbytes_left;

        /** Get command which was stored in gw_MySQLWrite_backend */
        p = DCB_PROTOCOL(dcb, MySQLProtocol);
	if (!DCB_IS_CLONE(dcb)) CHK_PROTOCOL(p);
                
        /** All buffers processed here are sescmd responses */
        gwbuf_set_type(readbuf, GWBUF_TYPE_SESCMD_RESPONSE);
        
        /**
         * Now it is known how many packets there should be and how much
         * is read earlier. 
         */
        while (nbytes_to_process != 0)
        {
                mysql_server_cmd_t srvcmd;
                bool               succp;
                
                srvcmd = protocol_get_srv_command(p, false);

		MXS_DEBUG("%lu [process_response_data] Read command %s for DCB %p fd %d.",
                          pthread_self(),
                          STRPACKETTYPE(srvcmd),
                          dcb,
                          dcb->fd);
                /** 
                 * Read values from protocol structure, fails if values are 
                 * uninitialized. 
                 */
                if (npackets_left == 0)
                {
                        succp = protocol_get_response_status(p, &npackets_left, &nbytes_left);
                        
                        if (!succp || npackets_left == 0)
                        {
                                /** 
                                * Examine command type and the readbuf. Conclude response 
                                * packet count from the command type or from the first 
                                * packet content. Fails if read buffer doesn't include 
                                * enough data to read the packet length.
                                */
                                init_response_status(readbuf, srvcmd, &npackets_left, &nbytes_left);
                        }

			initial_packets = npackets_left;
			initial_bytes = nbytes_left;
                }
                /** Only session commands with responses should be processed */
                ss_dassert(npackets_left > 0);

                /** Read incomplete packet. */
                if (nbytes_left > nbytes_to_process)
                {
                        /** Includes length info so it can be processed */
                        if (nbytes_to_process >= 5)
                        {
                                /** discard source buffer */
                                readbuf = gwbuf_consume(readbuf, GWBUF_LENGTH(readbuf));
                                nbytes_left -= nbytes_to_process;
                        }
                        nbytes_to_process = 0;
                }
                /** Packet was read. All bytes belonged to the last packet. */
                else if (nbytes_left == nbytes_to_process)
                {
                        nbytes_left = 0;
                        nbytes_to_process = 0;
                        ss_dassert(npackets_left > 0);
                        npackets_left -= 1;
                        outbuf = gwbuf_append(outbuf, readbuf);
                        readbuf = NULL;
                }
                /** 
                 * Packet was read. There should be more since bytes were 
                 * left over.
                 * Move the next packet to its own buffer and add that next 
                 * to the prev packet's buffer.
                 */
                else /*< nbytes_left < nbytes_to_process */
                {
			ss_dassert(nbytes_left >= 0);
                        nbytes_to_process -= nbytes_left;
                        
                        /** Move the prefix of the buffer to outbuf from redbuf */
                        outbuf = gwbuf_append(outbuf, 
					      gwbuf_clone_portion(readbuf, 0, (size_t)nbytes_left));
                        readbuf = gwbuf_consume(readbuf, (size_t)nbytes_left);
                        ss_dassert(npackets_left > 0);
                        npackets_left -= 1;
                        nbytes_left = 0;
                }
                
                /** Store new status to protocol structure */
                protocol_set_response_status(p, npackets_left, nbytes_left);  
                
                /** A complete packet was read */
                if (nbytes_left == 0)
                {                        
                        /** No more packets in this response */
                        if (npackets_left == 0 && outbuf != NULL)
                        {
                                GWBUF* b = outbuf;
                                
                                while (b->next != NULL)
                                {
                                        b = b->next;
                                }
                                /** Mark last as end of response */
                                gwbuf_set_type(b, GWBUF_TYPE_RESPONSE_END);

                                /** Archive the command */
                                protocol_archive_srv_command(p);                                
                        }
                        /** Read next packet */
                        else
                        {
                                uint8_t* data;

                                /** Read next packet length if there is at least
				 * three bytes left. If there is less than three
				 * bytes in the buffer or it is NULL, we need to
				 wait for more data from the backend server.*/
				if(readbuf == NULL || GWBUF_LENGTH(readbuf) < 3)
				{
				    MXS_DEBUG("%lu [%s] Read %d packets. Waiting for %d more "
                                              "packets for a total of %d packets.",
                                              pthread_self(),__FUNCTION__,
                                              initial_packets - npackets_left,
                                              npackets_left,initial_packets);

				    /** Store the already read data into the readqueue of the DCB
				     * and restore the response status to the initial number of packets */
				    dcb->dcb_readqueue = gwbuf_append(outbuf,dcb->dcb_readqueue);
				    protocol_set_response_status(p, initial_packets, initial_bytes);
				    return NULL;
				}

				data = GWBUF_DATA(readbuf);
				nbytes_left = MYSQL_GET_PACKET_LEN(data)+MYSQL_HEADER_LEN;
				/** Store new status to protocol structure */
				protocol_set_response_status(p, npackets_left, nbytes_left);
                        }
                }
        }
        return outbuf;
}


static bool sescmd_response_complete(
	DCB* dcb)
{
	int 		npackets_left;
	ssize_t 	nbytes_left;
	MySQLProtocol* 	p;
	bool		succp;
	
	p = DCB_PROTOCOL(dcb, MySQLProtocol);
	if (!DCB_IS_CLONE(dcb)) CHK_PROTOCOL(p);
	
	protocol_get_response_status(p, &npackets_left, &nbytes_left);
	
	if (npackets_left == 0)
	{
		succp = true;
	}
	else 
	{
		succp = false;
	}
	return succp;
}
