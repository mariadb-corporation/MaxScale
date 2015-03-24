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
 * Copyright MariaDB Corporation Ab 2013-2015
 */

/**
 * @file mongo_client.c
 *
 * Revision History
 * Date		Who			Description

 *
 */
#include <skygw_utils.h>
#include <log_manager.h>
#include <gw.h>
#include <modinfo.h>
#include <sys/stat.h>
#include <modutil.h>
#include <plainprotocol.h>

MODULE_INFO info = {
	MODULE_API_PROTOCOL,
	MODULE_GA,
	GWPROTOCOL_VERSION,
	"The plain client protocol"
};

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static char *version_str = "V1.0.0";

static int plain_accept(DCB *listener);
static int plain_listen(DCB *listener, char *config_bind);
static int plain_read(DCB* dcb);
static int plain_write_ready(DCB *dcb);
static int plain_write(DCB *dcb, GWBUF *queue);
static int plain_client_error(DCB *dcb);
static int plain_client_close(DCB *dcb);
static int plain_client_hangup_event(DCB *dcb);

/*
 * The "module object" for the mysqld client protocol module.
 */
static GWPROTOCOL MyObject = { 
	plain_read,			/* Read - EPOLLIN handler	 */
	plain_write,			/* Write - data from gateway	 */
	plain_write_ready,			/* WriteReady - EPOLLOUT handler */
	plain_client_error,			/* Error - EPOLLERR handler	 */
	plain_client_hangup_event,			/* HangUp - EPOLLHUP handler	 */
	plain_accept,				/* Accept			 */
	NULL,					/* Connect			 */
	plain_client_close,			/* Close			 */
	plain_listen,			/* Listen			 */
	NULL,					/* Authentication		 */
	NULL					/* Session			 */
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
}

/**
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
 * Write function for client DCB: writes data from MaxScale to Client
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
plain_write(DCB *dcb, GWBUF *queue)
{
 	return dcb_write(dcb, queue);
}

/**
 * Client read event triggered by EPOLLIN
 *
 * @param dcb	Descriptor control block
 * @return 0 if succeed, 1 otherwise
 */
int plain_read(
        DCB* dcb) 
{
	SESSION        *session = NULL;
	ROUTER_OBJECT  *router = NULL;
	ROUTER         *router_instance = NULL;
	void           *rsession = NULL;
	PlainProtocol  *protocol = NULL;
        GWBUF          *read_buffer = NULL;
        int             rc = 0;
        int             nbytes_read = 0;
        uint8_t         cap = 0;
        bool            stmt_input = false; /*< router input type */

        CHK_DCB(dcb);
        protocol = DCB_PROTOCOL(dcb, PlainProtocol);
        CHK_PROTOCOL(protocol);
        rc = dcb_read(dcb, &read_buffer);
        
        if (rc < 0)
        {
                dcb_close(dcb);
        }
        nbytes_read = gwbuf_length(read_buffer);
       
        if (nbytes_read == 0)
        {
                goto return_rc;
        }
	
	if(dcb->session == NULL)
	{
	    dcb->session = session_alloc(dcb->service,dcb);
	}

	rc = SESSION_ROUTE_QUERY(dcb->session, read_buffer);
    
        
return_rc:

	return rc;
}

///////////////////////////////////////////////
// client write event to Client triggered by EPOLLOUT
//////////////////////////////////////////////
/** 
 * @node Client's fd became writable, and EPOLLOUT event
 * arrived. As a consequence, client input buffer (writeq) is flushed. 
 *
 * Parameters:
 * @param dcb - in, use
 *          client dcb
 *
 * @return constantly 1
 *
 * 
 * @details (write detailed description here)
 *
 */
int plain_write_ready(DCB *dcb)
{
	PlainProtocol *protocol = NULL;

        CHK_DCB(dcb);

        ss_dassert(dcb->state != DCB_STATE_DISCONNECTED);
        
	if (dcb == NULL) {
		goto return_1;
	}

	if (dcb->state == DCB_STATE_DISCONNECTED) {
		goto return_1;
	}
        
	if (dcb->protocol == NULL) {
	        goto return_1;
	}
        protocol = (PlainProtocol *)dcb->protocol;


		dcb_drain_writeq(dcb);
                goto return_1;

return_1:

        return 1;
}

/**
 * set listener for mysql protocol, retur 1 on success and 0 in failure
 */
int plain_listen(
        DCB  *listen_dcb,
        char *config_bind)
{
	int l_so;
	int syseno = 0;
	struct sockaddr_in serv_addr;
	struct sockaddr_un local_addr;
	struct sockaddr *current_addr;
	int  one = 1;
        int  rc;

	if (strchr(config_bind, '/')) {
		char *tmp = strrchr(config_bind, ':');
		if (tmp)
			*tmp = '\0';

		// UNIX socket create
		if ((l_so = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
			fprintf(stderr,
				"\n* Error: can't create UNIX socket due "
				"error %i, %s.\n\n\t",
				errno,
				strerror(errno));
			return 0;
		}
		memset(&local_addr, 0, sizeof(local_addr));
		local_addr.sun_family = AF_UNIX;
		strncpy(local_addr.sun_path, config_bind, sizeof(local_addr.sun_path) - 1);

		current_addr = (struct sockaddr *) &local_addr;

	} else {
		/* MaxScale, as default, will bind on port 4406 */
		if (!parse_bindconfig(config_bind, 4406, &serv_addr)) {
			fprintf(stderr, "Error in parse_bindconfig for [%s]\n", config_bind);
			return 0;
		}
		// TCP socket create
		if ((l_so = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			fprintf(stderr,
				"\n* Error: can't create socket due "
				"error %i, %s.\n\n\t",
				errno,
				strerror(errno));
			return 0;
		}

		current_addr = (struct sockaddr *) &serv_addr;
	}

	listen_dcb->fd = -1;

	// socket options
	if((syseno = setsockopt(l_so, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one))) != 0){
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,"Error: Failed to set socket options. Error %d: %s",errno,strerror(errno))));
	}


	// set NONBLOCKING mode
	setnonblocking(l_so);

	/* get the right socket family for bind */
	switch (current_addr->sa_family) {
		case AF_UNIX:
			rc = unlink(config_bind);
			if ( (rc == -1) && (errno!=ENOENT) ) {
				fprintf(stderr, "Error unlink Unix Socket %s\n", config_bind);
			}

			if (bind(l_so, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
				fprintf(stderr,
					"\n* Bind failed due error %i, %s.\n",
					errno,
					strerror(errno));
				fprintf(stderr, "* Can't bind to %s\n\n", config_bind);
				close(l_so);
				return 0;
			}

			/* set permission for all users */
			if (chmod(config_bind, 0777) < 0) {
				fprintf(stderr,
					"\n* chmod failed for %s due error %i, %s.\n\n",
					config_bind,
					errno,
					strerror(errno));
			}

			break;

		case AF_INET:
			if (bind(l_so, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
				fprintf(stderr,
					"\n* Bind failed due error %i, %s.\n",
					errno,
					strerror(errno));
				fprintf(stderr, "* Can't bind to %s\n\n", config_bind);
				close(l_so);
				return 0;
			}
			break;

		default:
			fprintf(stderr, "* Socket Family %i not supported\n", current_addr->sa_family);
			close(l_so);
			return 0;
	}

        rc = listen(l_so, 10 * SOMAXCONN);

        if (rc == 0) {
		LOGIF(LM, (skygw_log_write_flush(LOGFILE_MESSAGE,"Listening MySQL connections at %s", config_bind)));
        } else {
                int eno = errno;
                errno = 0;
                fprintf(stderr,
                        "\n* Failed to start listening MySQL due error %d, %s\n\n",
                        eno,
                        strerror(eno));
		close(l_so);
                return 0;
        }
	// assign l_so to dcb
	listen_dcb->fd = l_so;

        // add listening socket to poll structure
        if (poll_add_dcb(listen_dcb) == -1) {
            fprintf(stderr,
                    "\n* Failed to start polling the socket due error "
                    "%i, %s.\n\n",
                    errno,
                    strerror(errno));
		return 0;
        }
#if defined(FAKE_CODE)
        conn_open[l_so] = true;
#endif /* FAKE_CODE */
	listen_dcb->func.accept = plain_accept;

	return 1;
}


/** 
 * @node (write brief function description here) 
 *
 * Parameters:
 * @param listener - <usage>
 *          <description>
 *
 * @return 0 in success, 1 in failure
 *
 * 
 * @details (write detailed description here)
 *
 */
int plain_accept(DCB *listener)
{
        int                rc = 0;
        DCB                *client_dcb;
        PlainProtocol      *protocol;
        int                c_sock;
	struct sockaddr    client_conn;
	socklen_t          client_len = sizeof(struct sockaddr_storage);
        int                sendbuf = GW_BACKEND_SO_SNDBUF;
        socklen_t          optlen = sizeof(sendbuf);
        int                eno = 0;
	int		   syseno = 0;
        int                i = 0;
                
        CHK_DCB(listener);
        
	while (1) {

    retry_accept:

                        // new connection from client
		        c_sock = accept(listener->fd,
                                        (struct sockaddr *) &client_conn,
                                        &client_len);
                        eno = errno;
                        errno = 0;
                        
                if (c_sock == -1) {
                        
                        if (eno == EAGAIN || eno == EWOULDBLOCK)
                        {
                                /**
                                 * We have processed all incoming connections.
                                 */
                                rc = 1;
                                goto return_rc;
                        }
                        else if (eno == ENFILE || eno == EMFILE)
                        {
				struct timespec ts1;
				ts1.tv_sec = 0;				
                                /**
                                 * Exceeded system's (ENFILE) or processes
                                 * (EMFILE) max. number of files limit.
                                 */
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [plain_accept] Error %d, %s. ",
                                        pthread_self(),
                                        eno,
                                        strerror(eno))));
                                
                                if (i == 0)
                                {
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error %d, %s. "
                                                "Failed to accept new client "
                                                "connection.",
                                                eno,
                                                strerror(eno))));
                                }
                                i++;
				ts1.tv_nsec = 100*i*i*1000000;
				nanosleep(&ts1, NULL);
				
                                if (i<10) {
                                        goto retry_accept;
                                }
                                rc = 1;
                                goto return_rc;
                        }
                        else
                        {
                                /**
                                 * Other error.
                                 */
                                LOGIF(LD, (skygw_log_write(
                                        LOGFILE_DEBUG,
                                        "%lu [plain_accept] Error %d, %s.",
                                        pthread_self(),
                                        eno,
                                        strerror(eno))));
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Failed to accept new client "
                                        "connection due to %d, %s.",
                                        eno,
                                        strerror(eno))));
                                rc = 1;
                                goto return_rc;
                        } /* if (eno == ..) */
		} /* if (c_sock == -1) */
                /* reset counter */
                i = 0;
                
                listener->stats.n_accepts++;
#if defined(SS_DEBUG)
                LOGIF(LD, (skygw_log_write_flush(
                        LOGFILE_DEBUG,
                        "%lu [plain_accept] Accepted fd %d.",
                        pthread_self(),
                        c_sock)));
#endif /* SS_DEBUG */

                /* set nonblocking  */
        	sendbuf = GW_CLIENT_SO_SNDBUF;

			if((syseno = setsockopt(c_sock, SOL_SOCKET, SO_SNDBUF, &sendbuf, optlen)) != 0){
				LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,"Error: Failed to set socket options. Error %d: %s",errno,strerror(errno))));
			}

        	sendbuf = GW_CLIENT_SO_RCVBUF;

			if((syseno = setsockopt(c_sock, SOL_SOCKET, SO_RCVBUF, &sendbuf, optlen)) != 0){
				LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,"Error: Failed to set socket options. Error %d: %s",errno,strerror(errno))));
			}
                setnonblocking(c_sock);
                
                client_dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);

		if (client_dcb == NULL) {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Failed to create "
				"DCB object for client connection.")));
			close(c_sock);
			rc = 1;
			goto return_rc;
		}

                client_dcb->service = listener->session->service;
                client_dcb->fd = c_sock;

		// get client address
		if ( client_conn.sa_family == AF_UNIX) 
                {
			// client address
			client_dcb->remote = strdup("localhost_from_socket");
			// set localhost IP for user authentication
  			(client_dcb->ipv4).sin_addr.s_addr = 0x0100007F;
		} 
		else 
                {
  			/* client IPv4 in raw data*/
			memcpy(&client_dcb->ipv4, 
                               (struct sockaddr_in *)&client_conn, 
                               sizeof(struct sockaddr_in));	
			/* client IPv4 in string representation */
			client_dcb->remote = (char *)calloc(INET_ADDRSTRLEN+1, sizeof(char));
                        
			if (client_dcb->remote != NULL) 
                        {
				inet_ntop(AF_INET, 
                                          &(client_dcb->ipv4).sin_addr, 
                                          client_dcb->remote,
                                          INET_ADDRSTRLEN);
			}
		}
                protocol = mysql_protocol_init(client_dcb, c_sock);
                ss_dassert(protocol != NULL);
                
                if (protocol == NULL) {
                        /** delete client_dcb */
                        dcb_close(client_dcb);
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "%lu [plain_accept] Failed to create "
                                "protocol object for client connection.",
                                pthread_self())));
                        rc = 1;
                        goto return_rc;
                }
                client_dcb->protocol = protocol;
                // assign function poiters to "func" field
                memcpy(&client_dcb->func, &MyObject, sizeof(GWPROTOCOL));
            
 
                /**
                 * Set new descriptor to event set. At the same time,
                 * change state to DCB_STATE_POLLING so that
                 * thread which wakes up sees correct state.
                 */
                if (poll_add_dcb(client_dcb) == -1)
                {
                        /* Send a custom error as MySQL command reply */

                        /** close client_dcb */
                        dcb_close(client_dcb);

                        /** Previous state is recovered in poll_add_dcb. */
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "%lu [plain_accept] Failed to add dcb %p for "
                                "fd %d to epoll set.",
                                pthread_self(),
                                client_dcb,
                                client_dcb->fd)));
                        rc = 1;
                        goto return_rc;
                }
                else
                {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [plain_accept] Added dcb %p for fd "
                                "%d to epoll set.",
                                pthread_self(),
                                client_dcb,
                                client_dcb->fd)));
                }
        } /**< while 1 */
#if defined(SS_DEBUG)
        if (rc == 0) {
                CHK_DCB(client_dcb);
                CHK_PROTOCOL(((PlainProtocol *)client_dcb->protocol));
        }
#endif
return_rc:
	
        return rc;
}

static int plain_client_error(
        DCB* dcb) 
{
        SESSION* session;

        CHK_DCB(dcb);
        
        session = dcb->session;
        
        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [plain_client_error] Error event handling for DCB %p "
                "in state %s, session %p.",
                pthread_self(),
                dcb,
                STRDCBSTATE(dcb->state),
                (session != NULL ? session : NULL))));
        
        if (session != NULL && session->state == SESSION_STATE_STOPPING)
        {
                goto retblock;
        }
        
        dcb_close(dcb);
        
retblock:
        return 1;
}

static int
plain_client_close(DCB *dcb)
{
        SESSION*       session;
        ROUTER_OBJECT* router;
        void*          router_instance;
#if defined(SS_DEBUG)
        PlainProtocol* protocol = (PlainProtocol *)dcb->protocol;
        if (dcb->state == DCB_STATE_POLLING ||
            dcb->state == DCB_STATE_NOPOLLING ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
		if (!DCB_IS_CLONE(dcb)) CHK_PROTOCOL(protocol);
        }
#endif
	LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
				"%lu [plain_client_close]",
				pthread_self())));                                
	mysql_protocol_done(dcb);
        session = dcb->session;
        /**
         * session may be NULL if session_alloc failed.
         * In that case, router session wasn't created.
         */
        if (session != NULL)
        {
                CHK_SESSION(session);
                spinlock_acquire(&session->ses_lock);
                
                if (session->state != SESSION_STATE_STOPPING)
                {
			session->state = SESSION_STATE_STOPPING;
		}
                router_instance = session->service->router_instance;		
		router = session->service->router;
		/**
		 * If router session is being created concurrently router 
		 * session might be NULL and it shouldn't be closed.
		 */
		if (session->router_session != NULL)
		{
			spinlock_release(&session->ses_lock);
			/** Close router session and all its connections */
			router->closeSession(router_instance, session->router_session);
		}
		else
		{
			spinlock_release(&session->ses_lock);
		}
        }
	return 1;
}

/**
 * Handle a hangup event on the client side descriptor.
 *
 * We simply close the DCB, this will propogate the closure to any
 * backend descriptors and perform the session cleanup.
 *
 * @param dcb		The DCB of the connection
 */
static int
plain_client_hangup_event(DCB *dcb)
{
        SESSION* session;

        CHK_DCB(dcb);
        session = dcb->session;
        
        if (session != NULL && session->state == SESSION_STATE_ROUTER_READY)
        {
                CHK_SESSION(session);
        }
        
        if (session != NULL && session->state == SESSION_STATE_STOPPING)
        {
                goto retblock;
        }

        dcb_close(dcb);
 
retblock:
        return 1;
}

