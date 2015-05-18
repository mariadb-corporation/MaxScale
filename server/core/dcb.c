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

/**
 * @file dcb.c  -  Descriptor Control Block generic functions
 *
 * Descriptor control blocks provide the key mechanism for the interface
 * with the non-blocking socket polling routines. The descriptor control
 * block is the user data that is handled by the epoll system and contains
 * the state data and pointers to other components that relate to the
 * use of a file descriptor.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 12/06/13	Mark Riddoch		Initial implementation
 * 21/06/13	Massimiliano Pinto	free_dcb is used
 * 25/06/13	Massimiliano Pinto	Added checks to session and router_session
 * 28/06/13	Mark Riddoch		Changed the free mechanism to
 * 					introduce a zombie state for the
 * 					dcb
 * 02/07/2013	Massimiliano Pinto	Addition of delayqlock, delayq and
 *                                      authlock for handling backend
 *                                      asynchronous protocol connection
 *					and a generic lock for backend
 *                                      authentication
 * 16/07/2013	Massimiliano Pinto	Added command type for dcb
 * 23/07/2013	Mark Riddoch		Tidy up logging
 * 02/09/2013	Massimiliano Pinto	Added session refcount
 * 27/09/2013	Massimiliano Pinto	dcb_read returns 0 if ioctl returns no
 *                                      error and 0 bytes to read.
 *					This fixes a bug with many reads from
 *                                      backend
 * 07/05/2014	Mark Riddoch		Addition of callback mechanism
 * 20/06/2014	Mark Riddoch		Addition of dcb_clone
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <dcb.h>
#include <spinlock.h>
#include <server.h>
#include <session.h>
#include <service.h>
#include <modules.h>
#include <router.h>
#include <errno.h>
#include <gw.h>
#include <poll.h>
#include <atomic.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <hashtable.h>
#include <hk_heartbeat.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	DCB		*allDCBs = NULL;	/* Diagnostics need a list of DCBs */
static	DCB		*zombies = NULL;
static	SPINLOCK	dcbspin = SPINLOCK_INIT;
static	SPINLOCK	zombiespin = SPINLOCK_INIT;

static void dcb_final_free(DCB *dcb);
static bool dcb_set_state_nomutex(
        DCB*              dcb,
        const dcb_state_t new_state,
        dcb_state_t*      old_state);
static void dcb_call_callback(DCB *dcb, DCB_REASON reason);
static DCB* dcb_get_next (DCB* dcb);
static int  dcb_null_write(DCB *dcb, GWBUF *buf);
static int  dcb_null_close(DCB *dcb);
static int  dcb_null_auth(DCB *dcb, SERVER *server, SESSION *session, GWBUF *buf);
static int  dcb_isvalid_nolock(DCB *dcb);

size_t dcb_get_session_id(
	DCB* dcb)
{
	size_t rval;
	
	if (dcb != NULL && dcb->session != NULL)
	{
		rval = dcb->session->ses_id;
	}
	else
	{
		rval = 0;
	}
	return rval;
}

/**
 * Read log info from session through DCB and store values to memory locations
 * passed as parameters.
 * 
 * @param dcb		DCB
 * @param sesid		location where session id is to be copied
 * @param enabled_logs	bit field indicating which log types are enabled for the
 * session
 *
 *@return true if call arguments included memory addresses, false if any of the 
 *parameters was NULL.
 */ 
bool dcb_get_ses_log_info(
	DCB*    dcb,
	size_t* sesid,
	int*    enabled_logs)
{
	bool succp;
	
	if (dcb == NULL || 
		dcb->session == NULL || 
		sesid == NULL || 
		enabled_logs == NULL)
	{
		succp = false;
	}
	else
	{
		*sesid = dcb->session->ses_id;
		*enabled_logs = dcb->session->ses_enabled_logs;
		succp = true;
	}
	
	return succp;
}

/**
 * Return the pointer to the list of zombie DCB's
 *
 * @return Zombies DCB list
 */
DCB *
dcb_get_zombies(void)
{
        return zombies;
}

/**
 * Allocate a new DCB. 
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated DCB.
 *
 * @param dcb_role_t	The role for the new DCB
 * @return A newly allocated DCB or NULL if non could be allocated.
 */
DCB *
dcb_alloc(dcb_role_t role)
{
DCB	*rval;

	if ((rval = calloc(1, sizeof(DCB))) == NULL)
	{
		return NULL;
	}
#if defined(SS_DEBUG)
        rval->dcb_chk_top = CHK_NUM_DCB;
        rval->dcb_chk_tail = CHK_NUM_DCB;
#endif
	rval->dcb_errhandle_called = false;
        rval->dcb_role = role;
        spinlock_init(&rval->dcb_initlock);
	spinlock_init(&rval->writeqlock);
	spinlock_init(&rval->delayqlock);
	spinlock_init(&rval->authlock);
	spinlock_init(&rval->cb_lock);
	spinlock_init(&rval->pollinlock);
	spinlock_init(&rval->polloutlock);
	rval->pollinbusy = 0;
	rval->readcheck = 0;
	rval->polloutbusy = 0;
	rval->writecheck = 0;
        rval->fd = DCBFD_CLOSED;

	rval->evq.next = NULL;
	rval->evq.prev = NULL;
	rval->evq.pending_events = 0;
	rval->evq.processing = 0;
	spinlock_init(&rval->evq.eventqlock);

	memset(&rval->stats, 0, sizeof(DCBSTATS));	// Zero the statistics
	rval->state = DCB_STATE_ALLOC;
	bitmask_init(&rval->memdata.bitmask);
	rval->writeqlen = 0;
	rval->high_water = 0;
	rval->low_water = 0;
	rval->next = NULL;
	rval->callbacks = NULL;
	rval->data = NULL;

	rval->remote = NULL;
	rval->user = NULL;
	rval->flags = 0;

	spinlock_acquire(&dcbspin);
	if (allDCBs == NULL)
		allDCBs = rval;
	else
	{
		DCB *ptr = allDCBs;
		while (ptr->next)
			ptr = ptr->next;
		ptr->next = rval;
	}
	spinlock_release(&dcbspin);
	return rval;
}


/**
 * Free a DCB that has not been associated with a descriptor.
 *
 * @param dcb	The DCB to free
 */
void
dcb_free(DCB *dcb)
{
	if (dcb->fd == DCBFD_CLOSED)
	{
		dcb_final_free(dcb);
	}
	else
	{
		LOGIF(LE, (skygw_log_write_flush(
               		LOGFILE_ERROR,
			"Error : Attempt to free a DCB via dcb_free "
			"that has been associated with a descriptor.")));
	}
}

/** 
 * Add the DCB to the end of zombies list. 
 *
 * Adding to list occurs once per DCB. This is ensured by changing the
 * state of DCB to DCB_STATE_ZOMBIE after addition. Prior insertion, DCB state
 * is checked and operation proceeds only if state differs from DCB_STATE_ZOMBIE.
 * @param dcb The DCB to add to the zombie list
 * @return none
 */
void
dcb_add_to_zombieslist(DCB *dcb)
{
        bool        succp = false;
        dcb_state_t prev_state = DCB_STATE_UNDEFINED;
        
        CHK_DCB(dcb);        

        /*<
         * Protect zombies list access.
         */
	spinlock_acquire(&zombiespin);
        /*<
         * If dcb is already added to zombies list, return.
         */
        if (dcb->state != DCB_STATE_NOPOLLING) {
                ss_dassert(dcb->state != DCB_STATE_POLLING &&
                           dcb->state != DCB_STATE_LISTENING);
                spinlock_release(&zombiespin);
                return;
        }
        /*<
         * Add closing dcb to the top of the list.
         */
        dcb->memdata.next = zombies;
        zombies = dcb;
        /*<
         * Set state which indicates that it has been added to zombies
         * list.
         */
        succp = dcb_set_state(dcb, DCB_STATE_ZOMBIE, &prev_state);
        ss_info_dassert(succp, "Failed to set DCB_STATE_ZOMBIE");
        
	spinlock_release(&zombiespin);
}

/*
 * Clone a DCB for internal use, mostly used for specialist filters
 * to create dummy clients based on real clients.
 *
 * @param orig		The DCB to clone
 * @return 		A DCB that can be used as a client
 */
DCB *
dcb_clone(DCB *orig)
{
DCB	*clone;

	if ((clone = dcb_alloc(DCB_ROLE_REQUEST_HANDLER)) == NULL)
	{
		return NULL;
	}

	clone->fd = DCBFD_CLOSED;
	clone->flags |= DCBF_CLONE;
	clone->state = orig->state;
	clone->data = orig->data;
	if (orig->remote)
		clone->remote = strdup(orig->remote);
	if (orig->user)
		clone->user = strdup(orig->user);
	clone->protocol = orig->protocol;

	clone->func.write = dcb_null_write;
	/** 
	 * Close triggers closing of router session as well which is needed. 
	 */
	clone->func.close = orig->func.close;
	clone->func.auth = dcb_null_auth;

	return clone;
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * NB This is called with the caller holding the zombie queue
 * spinlock
 *
 * @param dcb The DCB to free
 */
static void
dcb_final_free(DCB *dcb)
{
DCB_CALLBACK		*cb;

        CHK_DCB(dcb);
        ss_info_dassert(dcb->state == DCB_STATE_DISCONNECTED || 
                        dcb->state == DCB_STATE_ALLOC,
                        "dcb not in DCB_STATE_DISCONNECTED not in DCB_STATE_ALLOC state.");

	if (DCB_POLL_BUSY(dcb))
	{
		/* Check if DCB has outstanding poll events */
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"dcb_final_free: DCB %p has outstanding events",
			dcb)));
	}

	/*< First remove this DCB from the chain */
	spinlock_acquire(&dcbspin);
	if (allDCBs == dcb)
	{
		/*<
		 * Deal with the special case of removing the DCB at the head of
		 * the chain.
		 */
		allDCBs = dcb->next;
	}
	else
	{
		/*<
		 * We find the DCB that point to the one we are removing and then
		 * set the next pointer of that DCB to the next pointer of the
		 * DCB we are removing.
		 */
		DCB *ptr = allDCBs;
		while (ptr && ptr->next != dcb)
			ptr = ptr->next;
		if (ptr)
			ptr->next = dcb->next;
	}
	spinlock_release(&dcbspin);

        if (dcb->session) {
                /*<
                 * Terminate client session.
                 */
                {
                        SESSION *local_session = dcb->session;
			dcb->session = NULL;
                        CHK_SESSION(local_session);
                        /** 
			 * Set session's client pointer NULL so that other threads
			 * won't try to call dcb_close for client DCB
			 * after this call.
			 */
                        if (local_session->client == dcb)
			{
				spinlock_acquire(&local_session->ses_lock);
				local_session->client = NULL;
				spinlock_release(&local_session->ses_lock);
			}
			session_free(local_session);
		}
	}

	if (dcb->protocol && (!DCB_IS_CLONE(dcb)))
		free(dcb->protocol);	
	if (dcb->remote)
		free(dcb->remote);
	if (dcb->user)
		free(dcb->user);

	/* Clear write and read buffers */	
	if (dcb->delayq) {
		GWBUF *queue = dcb->delayq;
		while ((queue = gwbuf_consume(queue, GWBUF_LENGTH(queue))) != NULL);
	}
	if (dcb->dcb_readqueue)
        {
                GWBUF* queue = dcb->dcb_readqueue;
                while ((queue = gwbuf_consume(queue, GWBUF_LENGTH(queue))) != NULL);
        }

	spinlock_acquire(&dcb->cb_lock);
	while ((cb = dcb->callbacks) != NULL)
	{
		dcb->callbacks = cb->next;
		free(cb);
	}
	spinlock_release(&dcb->cb_lock);

	bitmask_free(&dcb->memdata.bitmask);
	free(dcb);
}

/**
 * Process the DCB zombie queue
 *
 * This routine is called by each of the polling threads with
 * the thread id of the polling thread. It must clear the bit in
 * the memdata bitmask for the polling thread that calls it. If the
 * operation of clearing this bit means that no bits are set in
 * the memdata.bitmask then the DCB is no longer able to be 
 * referenced and it can be finally removed.
 *
 * @param	threadid	The thread ID of the caller
 */
DCB *
dcb_process_zombies(int threadid)
{
DCB	*ptr, *lptr;
DCB*    dcb_list = NULL;
DCB*    dcb = NULL;
bool    succp = false;

	/**
	 * Perform a dirty read to see if there is anything in the queue.
	 * This avoids threads hitting the queue spinlock when the queue 
	 * is empty. This will really help when the only entry is being
	 * freed, since the queue is updated before the expensive call to
	 * dcb_final_free.
	 */
	if (!zombies)
		return NULL;

	/*
	 * Process the zombie queue and create a list of DCB's that can be
	 * finally freed. This processing is down under a spinlock that
	 * will prevent new entries being added to the zombie queue. Therefore
	 * we do not want to do any expensive operations under this spinlock
	 * as it will block other threads. The expensive operations will be
	 * performed on the victim queue within holding the zombie queue
	 * spinlock.
	 */
	spinlock_acquire(&zombiespin);
	ptr = zombies;
	lptr = NULL;
	while (ptr)
	{
		CHK_DCB(ptr);

		/*
		 * Skip processing of DCB's that are
		 * in the event queue waiting to be processed.
		 */
		if (ptr->evq.next || ptr->evq.prev)
		{
			lptr = ptr;
			ptr = ptr->memdata.next;
		}
		else
		{

			bitmask_clear(&ptr->memdata.bitmask, threadid);
			
			if (bitmask_isallclear(&ptr->memdata.bitmask))
			{
				/**
				 * Remove the DCB from the zombie queue
				 * and call the final free routine for the
				 * DCB
				 *
				 * ptr is the DCB we are processing
				 * lptr is the previous DCB on the zombie queue
				 * or NULL if the DCB is at the head of the
				 * queue tptr is the DCB after the one we are
				 * processing on the zombie queue
				 */
				DCB	*tptr = ptr->memdata.next;
				if (lptr == NULL)
					zombies = tptr;
				else
					lptr->memdata.next = tptr;
				
				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"%lu [dcb_process_zombies] Remove dcb "
					"%p fd %d " "in state %s from the "
					"list of zombies.",
					pthread_self(),
					ptr,
					ptr->fd,
					STRDCBSTATE(ptr->state)))); 
				ss_info_dassert(ptr->state == DCB_STATE_ZOMBIE,
						"dcb not in DCB_STATE_ZOMBIE state.");
				/*<
				 * Move dcb to linked list of victim dcbs.
				 */
				if (dcb_list == NULL) {
					dcb_list = ptr;
					dcb = dcb_list;
				} else {
					dcb->memdata.next = ptr;
					dcb = dcb->memdata.next;
				}
				dcb->memdata.next = NULL;
				ptr = tptr;
			}
			else
			{
				lptr = ptr;
				ptr = ptr->memdata.next;
			}
		}
	}
	spinlock_release(&zombiespin);

	/*
	 * Process the victim queue. These are DCBs that are not in
	 * use by any thread. 
	 * The corresponding file descriptor is closed, the DCB marked
	 * as disconnected and the DCB itself is fianlly freed.
	 */
        dcb = dcb_list;
        while (dcb != NULL) {
		DCB* dcb_next = NULL;
                int  rc = 0;

		if (dcb->fd > 0)
		{
			/*<
			* Close file descriptor and move to clean-up phase.
			*/
			rc = close(dcb->fd);

			if (rc < 0) 
			{
				int eno = errno;
				errno = 0;
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Failed to close "
					"socket %d on dcb %p due error %d, %s.",
					dcb->fd,
					dcb,
					eno,
					strerror(eno))));
			}  
			else 
			{
				dcb->fd = DCBFD_CLOSED;
				
				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"%lu [dcb_process_zombies] Closed socket "
					"%d on dcb %p.",
					pthread_self(),
					dcb->fd,
					dcb)));
#if defined(FAKE_CODE)
				conn_open[dcb->fd] = false;
#endif /* FAKE_CODE */
			}
		}
		LOGIF_MAYBE(LT, (dcb_get_ses_log_info(
			dcb, 
			&tls_log_info.li_sesid, 
			&tls_log_info.li_enabled_logs)));

                succp = dcb_set_state(dcb, DCB_STATE_DISCONNECTED, NULL);
                ss_dassert(succp);
		dcb_next = dcb->memdata.next;
                dcb_final_free(dcb);
                dcb = dcb_next;
        }
        /** Reset threads session data */
        LOGIF(LT, tls_log_info.li_sesid = 0);
	
        return zombies;
}

/**
 * Connect to a server
 * 
 * This routine will create a server connection
 * If succesful the new dcb will be put in
 * epoll set by dcb->func.connect
 *
 * @param server	The server to connect to
 * @param session	The session this connection is being made for
 * @param protocol	The protocol module to use
 * @return		The new allocated dcb or NULL if the DCB was not connected
 */
DCB *
dcb_connect(SERVER *server, SESSION *session, const char *protocol)
{
DCB		*dcb;
GWPROTOCOL	*funcs;
int             fd;
int             rc;

	if ((dcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER)) == NULL)
	{
		return NULL;
	}
        
	if ((funcs = (GWPROTOCOL *)load_module(protocol,
                                               MODULE_PROTOCOL)) == NULL)
	{
                dcb_set_state(dcb, DCB_STATE_DISCONNECTED, NULL);
		dcb_final_free(dcb);
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
			"Error : Failed to load protocol module for %s, free "
                        "dcb %p\n",
                        protocol,
                        dcb)));
		return NULL;
	}
	memcpy(&(dcb->func), funcs, sizeof(GWPROTOCOL));

        /**
         * Link dcb to session. Unlink is called in dcb_final_free
         */
	if (!session_link_dcb(session, dcb))
	{
		LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
			"%lu [dcb_connect] Failed to link to session, the "
                        "session has been removed.",
                        pthread_self())));
		dcb_final_free(dcb);
		return NULL;
	}
        fd = dcb->func.connect(dcb, server, session);

        if (fd == DCBFD_CLOSED) {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [dcb_connect] Failed to connect to server %s:%d, "
                        "from backend dcb %p, client dcp %p fd %d.",
                        pthread_self(),
                        server->name,
                        server->port,
                        dcb,
                        session->client,
                        session->client->fd)));
                dcb_set_state(dcb, DCB_STATE_DISCONNECTED, NULL);
                dcb_final_free(dcb);
                return NULL;
	} else {
                LOGIF(LD, (skygw_log_write_flush(
                        LOGFILE_DEBUG,
                        "%lu [dcb_connect] Connected to server %s:%d, "
                        "from backend dcb %p, client dcp %p fd %d.",
                        pthread_self(),
                        server->name,
                        server->port,
                        dcb,
                        session->client,
                        session->client->fd)));
        }
        ss_dassert(dcb->fd == DCBFD_CLOSED); /*< must be uninitialized at this point */
        /**
         * Successfully connected to backend. Assign file descriptor to dcb
         */
        dcb->fd = fd;

	/**
	 * Add server pointer to dcb
	 */
        dcb->server = server;

        /** Copy status field to DCB */
        dcb->dcb_server_status = server->status;
        ss_debug(dcb->dcb_port = server->port;)
        
	/**
	 * backend_dcb is connected to backend server, and once backend_dcb
         * is added to poll set, authentication takes place as part of 
	 * EPOLLOUT event that will be received once the connection
	 * is established.
	 */
        
        /**
         * Add the dcb in the poll set
         */
        rc = poll_add_dcb(dcb);

        if (rc == DCBFD_CLOSED) {
                dcb_set_state(dcb, DCB_STATE_DISCONNECTED, NULL);
                dcb_final_free(dcb);
                return NULL;
        }
	/**
	 * The dcb will be addded into poll set by dcb->func.connect
	 */
	atomic_add(&server->stats.n_connections, 1);
	atomic_add(&server->stats.n_current, 1);

	return dcb;
}


/**
 * General purpose read routine to read data from a socket in the
 * Descriptor Control Block and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL
 *
 * @param dcb	The DCB to read from
 * @param head	Pointer to linked list to append data to
 * @return	-1 on error, otherwise the number of read bytes on the last 
 * iteration of while loop. 0 is returned if no data available.
 */
int dcb_read(
        DCB   *dcb, 
        GWBUF **head)
{
        GWBUF *buffer = NULL;
        int   b;
        int   rc;
        int   n;
        int   nread = 0;
        
        CHK_DCB(dcb);

	if (dcb->fd <= 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : Read failed, dcb is %s.",
			dcb->fd == DCBFD_CLOSED ? "closed" : "cloned, not readable")));
		n = 0;
		goto return_n;
	}

	while (true)
        {
                int bufsize;
                
                rc = ioctl(dcb->fd, FIONREAD, &b);
                
                if (rc == -1) 
                {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : ioctl FIONREAD for dcb %p in "
                                "state %s fd %d failed due error %d, %s.",
                                dcb,
                                STRDCBSTATE(dcb->state),
                                dcb->fd,
                                errno,
                                strerror(errno))));
                        n = -1;
                        goto return_n;
                }

                if (b == 0 && nread == 0)
                {                        
                        /** Handle closed client socket */
                        if (dcb_isclient(dcb)) 
                        {
                                char c;
                                int l_errno = 0;
                                int r = -1;
                                
                                /* try to read 1 byte, without consuming the socket buffer */
                                r = recv(dcb->fd, &c, sizeof(char), MSG_PEEK);
                                l_errno = errno;
                                
                                if (r <= 0 && 
                                        l_errno != EAGAIN && 
                                        l_errno != EWOULDBLOCK &&
					l_errno != 0) 
                                {
                                        n = -1;
                                        goto return_n;
                                }
                        }
                        n = 0;
                        goto return_n;
                }
                else if (b == 0)
                {
                        n = 0;
                        goto return_n;
                }

		dcb->last_read = hkheartbeat;

                bufsize = MIN(b, MAX_BUFFER_SIZE);
                
                if ((buffer = gwbuf_alloc(bufsize)) == NULL)
                {
                        /*<
                        * This is a fatal error which should cause shutdown.
                        * Todo shutdown if memory allocation fails.
                        */
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Failed to allocate read buffer "
                                "for dcb %p fd %d, due %d, %s.",
                                dcb,
                                dcb->fd, 
                                errno,
                                strerror(errno))));
                        
                        n = -1;
                        goto return_n;
                }
                GW_NOINTR_CALL(n = read(dcb->fd, GWBUF_DATA(buffer), bufsize);
                dcb->stats.n_reads++);
                
                if (n <= 0)
                {                        
                        if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK) 
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Read failed, dcb %p in state "
                                        "%s fd %d, due %d, %s.",
                                        dcb,
                                        STRDCBSTATE(dcb->state),
                                        dcb->fd, 
                                        errno,
                                        strerror(errno))));
                        }
			gwbuf_free(buffer);
                        goto return_n;
                }
                nread += n;
                
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [dcb_read] Read %d bytes from dcb %p in state %s "
                        "fd %d.", 
                        pthread_self(),
                        n,
                        dcb,
                        STRDCBSTATE(dcb->state),
                        dcb->fd)));
                /*< Append read data to the gwbuf */
                *head = gwbuf_append(*head, buffer);
        } /*< while (true) */
return_n:
        return n;
}

/**
 * General purpose routine to write to a DCB
 *
 * @param dcb	The DCB of the client
 * @param queue	Queue of buffers to write
 */
int
dcb_write(DCB *dcb, GWBUF *queue)
{
int	w;
int	saved_errno = 0;
int	below_water;

	below_water = (dcb->high_water && dcb->writeqlen < dcb->high_water) ? 1 : 0;
        ss_dassert(queue != NULL);

	if (dcb->fd <= 0)
	{
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : Write failed, dcb is %s.",
			dcb->fd == DCBFD_CLOSED ? "closed" : "cloned, not writable")));
		return 0;
	}
        /**
         * SESSION_STATE_STOPPING means that one of the backends is closing 
         * the router session. Some backends may have not completed 
         * authentication yet and thus they have no information about router
         * being closed. Session state is changed to SESSION_STATE_STOPPING
         * before router's closeSession is called and that tells that DCB may 
         * still be writable.
         */
        if (queue == NULL ||
            (dcb->state != DCB_STATE_ALLOC &&
             dcb->state != DCB_STATE_POLLING &&
             dcb->state != DCB_STATE_LISTENING &&
             dcb->state != DCB_STATE_NOPOLLING &&
             (dcb->session == NULL ||
             dcb->session->state != SESSION_STATE_STOPPING)))
        {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [dcb_write] Write aborted to dcb %p because "
                        "it is in state %s",
                        pthread_self(),
                        dcb->stats.n_buffered,
                        dcb,
                        STRDCBSTATE(dcb->state),
                        dcb->fd)));
                //ss_dassert(false);
                return 0;
        }
                
        spinlock_acquire(&dcb->writeqlock);
        
	if (dcb->writeq != NULL)
	{
		/*
		 * We have some queued data, so add our data to
		 * the write queue and return.
		 * The assumption is that there will be an EPOLLOUT
		 * event to drain what is already queued. We are protected
		 * by the spinlock, which will also be acquired by the
		 * the routine that drains the queue data, so we should
		 * not have a race condition on the event.
		 */
		if (queue)
                {
                        int qlen;
                        
                        qlen = gwbuf_length(queue);
                        atomic_add(&dcb->writeqlen, qlen);
                        dcb->writeq = gwbuf_append(dcb->writeq, queue);
                        dcb->stats.n_buffered++;
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [dcb_write] Append to writequeue. %d writes "
                                "buffered for dcb %p in state %s fd %d",
                                pthread_self(),
                                dcb->stats.n_buffered,
                                dcb,
                                STRDCBSTATE(dcb->state),
                                dcb->fd)));
                }
	}
	else
	{
		/*
		 * Loop over the buffer chain that has been passed to us
		 * from the reading side.
		 * Send as much of the data in that chain as possible and
		 * add any balance to the write queue.
		 */
		while (queue != NULL)
		{
                        int qlen;
#if defined(FAKE_CODE)
                        if (dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER &&
                            dcb->session != NULL)
                        {
                                if (dcb_isclient(dcb) && fail_next_client_fd) {
                                        dcb_fake_write_errno[dcb->fd] = 32;
                                        dcb_fake_write_ev[dcb->fd] = 29;
                                        fail_next_client_fd = false;
                                } else if (!dcb_isclient(dcb) &&
                                           fail_next_backend_fd)
                                {
                                        dcb_fake_write_errno[dcb->fd] = 32;
                                        dcb_fake_write_ev[dcb->fd] = 29;
                                        fail_next_backend_fd = false;
                                }
                        }
#endif /* FAKE_CODE */
			qlen = GWBUF_LENGTH(queue);
			GW_NOINTR_CALL(
                                w = gw_write(dcb, GWBUF_DATA(queue), qlen);
                                dcb->stats.n_writes++;
                                );
                        
			if (w < 0)
			{
                                saved_errno = errno;
                                errno = 0;

                                if (LOG_IS_ENABLED(LOGFILE_DEBUG))
                                {
                                        if (saved_errno == EPIPE) 
                                        {
                                                LOGIF(LD, (skygw_log_write(
                                                        LOGFILE_DEBUG,
                                                        "%lu [dcb_write] Write to dcb "
                                                        "%p in state %s fd %d failed "
                                                        "due errno %d, %s",
                                                        pthread_self(),
                                                        dcb,
                                                        STRDCBSTATE(dcb->state),
                                                        dcb->fd,
                                                        saved_errno,
                                                        strerror(saved_errno))));
                                        } 
                                }
                                
                                if (LOG_IS_ENABLED(LOGFILE_ERROR))
                                {
                                        if (saved_errno != EPIPE &&
                                                saved_errno != EAGAIN &&
                                                saved_errno != EWOULDBLOCK)
                                        {
                                                LOGIF(LE, (skygw_log_write_flush(
                                                        LOGFILE_ERROR,
                                                        "Error : Write to dcb %p in "
                                                        "state %s fd %d failed due "
                                                        "errno %d, %s",
                                                        dcb,
                                                        STRDCBSTATE(dcb->state),
                                                        dcb->fd,
                                                        saved_errno,
                                                        strerror(saved_errno))));
                                        }
                                }
				break;
			}
			/*
			 * Pull the number of bytes we have written from
			 * queue with have.
			 */
			queue = gwbuf_consume(queue, w);
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [dcb_write] Wrote %d Bytes to dcb %p in "
                                "state %s fd %d",
                                pthread_self(),
                                w,
                                dcb,
                                 STRDCBSTATE(dcb->state),
                                dcb->fd)));
		} /*< while (queue != NULL) */
                /*<
                 * What wasn't successfully written is stored to write queue
                 * for suspended write.
                 */
                dcb->writeq = queue;

                if (queue)
		{
                        int qlen;
                        
			qlen = gwbuf_length(queue);
                        atomic_add(&dcb->writeqlen, qlen);
                        dcb->stats.n_buffered++;
                }
	} /* if (dcb->writeq) */

	if (saved_errno != 0           &&
            queue != NULL              &&
            saved_errno != EAGAIN      &&
            saved_errno != EWOULDBLOCK)
	{
                bool dolog = true;

                /**
                 * Do not log if writing COM_QUIT to backend failed.
                 */
                if (GWBUF_IS_TYPE_MYSQL(queue))
                {
                        uint8_t* data = GWBUF_DATA(queue);
                        
                        if (data[4] == 0x01)
                        {
                                dolog = false;
                        }
                }
                if (dolog)
                {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [dcb_write] Writing to %s socket failed due %d, %s.",
                                pthread_self(),
                                dcb_isclient(dcb) ? "client" : "backend server",
                                saved_errno,
                                strerror(saved_errno))));
                }
		spinlock_release(&dcb->writeqlock);
		return 0;
	}
	spinlock_release(&dcb->writeqlock);

	if (dcb->high_water && dcb->writeqlen > dcb->high_water && below_water)
	{
		atomic_add(&dcb->stats.n_high_water, 1);
		dcb_call_callback(dcb, DCB_REASON_HIGH_WATER);
	}

	return 1;
}

/**
 * Drain the write queue of a DCB. This is called as part of the EPOLLOUT handling
 * of a socket and will try to send any buffered data from the write queue
 * up until the point the write would block.
 *
 * @param dcb	DCB to drain the write queue of
 * @return The number of bytes written
 */
int
dcb_drain_writeq(DCB *dcb)
{
int	n = 0;
int	w;
int	saved_errno = 0;
int	above_water;

	above_water = (dcb->low_water && dcb->writeqlen > dcb->low_water) ? 1 : 0;

	spinlock_acquire(&dcb->writeqlock);

        if (dcb->writeq)
	{
		int	len;
		/*
		 * Loop over the buffer chain in the pending writeq
		 * Send as much of the data in that chain as possible and
		 * leave any balance on the write queue.
		 */
		while (dcb->writeq != NULL)
		{
			len = GWBUF_LENGTH(dcb->writeq);
			GW_NOINTR_CALL(w = gw_write(dcb, GWBUF_DATA(dcb->writeq), len););
			saved_errno = errno;
                        errno = 0;
                        
			if (w < 0)
			{
#if defined(SS_DEBUG)
                                if (saved_errno == EAGAIN ||
                                    saved_errno == EWOULDBLOCK)
#else
                                if (saved_errno == EAGAIN ||
                                    saved_errno == EWOULDBLOCK ||
                                    saved_errno == EPIPE)
#endif
                                {
                                        break;
                                }
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Write to dcb %p "
                                        "in state %s fd %d failed due errno %d, %s",
                                        dcb,
                                        STRDCBSTATE(dcb->state),
                                        dcb->fd,
                                        saved_errno,
                                        strerror(saved_errno))));
                                break;
			}
			/*
			 * Pull the number of bytes we have written from
			 * queue with have.
			 */
			dcb->writeq = gwbuf_consume(dcb->writeq, w);
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [dcb_drain_writeq] Wrote %d Bytes to dcb %p "
                                "in state %s fd %d",
                                pthread_self(),
                                w,
                                dcb,
                                STRDCBSTATE(dcb->state),
                                dcb->fd)));
			n += w;
		}
	}
	spinlock_release(&dcb->writeqlock);
	atomic_add(&dcb->writeqlen, -n);
	
        /* The write queue has drained, potentially need to call a callback function */
	if (dcb->writeq == NULL)
		dcb_call_callback(dcb, DCB_REASON_DRAINED);

        if (above_water && dcb->writeqlen < dcb->low_water)
	{
		atomic_add(&dcb->stats.n_low_water, 1);
		dcb_call_callback(dcb, DCB_REASON_LOW_WATER);
	}

	return n;
}

/** 
 * Removes dcb from poll set, and adds it to zombies list. As a consequense,
 * dcb first moves to DCB_STATE_NOPOLLING, and then to DCB_STATE_ZOMBIE state.
 * At the end of the function state may not be DCB_STATE_ZOMBIE because once
 * dcb_initlock is released parallel threads may change the state.
 *
 * Parameters:
 * @param dcb The DCB to close
 *
 *
 */
void
dcb_close(DCB *dcb)
{
        int  rc = 0;

        CHK_DCB(dcb);

	LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
				"%lu [dcb_close]",
				pthread_self())));                                
	
        /**
         * dcb_close may be called for freshly created dcb, in which case
         * it only needs to be freed.
         */
        if (dcb->state == DCB_STATE_ALLOC)
        {
                dcb_set_state(dcb, DCB_STATE_DISCONNECTED, NULL);
                dcb_final_free(dcb);
                return;
        }
        
        ss_dassert(dcb->state == DCB_STATE_POLLING ||
               dcb->state == DCB_STATE_NOPOLLING ||
               dcb->state == DCB_STATE_ZOMBIE);
        
        /*<
        * Stop dcb's listening and modify state accordingly.
        */
	if (dcb->state == DCB_STATE_POLLING)
	{
		rc = poll_remove_dcb(dcb);

		if (rc == 0) {
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [dcb_close] Removed dcb %p in state %s from "
				"poll set.",
				pthread_self(),
				dcb,
				STRDCBSTATE(dcb->state))));
		} else {
			LOGIF(LE, (skygw_log_write(
				LOGFILE_ERROR,
				"Error : Removing DCB fd == %d in state %s from "
				"poll set failed.",
				dcb->fd,
				STRDCBSTATE(dcb->state))));
		}
	
		if (rc == 0)
		{
			/**
			 * close protocol and router session
			 */
			if (dcb->func.close != NULL)
			{
				dcb->func.close(dcb);
			}
			/** Call possible callback for this DCB in case of close */
			dcb_call_callback(dcb, DCB_REASON_CLOSE);
			
			if (dcb->state == DCB_STATE_NOPOLLING) 
			{
				dcb_add_to_zombieslist(dcb);
			}
		}
	        ss_dassert(dcb->state == DCB_STATE_NOPOLLING ||
					dcb->state == DCB_STATE_ZOMBIE);	
	}
}

/**
 * Diagnostic to print a DCB
 *
 * @param dcb	The DCB to print
 *
 */
void
printDCB(DCB *dcb)
{
	printf("DCB: %p\n", (void *)dcb);
	printf("\tDCB state: 		%s\n", gw_dcb_state2string(dcb->state));
	if (dcb->remote)
		printf("\tConnected to:		%s\n", dcb->remote);
	if (dcb->user)
		printf("\tUsername to:		%s\n", dcb->user);
	if (dcb->writeq)
		printf("\tQueued write data:	%d\n",gwbuf_length(dcb->writeq));
	printf("\tStatistics:\n");
	printf("\t\tNo. of Reads: 			%d\n",
				dcb->stats.n_reads);
	printf("\t\tNo. of Writes:			%d\n",
				dcb->stats.n_writes);
	printf("\t\tNo. of Buffered Writes:		%d\n",
				dcb->stats.n_buffered);
	printf("\t\tNo. of Accepts:			%d\n",
				dcb->stats.n_accepts);
	printf("\t\tNo. of High Water Events:	%d\n",
				dcb->stats.n_high_water);
	printf("\t\tNo. of Low Water Events:	%d\n",
				dcb->stats.n_low_water);
}
/**
 * Display an entry from the spinlock statistics data
 *
 * @param       dcb     The DCB to print to
 * @param       desc    Description of the statistic
 * @param       value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
	dcb_printf((DCB *)dcb, "\t\t%-40s  %d\n", desc, value);
}


/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void printAllDCBs()
{
DCB	*dcb;

	spinlock_acquire(&dcbspin);
	dcb = allDCBs;
	while (dcb)
	{
		printDCB(dcb);
		dcb = dcb->next;
	}
	spinlock_release(&dcbspin);
}


/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void dprintAllDCBs(DCB *pdcb)
{
DCB	*dcb;

	spinlock_acquire(&dcbspin);
#if SPINLOCK_PROFILE
	dcb_printf(pdcb, "DCB List Spinlock Statistics:\n");
	spinlock_stats(&dcbspin, spin_reporter, pdcb);
	dcb_printf(pdcb, "Zombie Queue Lock Statistics:\n");
	spinlock_stats(&zombiespin, spin_reporter, pdcb);
#endif
	dcb = allDCBs;
	while (dcb)
	{
		dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
		dcb_printf(pdcb, "\tDCB state:          %s\n",
					gw_dcb_state2string(dcb->state));
		if (dcb->session && dcb->session->service)
			dcb_printf(pdcb, "\tService:            %s\n",
					dcb->session->service->name);
		if (dcb->remote)
			dcb_printf(pdcb, "\tConnected to:       %s\n",
					dcb->remote);
		if (dcb->user)
			dcb_printf(pdcb, "\tUsername:           %s\n",
					dcb->user);
		if (dcb->writeq)
			dcb_printf(pdcb, "\tQueued write data:  %d\n",
					gwbuf_length(dcb->writeq));
		dcb_printf(pdcb, "\tStatistics:\n");
		dcb_printf(pdcb, "\t\tNo. of Reads:           	%d\n", dcb->stats.n_reads);
		dcb_printf(pdcb, "\t\tNo. of Writes:          	%d\n", dcb->stats.n_writes);
		dcb_printf(pdcb, "\t\tNo. of Buffered Writes: 	%d\n", dcb->stats.n_buffered);
		dcb_printf(pdcb, "\t\tNo. of Accepts:         	%d\n", dcb->stats.n_accepts);
		dcb_printf(pdcb, "\t\tNo. of High Water Events:	%d\n", dcb->stats.n_high_water);
		dcb_printf(pdcb, "\t\tNo. of Low Water Events:	%d\n", dcb->stats.n_low_water);
		if (dcb->flags & DCBF_CLONE)
			dcb_printf(pdcb, "\t\tDCB is a clone.\n");
		dcb = dcb->next;
	}
	spinlock_release(&dcbspin);
}

/** 
 * Diagnotic routine to print DCB data in a tabular form.
 * 
 * @param       pdcb    DCB to print results to
 */
void
dListDCBs(DCB *pdcb)
{
DCB     *dcb;

	spinlock_acquire(&dcbspin);
	dcb = allDCBs;
	dcb_printf(pdcb, "Descriptor Control Blocks\n");
	dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
	dcb_printf(pdcb, " %-16s | %-26s | %-18s | %s\n", 
			"DCB", "State", "Service", "Remote");
	dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n");
	while (dcb)
	{
		dcb_printf(pdcb, " %-16p | %-26s | %-18s | %s\n",
			dcb, gw_dcb_state2string(dcb->state),
			
			((dcb->session && dcb->session->service) ? dcb->session->service->name : ""), 
			(dcb->remote ? dcb->remote : ""));
		dcb = dcb->next;
	}
	dcb_printf(pdcb, "------------------+----------------------------+--------------------+----------\n\n");
	spinlock_release(&dcbspin);
}

/** 
 * Diagnotic routine to print client DCB data in a tabular form.
 * 
 * @param       pdcb    DCB to print results to
 */
void
dListClients(DCB *pdcb)
{
DCB     *dcb;

	spinlock_acquire(&dcbspin);
	dcb = allDCBs;
	dcb_printf(pdcb, "Client Connections\n");
	dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
	dcb_printf(pdcb, " %-15s | %-16s | %-20s | %s\n", 
			"Client", "DCB", "Service", "Session");
	dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n");
	while (dcb)
	{
		if (dcb_isclient(dcb)
			&& dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER)
		{
			dcb_printf(pdcb, " %-15s | %16p | %-20s | %10p\n",
				(dcb->remote ? dcb->remote : ""),
				dcb, (dcb->session->service ?
					dcb->session->service->name : ""), 
				dcb->session);
		}
		dcb = dcb->next;
	}
	dcb_printf(pdcb, "-----------------+------------------+----------------------+------------\n\n");
	spinlock_release(&dcbspin);
}


/**
 * Diagnostic to print a DCB to another DCB
 *
 * @param pdcb	The DCB to which send the output
 * @param dcb	The DCB to print
 */
void
dprintDCB(DCB *pdcb, DCB *dcb)
{
	dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
	dcb_printf(pdcb, "\tDCB state: 		%s\n", gw_dcb_state2string(dcb->state));
	if (dcb->session && dcb->session->service)
		dcb_printf(pdcb, "\tService:		%s\n",
					dcb->session->service->name);
	if (dcb->remote)
		dcb_printf(pdcb, "\tConnected to:		%s\n", dcb->remote);
	if (dcb->user)
		dcb_printf(pdcb, "\tUsername:			%s\n",
					dcb->user);
	dcb_printf(pdcb, "\tOwning Session:   	%p\n", dcb->session);
	if (dcb->writeq)
		dcb_printf(pdcb, "\tQueued write data:	%d\n", gwbuf_length(dcb->writeq));
	if (dcb->delayq)
		dcb_printf(pdcb, "\tDelayed write data:	%d\n", gwbuf_length(dcb->delayq));
	dcb_printf(pdcb, "\tStatistics:\n");
	dcb_printf(pdcb, "\t\tNo. of Reads: 			%d\n",
						dcb->stats.n_reads);
	dcb_printf(pdcb, "\t\tNo. of Writes:			%d\n",
						dcb->stats.n_writes);
	dcb_printf(pdcb, "\t\tNo. of Buffered Writes:		%d\n",
						dcb->stats.n_buffered);
	dcb_printf(pdcb, "\t\tNo. of Accepts:			%d\n",
						dcb->stats.n_accepts);
	dcb_printf(pdcb, "\t\tNo. of High Water Events:	%d\n",
						dcb->stats.n_high_water);
	dcb_printf(pdcb, "\t\tNo. of Low Water Events:	%d\n",
						dcb->stats.n_low_water);
	if (DCB_POLL_BUSY(dcb))
	{
		dcb_printf(pdcb, "\t\tPending events in the queue:	%x %s\n",
			dcb->evq.pending_events, dcb->evq.processing ? "(processing)" : "");
		
	}
	if (dcb->flags & DCBF_CLONE)
		dcb_printf(pdcb, "\t\tDCB is a clone.\n");
#if SPINLOCK_PROFILE
	dcb_printf(pdcb, "\tInitlock Statistics:\n");
	spinlock_stats(&dcb->dcb_initlock, spin_reporter, pdcb);
	dcb_printf(pdcb, "\tWrite Queue Lock Statistics:\n");
	spinlock_stats(&dcb->writeqlock, spin_reporter, pdcb);
	dcb_printf(pdcb, "\tDelay Queue Lock Statistics:\n");
	spinlock_stats(&dcb->delayqlock, spin_reporter, pdcb);
	dcb_printf(pdcb, "\tPollin Lock Statistics:\n");
	spinlock_stats(&dcb->pollinlock, spin_reporter, pdcb);
	dcb_printf(pdcb, "\tPollout Lock Statistics:\n");
	spinlock_stats(&dcb->polloutlock, spin_reporter, pdcb);
	dcb_printf(pdcb, "\tCallback Lock Statistics:\n");
	spinlock_stats(&dcb->cb_lock, spin_reporter, pdcb);
#endif
}

/**
 * Return a string representation of a DCB state.
 *
 * @param state	The DCB state
 * @return String representation of the state
 *
 */
const char *
gw_dcb_state2string (int state) {
	switch(state) {
		case DCB_STATE_ALLOC:
			return "DCB Allocated";
		case DCB_STATE_POLLING:
			return "DCB in the polling loop";
		case DCB_STATE_LISTENING:
			return "DCB for listening socket";
		case DCB_STATE_DISCONNECTED:
			return "DCB socket closed";
		case DCB_STATE_FREED:
			return "DCB memory could be freed";
		case DCB_STATE_ZOMBIE:
			return "DCB Zombie";
		default:
			return "DCB (unknown)";
	}
}

/**
 * A  DCB based wrapper for printf. Allows formatting printing to
 * a descritor control block.
 *
 * @param dcb	Descriptor to write to
 * @param fmt	A printf format string
 * @param ...	Variable arguments for the print format
 */
void
dcb_printf(DCB *dcb, const char *fmt, ...)
{
GWBUF	*buf;
va_list	args;

	if ((buf = gwbuf_alloc(10240)) == NULL)
		return;
	va_start(args, fmt);
	vsnprintf(GWBUF_DATA(buf), 10240, fmt, args);
	va_end(args);

	buf->end = GWBUF_DATA(buf) + strlen(GWBUF_DATA(buf));
	dcb->func.write(dcb, buf);
}

/**
 * Determine the role that a DCB plays within a session.
 *
 * @param dcb
 * @return Non-zero if the DCB is the client of the session
 */
int
dcb_isclient(DCB *dcb)
{
	if (dcb->state != DCB_STATE_LISTENING && dcb->session)
	{
		if (dcb->session->client)
		{
			return (dcb->session && dcb == dcb->session->client);
		}
	}

        return 0;
}

/**
 * Print hash table statistics to a DCB
 *
 * @param dcb		The DCB to send the information to
 * @param table		The hash table
 */
void dcb_hashtable_stats(
        DCB*  dcb,
        void* table)
{
        int total;
        int longest;
        int hashsize;

        total = 0;
	longest = 0;

        hashtable_get_stats(table, &hashsize, &total, &longest);

        dcb_printf(dcb,
                   "Hashtable: %p, size %d\n",
                   table,
                   hashsize);
        
	dcb_printf(dcb, "\tNo. of entries:     	%d\n", total);
	dcb_printf(dcb, 
		"\tAverage chain length:	%.1f\n", 
		(hashsize == 0 ? (float)hashsize : (float)total / hashsize));
	dcb_printf(dcb, "\tLongest chain length:	%d\n", longest);
}


bool dcb_set_state(
        DCB*              dcb,
        const dcb_state_t new_state,
        dcb_state_t*      old_state)
{
        bool              succp;
        dcb_state_t       state ;
        
        CHK_DCB(dcb);
        spinlock_acquire(&dcb->dcb_initlock);
        succp = dcb_set_state_nomutex(dcb, new_state, &state);
        
        spinlock_release(&dcb->dcb_initlock);

        if (old_state != NULL) {
                *old_state = state;
        }
        return succp;
}

static bool dcb_set_state_nomutex(
        DCB*              dcb,
        const dcb_state_t new_state,
        dcb_state_t*      old_state)
{
        bool        succp = false;
        dcb_state_t state = DCB_STATE_UNDEFINED;
        
        CHK_DCB(dcb);

        state = dcb->state;
        
        if (old_state != NULL) {
                *old_state = state;
        }
        
        switch (state) {
        case DCB_STATE_UNDEFINED:
                dcb->state = new_state;
                succp = true;
                break;

        case DCB_STATE_ALLOC:
                switch (new_state) {
			/** fall through, for client requests */
			case DCB_STATE_POLLING: 
			/** fall through, for connect listeners */
			case DCB_STATE_LISTENING: 
			/** for failed connections */
			case DCB_STATE_DISCONNECTED: 
				dcb->state = new_state;
				succp = true;
				break;
			default:                        
				ss_dassert(old_state != NULL);
				break;
                }
                break;
                
        case DCB_STATE_POLLING:
                switch(new_state) {
			case DCB_STATE_NOPOLLING:
				dcb->state = new_state;
				succp = true;
				break;
			default:
				ss_dassert(old_state != NULL);
				break;
                }
                break;

        case DCB_STATE_LISTENING:
                switch(new_state) {
			case DCB_STATE_NOPOLLING:
				dcb->state = new_state;
				succp = true;
				break;
			default:
				ss_dassert(old_state != NULL);
				break;
                }
                break;
                
        case DCB_STATE_NOPOLLING:
                switch (new_state) {
			case DCB_STATE_ZOMBIE: /*< fall through */
				dcb->state = new_state;
			case DCB_STATE_POLLING: /*< ok to try but state can't change */
				succp = true;
				break;
			default:
				ss_dassert(old_state != NULL);
				break;
                }
                break;

        case DCB_STATE_ZOMBIE:
                switch (new_state) {
			case DCB_STATE_DISCONNECTED: /*< fall through */
				dcb->state = new_state;
			case DCB_STATE_POLLING: /*< ok to try but state can't change */
				succp = true;
				break;
			default:
				ss_dassert(old_state != NULL);
				break;
                }
                break;

        case DCB_STATE_DISCONNECTED:
                switch (new_state) {
			case DCB_STATE_FREED:
				dcb->state = new_state;
				succp = true;
				break;
			default:
				ss_dassert(old_state != NULL);
				break;
                }
                break;

        case DCB_STATE_FREED:
                ss_dassert(old_state != NULL);
                break;
                
        default:
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Unknown dcb state %s for "
                        "dcb %p",
                        STRDCBSTATE(dcb->state),
                        dcb)));
                ss_dassert(false);
                break;
        } /*< switch (dcb->state) */

        if (succp) {
                LOGIF(LD, (skygw_log_write_flush(
                        LOGFILE_DEBUG,
                        "%lu [dcb_set_state_nomutex] dcb %p fd %d %s -> %s",
                        pthread_self(),
                        dcb,
                        dcb->fd,
                        STRDCBSTATE(state),
                        STRDCBSTATE(dcb->state))));
        }
        else
        {
                LOGIF(LD, (skygw_log_write(
                                   LOGFILE_DEBUG,
                                   "%lu [dcb_set_state_nomutex] Failed "
                                   "to change state of DCB %p. "
                                   "Old state %s > new state %s.",
                                   pthread_self(),
                                   dcb,
				   (old_state == NULL ? "NULL" : STRDCBSTATE(*old_state)),
                                   STRDCBSTATE(new_state))));
        }
        return succp;
}

/**
 * Write data to a DCB
 *
 * @param dcb		The DCB to write buffer
 * @param buf		Buffer to write
 * @param nbytes	Number of bytes to write
 * @return Number of written bytes
 */
int
gw_write(DCB *dcb, const void *buf, size_t nbytes)
{
        int w = 0;
	int fd = dcb->fd;
#if defined(FAKE_CODE)                
        if (fd > 0 && dcb_fake_write_errno[fd] != 0) 
	{
                ss_dassert(dcb_fake_write_ev[fd] != 0);
                w = write(fd, buf, nbytes/2); /*< leave peer to read missing bytes */

                if (w > 0) {
                        w = -1;
                        errno = dcb_fake_write_errno[fd];
                }
        } else if (fd > 0)
	{
                w = write(fd, buf, nbytes);
        }
#else
	if (fd > 0)
	{
		w = write(fd, buf, nbytes);
	}
#endif /* FAKE_CODE */

#if defined(SS_DEBUG_MYSQL)
        {
                size_t   len;
                uint8_t* packet = (uint8_t *)buf;
                char*    str;
                
                /** Print only MySQL packets */
                if (w > 5)
                {
                        str = (char *)&packet[5];
                        len      = packet[0];
                        len     += 256*packet[1];
                        len     += 256*256*packet[2];
                                                
                        if (strncmp(str, "insert", 6) == 0 ||
                                strncmp(str, "create", 6) == 0 ||
                                strncmp(str, "drop", 4) == 0)
                        {
                                ss_dassert((dcb->dcb_server_status & (SERVER_RUNNING|SERVER_MASTER|SERVER_SLAVE))==(SERVER_RUNNING|SERVER_MASTER));
                        }
                        
                        if (strncmp(str, "set autocommit", 14) == 0 && nbytes > 17)
                        {
                                char* s = (char *)calloc(1, nbytes+1);
                                
                                if (nbytes-5 > len)
                                {
                                        size_t len2 = packet[4+len];
                                        len2 += 256*packet[4+len+1];
                                        len2 += 256*256*packet[4+len+2];
                                        
                                        char* str2 = (char *)&packet[4+len+5];
                                        snprintf(s, 5+len+len2, "long %s %s", (char *)str, (char *)str2);
                                }
                                else
                                {
                                        snprintf(s, len, "%s", (char *)str);
                                }
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "%lu [gw_write] Wrote %d bytes : %s ",
                                        pthread_self(),
                                        w,
                                        s)));
                                free(s);
                        }
                }
        }
#endif
        return w;
}

/**
 * Add a callback
 *
 * Duplicate registrations are not allowed, therefore an error will be
 * returned if the specific function, reason and userdata triple
 * are already registered.
 * An error will also be returned if the is insufficient memeory available to
 * create the registration.
 *
 * @param dcb		The DCB to add the callback to
 * @param reason	The callback reason
 * @param callback	The callback function to call
 * @param userdata	User data to send in the call
 * @return		Non-zero (true) if the callback was added
 */
int
dcb_add_callback(DCB *dcb, DCB_REASON reason, int (*callback)(struct dcb *, DCB_REASON, void *), void *userdata)
{
DCB_CALLBACK	*cb, *ptr;
int		rval = 1;

	if ((ptr = (DCB_CALLBACK *)malloc(sizeof(DCB_CALLBACK))) == NULL)
	{
		return 0;
	}
	ptr->reason = reason;
	ptr->cb = callback;
	ptr->userdata = userdata;
	ptr->next = NULL;
	spinlock_acquire(&dcb->cb_lock);
	cb = dcb->callbacks;
	if (cb == NULL)
	{
		dcb->callbacks = ptr;
		spinlock_release(&dcb->cb_lock);
	}
	else
	{
		while (cb)
		{
			if (cb->reason == reason && cb->cb == callback &&
				cb->userdata == userdata)
			{
				free(ptr);
				spinlock_release(&dcb->cb_lock);
				return 0;
			}
			if (cb->next == NULL)
			{
				cb->next = ptr;
				break;
			}
			cb = cb->next;
		}
		spinlock_release(&dcb->cb_lock);
	}
	return rval;
}

/**
 * Remove a callback from the callback list for the DCB
 *
 * Searches down the linked list to find the callback with a matching reason, function
 * and userdata.
 *
 * @param dcb		The DCB to add the callback to
 * @param reason	The callback reason
 * @param callback	The callback function to call
 * @param userdata	User data to send in the call
 * @return		Non-zero (true) if the callback was removed
 */
int
dcb_remove_callback(DCB *dcb, DCB_REASON reason, int (*callback)(struct dcb *, DCB_REASON, void *), void *userdata)
{
DCB_CALLBACK	*cb, *pcb = NULL;
int		rval = 0;

	spinlock_acquire(&dcb->cb_lock);
	cb = dcb->callbacks;
	if (cb == NULL)
	{
		rval = 0;
	}
	else
	{
		while (cb)
		{
			if (cb->reason == reason && cb->cb == callback
				&& cb->userdata == userdata)
			{
				if (pcb != NULL)
					pcb->next = cb->next;
				else
					dcb->callbacks = cb->next;
				spinlock_release(&dcb->cb_lock);
				free(cb);
				rval = 1;
				break;
			}
			pcb = cb;
			cb = cb->next;
		}
	}
	if (!rval)
		spinlock_release(&dcb->cb_lock);
	return rval;
}

/**
 * Call the set of callbacks registered for a particular reason.
 *
 * @param dcb		The DCB to call the callbacks regarding
 * @param reason	The reason that has triggered the call
 */
static void
dcb_call_callback(DCB *dcb, DCB_REASON reason)
{
DCB_CALLBACK	*cb, *nextcb;

	spinlock_acquire(&dcb->cb_lock);
	cb = dcb->callbacks;
	while (cb)
	{
		if (cb->reason == reason)
		{
			nextcb = cb->next;
			spinlock_release(&dcb->cb_lock);
			
			LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
					"%lu [dcb_call_callback] %s",
					pthread_self(),
					STRDCBREASON(reason))));
			
			cb->cb(dcb, reason, cb->userdata);
			spinlock_acquire(&dcb->cb_lock);
			cb = nextcb;
		}
		else
			cb = cb->next;
	}
	spinlock_release(&dcb->cb_lock);
}

/**
 * Check the passed DCB to ensure it is in the list of allDCBS
 *
 * @param	dcb	The DCB to check
 * @return	1 if the DCB is in the list, otherwise 0
 */
int
dcb_isvalid(DCB *dcb)
{
int	rval = 0;

    if (dcb)
    {
	spinlock_acquire(&dcbspin);
        rval = dcb_isvalid_nolock(dcb);
	spinlock_release(&dcbspin);
    }

    return rval;
}


/**
 * Check the passed DCB to ensure it is in the list of allDCBS.
 * Requires that the DCB list is already locked before call.
 *
 * @param	dcb	The DCB to check
 * @return	1 if the DCB is in the list, otherwise 0
 */
static int
dcb_isvalid_nolock(DCB *dcb)
{
DCB	*ptr;
int	rval = 0;

    if (dcb)
    {
	ptr = allDCBs;
	while (ptr && ptr != dcb)
	{
		ptr = ptr->next;
	}
        rval = (ptr == dcb);
    }
    return rval;
}


/**
 * Get the next DCB in the list of all DCB's
 *
 * @param dcb		The current DCB
 * @return	The pointer to the next DCB or NULL if this is the last
 */
static DCB *
dcb_get_next (DCB* dcb)
{
        spinlock_acquire(&dcbspin);
        if (dcb) {
            dcb = dcb_isvalid_nolock(dcb) ? dcb->next : NULL;
        }
        else dcb = allDCBs;
        spinlock_release(&dcbspin);
        
        return dcb;
}        

/**
 * Call all the callbacks on all DCB's that match the server and the reason given
 *
 * @param reason	The DCB_REASON that triggers the callback
 */
void
dcb_call_foreach(struct server* server, DCB_REASON reason)
{
	LOGIF(LD, (skygw_log_write(LOGFILE_DEBUG,
				"%lu [dcb_call_foreach]",
				pthread_self())));                                
	
        switch (reason) {
                case DCB_REASON_CLOSE:
                case DCB_REASON_DRAINED:
                case DCB_REASON_HIGH_WATER:
                case DCB_REASON_LOW_WATER:
                case DCB_REASON_ERROR:
                case DCB_REASON_HUP:
                case DCB_REASON_NOT_RESPONDING: 
                {
                        DCB* dcb;
                        dcb = dcb_get_next(NULL);
                        
                        while (dcb != NULL)
                        {
                                if (dcb->state == DCB_STATE_POLLING && dcb->server &&
				    strcmp(dcb->server->unique_name,server->unique_name) == 0)
                                {
                                        dcb_call_callback(dcb, DCB_REASON_NOT_RESPONDING);
                                }
                                dcb = dcb_get_next(dcb);
                        }
                        break;
                }
                        
                default:
                        break;
        }
        return;
}


/**
 * Null protocol write routine used for cloned dcb's. It merely consumes
 * buffers written on the cloned DCB and sets the DCB_REPLIED flag.
 *
 * @param dcb		The descriptor control block
 * @param buf		The buffer being written
 * @return	Always returns a good write operation result
 */
static int
dcb_null_write(DCB *dcb, GWBUF *buf)
{
	while (buf)
	{
		buf = gwbuf_consume(buf, GWBUF_LENGTH(buf));
	}
    
        dcb->flags |= DCBF_REPLIED;

	return 1;
}

/**
 * Null protocol close operation for use by cloned DCB's.
 *
 * @param dcb		The DCB being closed.
 */
static int
dcb_null_close(DCB *dcb)
{
	return 0;
}

/**
 * Null protocol auth operation for use by cloned DCB's.
 *
 * @param dcb		The DCB being closed.
 * @param server	The server to auth against
 * @param session	The user session
 * @param buf		The buffer with the new auth request
 */
static int
dcb_null_auth(DCB *dcb, SERVER *server, SESSION *session, GWBUF *buf)
{
	return 0;
}

/**
 * Return DCB counts optionally filtered by usage
 *
 * @param	usage	The usage of the DCB
 * @return	A count of DCBs in the desired state
 */
int
dcb_count_by_usage(DCB_USAGE usage)
{
int	rval = 0;
DCB	*ptr;

	spinlock_acquire(&dcbspin);
	ptr = allDCBs;
	while (ptr)
	{
		switch (usage)
		{
		case DCB_USAGE_CLIENT:
			if (dcb_isclient(ptr))
				rval++;
			break;
		case DCB_USAGE_LISTENER:
			if (ptr->state == DCB_STATE_LISTENING)
				rval++;
			break;
		case DCB_USAGE_BACKEND:
			if (dcb_isclient(ptr) == 0
					&& ptr->dcb_role == DCB_ROLE_REQUEST_HANDLER)
				rval++;
			break;
		case DCB_USAGE_INTERNAL:
			if (ptr->dcb_role == DCB_ROLE_REQUEST_HANDLER)
				rval++;
			break;
		case DCB_USAGE_ZOMBIE:
			if (DCB_ISZOMBIE(ptr))
				rval++;
			break;
		case DCB_USAGE_ALL:
			rval++;
			break;
		}
		ptr = ptr->next;
	}
	spinlock_release(&dcbspin);
	return rval;
}
