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

#include <plainprotocol.h>
#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <modutil.h>

#define PLAIN_BACKEND_SO_SNDBUF (128 * 1024)
#define PLAIN_BACKEND_SO_RCVBUF (128 * 1024)
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
 * 04/07/2013	Massimiliano Pinto	Added asyncrhronous MySQL protocol connection to backend
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
 *
 */
#include <modinfo.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_GA,
	GWPROTOCOL_VERSION,
	"The plain protocol"
};

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static char *version_str = "V2.0.0";
static int plain_create_backend_connection(DCB *backend, SERVER *server, SESSION *in_session);
static int plain_read_backend_event(DCB* dcb);
static int plain_write_ready_backend_event(DCB *dcb);
static int plain_write_backend(DCB *dcb, GWBUF *queue);
static int plain_error_backend_event(DCB *dcb);
static int plain_backend_close(DCB *dcb);
static int plain_backend_hangup(DCB *dcb);
static int backend_write_delayqueue(DCB *dcb);
static void backend_set_delayqueue(DCB *dcb, GWBUF *queue);
static int plain_change_user(DCB *backend_dcb, SERVER *server, SESSION *in_session, GWBUF *queue);
static GWBUF* process_response_data (DCB* dcb, GWBUF* readbuf, int nbytes_to_process); 
extern char* create_auth_failed_msg( GWBUF* readbuf, char*  hostaddr, uint8_t*  sha1);
extern char* create_auth_fail_str(char *username, char *hostaddr, char *sha1, char *db);
static bool sescmd_response_complete(DCB* dcb);

static GWPROTOCOL MyObject = { 
	plain_read_backend_event,			/* Read - EPOLLIN handler	 */
	plain_write_backend,			/* Write - data from gateway	 */
	plain_write_ready_backend_event,			/* WriteReady - EPOLLOUT handler */
	plain_error_backend_event,			/* Error - EPOLLERR handler	 */
	plain_backend_hangup,			/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	plain_create_backend_connection,		/* Connect                       */
	plain_backend_close,			/* Close			 */
	NULL,					/* Listen			 */
	NULL,				/* Authentication		 */
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


/**
 * Creates MySQL protocol structure
 *
 * @param dcb *          Must be non-NULL.
 * @param fd
 *
 * @return
 *
 *
 * @details Protocol structure does not have fd because dcb is not
 * connected yet.
 *
 */
PlainProtocol* plain_protocol_init(
        DCB* dcb,
        int  fd)
{
        PlainProtocol* p;

	p = (PlainProtocol *) calloc(1, sizeof(PlainProtocol));
        ss_dassert(p != NULL);

        if (p == NULL) {
            int eno = errno;
            errno = 0;
            LOGIF(LE, (skygw_log_write_flush(
                    LOGFILE_ERROR,
                    "%lu [mysql_init_protocol] MySQL protocol init failed : "
                    "memory allocation due error  %d, %s.",
                    pthread_self(),
                    eno,
                    strerror(eno))));
            goto return_p;
        }

        /*< Assign fd with protocol */
        p->fd = fd;
	p->owner_dcb = dcb;

return_p:
        return p;
}


/**
 * Backend Read Event for EPOLLIN on the MySQL backend protocol module
 * @param dcb   The backend Descriptor Control Block
 * @return 1 on operation, 0 for no action
 */
static int plain_read_backend_event(DCB *dcb) {
	PlainProtocol *client_protocol = NULL;
	PlainProtocol *backend_protocol = NULL;

        int            rc = 0;

       
        backend_protocol = (PlainProtocol *) dcb->protocol;
        CHK_PROTOCOL(backend_protocol);

       
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
                rc = dcb_read(dcb, &read_buffer);
                
                if (rc < 0) 
                {
                        GWBUF* errbuf;
                        bool   succp;                        

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
                        ss_dassert(dcb->dcb_errhandle_called);
                        dcb_close(dcb);
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

		if (dcb->session->state == SESSION_STATE_ROUTER_READY &&
			dcb->session->client != NULL && 
			dcb->session->client->state == DCB_STATE_POLLING)
                {
                        client_protocol = SESSION_PROTOCOL(dcb->session,
                                                           PlainProtocol);
                	
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

        goto return_rc;
}

/*
 * EPOLLOUT handler for the MySQL Backend protocol module.
 *
 * @param dcb   The descriptor control block
 * @return      1 in success, 0 in case of failure, 
 */
static int plain_write_ready_backend_event(DCB *dcb) {
        int rc = 0;
	PlainProtocol *backend_protocol = dcb->protocol;
        
        /*<
         * Don't write to backend if backend_dcb is not in poll set anymore.
         */
        if (dcb->state != DCB_STATE_POLLING) {
                uint8_t* data;
                
                if (dcb->writeq != NULL)
                {
                        data = (uint8_t *)GWBUF_DATA(dcb->writeq);
                        
                       
                }
                else
                {
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

        dcb_drain_writeq(dcb);
        rc = 1;
return_rc:

        
        return rc;
}


/**
 * plain_do_connect_to_backend
 *
 * This routine creates socket and connects to a backend server.
 * Connect it non-blocking operation. If connect fails, socket is closed.
 *
 * @param host The host to connect to
 * @param port The host TCP/IP port
 * @param *fd where connected fd is copied
 * @return 0/1 on success and -1 on failure
 * If successful, fd has file descriptor to socket which is connected to
 * backend server. In failure, fd == -1 and socket is closed.
 *
 */
int plain_do_connect_to_backend(
        char	*host,
        int     port,
        int	*fd)
{
	struct sockaddr_in serv_addr;
	int	rv;
	int	so = 0;
	int	bufsize;

	memset(&serv_addr, 0, sizeof serv_addr);
	serv_addr.sin_family = AF_INET;
	so = socket(AF_INET,SOCK_STREAM,0);

	if (so < 0) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error: Establishing connection to backend server "
                        "%s:%d failed.\n\t\t             Socket creation failed "
                        "due %d, %s.",
                        host,
                        port,
                        errno,
                        strerror(errno))));
                rv = -1;
                goto return_rv;
	}
	/* prepare for connect */
	setipaddress(&serv_addr.sin_addr, host);
	serv_addr.sin_port = htons(port);
	bufsize = PLAIN_BACKEND_SO_SNDBUF;

	if(setsockopt(so, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize)) != 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error: Failed to set socket options "
			"%s:%d failed.\n\t\t             Socket configuration failed "
			"due %d, %s.",
			host,
			port,
			errno,
			strerror(errno))));
		rv = -1;
		/** Close socket */
		goto close_so;
	}
	bufsize = PLAIN_BACKEND_SO_RCVBUF;

	if(setsockopt(so, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize)) != 0)
	{
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error: Failed to set socket options "
                        "%s:%d failed.\n\t\t             Socket configuration failed "
                        "due %d, %s.",
                        host,
                        port,
                        errno,
                        strerror(errno))));
		rv = -1;
		/** Close socket */
		goto close_so;
	}

	/* set socket to as non-blocking here */
	setnonblocking(so);
        rv = connect(so, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

        if (rv != 0)
	{
                if (errno == EINPROGRESS)
		{
                        rv = 1;
                }
                else
		{
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error:  Failed to connect backend server %s:%d, "
                                "due %d, %s.",
                                host,
                                port,
                                errno,
                                strerror(errno))));
			/** Close socket */
			goto close_so;
                }
	}
        *fd = so;
        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [plain_do_connect_to_backend] Connected to backend server "
                "%s:%d, fd %d.",
                pthread_self(),
                host,
                port,
                so)));

return_rv:
	return rv;

close_so:
	/*< Close newly created socket. */
	if (close(so) != 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error: Failed to "
			"close socket %d due %d, %s.",
			so,
			errno,
			strerror(errno))));
	}
	goto return_rv;
}

/*
 * Write function for backend DCB. Store command to protocol.
 *
 * @param dcb	The DCB of the backend
 * @param queue	Queue of buffers to write
 * @return	0 on failure, 1 on success
 */
static int
plain_write_backend(DCB *dcb, GWBUF *queue)
{
	PlainProtocol *backend_protocol = dcb->protocol;
        int rc = 0; 

	rc = dcb_write(dcb, queue);
 
	return rc;
}

/**
 * Error event handler.
 * Create error message, pass it to router's error handler and if error 
 * handler fails in providing enough backend servers, mark session being 
 * closed and call DCB close function which triggers closing router session 
 * and related backends (if any exists.
 */
static int plain_error_backend_event(DCB *dcb)
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
		char	buf[100];

		len = sizeof(error);
		
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0)
			{
				strerror_r(error, buf, 100);
				LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
						"DCB in state %s got error '%s'.",
						STRDCBSTATE(dcb->state),
						buf)));
			}
		}
                return 1;
        }
       
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
		char	buf[100];

		len = sizeof(error);
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0)
			{
				strerror_r(error, buf, 100);
				LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
						"Error '%s' in session that is not ready for routing.",
						buf)));
			}
		}		
                gwbuf_free(errbuf);
                goto retblock;
        }
        
#if defined(SS_DEBUG)                
        LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "Backend error event handling.")));
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
        ss_dassert(dcb->dcb_errhandle_called);
        dcb_close(dcb);
        
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
static int plain_create_backend_connection(
        DCB     *backend_dcb,
        SERVER  *server,
        SESSION *session)
{
        PlainProtocol *protocol = NULL;
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

        rv = plain_do_connect_to_backend(server->name, server->port, &fd);
        /*< Assign protocol with backend_dcb */
        backend_dcb->protocol = protocol;

        /*< Set protocol state */
	switch (rv) {
		case 0:
                        ss_dassert(fd > 0);
                        protocol->fd = fd;

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
plain_backend_hangup(DCB *dcb)
{
        SESSION*        session;
        void*           rsession;
        ROUTER_OBJECT*  router;
        ROUTER*         router_instance;
        bool            succp;
        GWBUF*          errbuf;
        session_state_t ses_state;
        
        CHK_DCB(dcb);
        session = dcb->session;
        CHK_SESSION(session);
        
        rsession = session->router_session;
        router = session->service->router;
        router_instance = session->service->router_instance;        
    
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
		char	buf[100];

		len = sizeof(error);
		if (getsockopt(dcb->fd, SOL_SOCKET, SO_ERROR, &error, (socklen_t *)&len) == 0)
		{
			if (error != 0)
			{
				strerror_r(error, buf, 100);
				LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
						"Hangup in session that is not ready for routing, "
						"Error reported is '%s'.",
						buf)));
			}
		}
                gwbuf_free(errbuf);
                goto retblock;
        }
#if defined(SS_DEBUG)
        LOGIF(LE, (skygw_log_write_flush(
                LOGFILE_ERROR,
                "Backend hangup error handling.")));
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
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Backend hangup -> closing session.")));
#endif
                spinlock_acquire(&session->ses_lock);
                session->state = SESSION_STATE_STOPPING;
                spinlock_release(&session->ses_lock);
        }
        ss_dassert(dcb->dcb_errhandle_called);
        dcb_close(dcb);
        
retblock:
        return 1;
}

/**
 * Send COM_QUIT to backend so that it can be closed. 
 * @param dcb The current Backend DCB
 * @return 1 always
 */
static int
plain_backend_close(DCB *dcb)
{
        DCB*     client_dcb;
        SESSION* session;
        GWBUF*   quitbuf;
        
        CHK_DCB(dcb);
        session = dcb->session;
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
        if (session != NULL && 
		session->state == SESSION_STATE_STOPPING &&
		session->client != NULL)
        {		
                if (session->client->state == DCB_STATE_POLLING)
                {
			spinlock_release(&session->ses_lock);
			
                        /** Close client DCB */
                        dcb_close(session->client);
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
                
		rc = dcb_write(dcb, localq);
        }

        if (rc == 0)
        {
#if defined(SS_DEBUG)                
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Backend write delayqueue error handling.")));
#endif

	}
        return rc;
}
