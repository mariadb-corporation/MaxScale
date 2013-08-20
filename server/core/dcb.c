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
 * 28/06/13	Mark Riddoch		Changed the free mechanism ti
 * 					introduce a zombie state for the
 * 					dcb
 * 02/07/2013	Massimiliano Pinto	Addition of delayqlock, delayq and authlock
 *					for handling backend asynchronous protocol connection
 *					and a generic lock for backend authentication
 * 16/07/2013	Massimiliano Pinto	Added command type for dcb
 * 23/07/13	Mark Riddoch		Tidy up logging
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

static	DCB		*allDCBs = NULL;	/* Diagnotics need a list of DCBs */
static	DCB		*zombies = NULL;
static	SPINLOCK	dcbspin = SPINLOCK_INIT;
static	SPINLOCK	zombiespin = SPINLOCK_INIT;

static void dcb_final_free(DCB *dcb);
/**
 * Allocate a new DCB. 
 *
 * This routine performs the generic initialisation on the DCB before returning
 * the newly allocated DCB.
 *
 * @return A newly allocated DCB or NULL if non could be allocated.
 */
DCB *
dcb_alloc()
{
DCB	*rval;

	if ((rval = malloc(sizeof(DCB))) == NULL)
	{
		return NULL;
	}
	spinlock_init(&rval->writeqlock);
	spinlock_init(&rval->delayqlock);
	spinlock_init(&rval->authlock);
	rval->writeq = NULL;
	rval->delayq = NULL;
	rval->remote = NULL;
	rval->state = DCB_STATE_ALLOC;
	rval->next = NULL;
	rval->data = NULL;
	rval->protocol = NULL;
	rval->session = NULL;
	memset(&rval->stats, 0, sizeof(DCBSTATS));	// Zero the statistics
	bitmask_init(&rval->memdata.bitmask);
	rval->memdata.next = NULL;
	rval->command = 0;

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
 * Free a DCB, this only marks the DCB as a zombie and adds it
 * to the zombie list. The real working of removing it occurs once
 * all the threads signal they no longer have access to the DCB
 *
 * @param dcb The DCB to free
 */
void
dcb_free(DCB *dcb)
{       
	if (dcb->state == DCB_STATE_ZOMBIE)
	{
		skygw_log_write(LOGFILE_ERROR,
                                "Call to free a DCB that is already a zombie.\n");
		return;
	}

	/* Set the bitmask of running pollng threads */
	bitmask_copy(&dcb->memdata.bitmask, poll_bitmask());

	/* Add the DCB to the Zombie list */
	spinlock_acquire(&zombiespin);
	if (zombies == NULL)
		zombies = dcb;
	else
	{
		DCB *ptr = zombies;
		while (ptr->memdata.next)
		{
			if (ptr == dcb)
			{
				skygw_log_write(
                                        LOGFILE_ERROR,
                                        "Attempt to add DCB to zombie queue "
					"when it is already in the queue");
				break;
			}
			ptr = ptr->memdata.next;
		}
		if (ptr != dcb)
			ptr->memdata.next = dcb;
	}
	spinlock_release(&zombiespin);

        skygw_log_write(
                LOGFILE_TRACE,
                "%lu [dcb_free] Set dcb %p for fd %d DCB_STATE_ZOMBIE",
                pthread_self(),
                (unsigned long)dcb,
                dcb->fd);
	dcb->state = DCB_STATE_ZOMBIE;
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
	dcb->state = DCB_STATE_FREED;

	/* First remove this DCB from the chain */
	spinlock_acquire(&dcbspin);
	if (allDCBs == dcb)
	{
		/*
		 * Deal with the special case of removing the DCB at the head of
		 * the chain.
		 */
		allDCBs = dcb->next;
	}
	else
	{
		/*
		 * We find the DCB that pont to the one we are removing and then
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

	if (dcb->protocol)
		free(dcb->protocol);
	if (dcb->data)
		free(dcb->data);
	if (dcb->remote)
		free(dcb->remote);
	bitmask_free(&dcb->memdata.bitmask);
	free(dcb);
}

/**
 * Process the DCB zombie queue
 *
 * This routine is called by each of the polling threads with
 * the thread id of the polling thread. It must clear the bit in
 * the memdata btmask for the polling thread that calls it. If the
 * operation of clearing this bit means that no bits are set in
 * the memdata.bitmask then the DCB is no longer able to be 
 * referenced and it can be finally removed.
 *
 * @param	threadid	The thread ID of the caller
 */
void
dcb_process_zombies(int threadid)
{
DCB	*ptr, *lptr;

	spinlock_acquire(&zombiespin);
	ptr = zombies;
	lptr = NULL;
	while (ptr)
	{                    
		bitmask_clear(&ptr->memdata.bitmask, threadid);
		if (bitmask_isallclear(&ptr->memdata.bitmask))
		{
			/*
			 * Remove the DCB from the zombie queue
			 * and call the final free routine for the
			 * DCB
			 *
			 * ptr is the DCB we are processing
			 * lptr is the previous DCB on the zombie queue
			 * or NULL if the DCB is at the head of the queue
			 * tptr is the DCB after the one we are processing
			 * on the zombie queue
			 */
			DCB	*tptr = ptr->memdata.next;
			if (lptr == NULL)
				zombies = tptr;
			else
				lptr->memdata.next = tptr;
                        skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [dcb_process_zombies] Free dcb %p for fd %d",
                                pthread_self(),
                                (unsigned long)ptr,
                                ptr->fd);
			dcb_final_free(ptr);
			ptr = tptr;
		}
		else
		{
			lptr = ptr;
			ptr = ptr->memdata.next;
		}
	}
	spinlock_release(&zombiespin);
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
 * @return		The new allocated dcb
 */
DCB *
dcb_connect(SERVER *server, SESSION *session, const char *protocol)
{
DCB		*dcb;
GWPROTOCOL	*funcs;

	if ((dcb = dcb_alloc()) == NULL)
	{
		return NULL;
	}
	if ((funcs = (GWPROTOCOL *)load_module(protocol, MODULE_PROTOCOL)) == NULL)
	{
		dcb_final_free(dcb);
		skygw_log_write( LOGFILE_ERROR,
			"Failed to load protocol module for %s, free dcb %p\n", protocol, dcb);
		return NULL;
	}
	memcpy(&(dcb->func), funcs, sizeof(GWPROTOCOL));
	dcb->session = session;

	if ((dcb->fd = dcb->func.connect(dcb, server, session)) == -1)
	{
		dcb_final_free(dcb);
		skygw_log_write( LOGFILE_ERROR, "Failed to connect to server %s:%d, free dcb %p\n",
				server->name, server->port, dcb);
		return NULL;
	}

	/*
	 * The dcb will be addded into poll set by dcb->func.connect
	 */

	atomic_add(&server->stats.n_connections, 1);
	atomic_add(&server->stats.n_current, 1);

	/*
	 * We are now connected, the authentication etc will happen as
	 * part of the EPOLLOUT event that will be received once the connection
	 * is established.
	 */
	return dcb;
}


/**
 * General purpose read routine to read data from a socket in the
 * Descriptor Control Block and append it to a linked list of buffers.
 * The list may be empty, in which case *head == NULL
 *
 * @param dcb	The DCB to read from
 * @param head	Pointer to linked list to append data to
 * @return	The numebr of bytes read or -1 on fatal error
 */
int
dcb_read(DCB *dcb, GWBUF **head)
{
GWBUF 	  *buffer = NULL;
int 	  b, n = 0;
int       rc = 0;
int       eno = 0;

	rc = ioctl(dcb->fd, FIONREAD, &b);

        if (rc == -1) {
                eno = errno;
                errno = 0;
                skygw_log_write(
                        LOGFILE_ERROR,
                        "%lu [dcb_read] Setting FIONREAD for fd %d failed. "
                        "errno %d, %s. dcb->state = %d",
                        pthread_self(),
                        dcb->fd,
                        eno ,
                        strerror(eno),
                        dcb->state);
                return -1;
        }
        
	while (b > 0)
	{
		int bufsize = b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE;
		if ((buffer = gwbuf_alloc(bufsize)) == NULL)
		{
			return n ? n : -1;
		}

		GW_NOINTR_CALL(n = read(dcb->fd, GWBUF_DATA(buffer), bufsize); dcb->stats.n_reads++);

		if (n < 0)
		{
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
			{
				return n;
			}
			else
			{
				return n ? n : -1;
			}
		}
		else if (n == 0)
		{
			return n;
		}

                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [dcb_read] Read %d Bytes from fd %d",
                        pthread_self(),
                        n,
                        dcb->fd);
		// append read data to the gwbuf
		*head = gwbuf_append(*head, buffer);

		/* Re issue the ioctl as the amount of data buffered may have changed */
		rc = ioctl(dcb->fd, FIONREAD, &b);

                if (rc == -1) {
                        eno = errno;
                        errno = 0;
                        skygw_log_write(
                                LOGFILE_ERROR,
                                "%lu [dcb_read] Setting FIONREAD for fd %d failed. "
                                "errno %d, %s. dcb->state = %d",
                                pthread_self(),
                                dcb->fd,
                                eno ,
                                strerror(eno),
                                dcb->state);
                        return -1;
                }
	} /**< while (b>0) */

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
int	w, saved_errno = 0;

        spinlock_acquire(&dcb->writeqlock);
	if (dcb->writeq)
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
		dcb->writeq = gwbuf_append(dcb->writeq, queue);
		dcb->stats.n_buffered++;
                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [dcb_write] Append to writequeue. %d writes buffered for %d",
                        pthread_self(),
                        dcb->stats.n_buffered,
                        dcb->fd);
	}
	else
	{
		int	len;

		/*
		 * Loop over the buffer chain that has been passed to us
		 * from the reading side.
		 * Send as much of the data in that chain as possible and
		 * add any balance to the write queue.
		 */
		while (queue != NULL)
		{
			len = GWBUF_LENGTH(queue);
			GW_NOINTR_CALL(w = write(dcb->fd, GWBUF_DATA(queue), len); dcb->stats.n_writes++);
			saved_errno = errno;
			if (w < 0)
			{
                            skygw_log_write(
                                    LOGFILE_ERROR,
                                    "%lu [dcb_write] Write to fd %d failed, errno %d",
                                    pthread_self(),
                                    dcb->fd,
                                    saved_errno);
				break;
			}

			/*
			 * Pull the number of bytes we have written from
			 * queue with have.
			 */
			queue = gwbuf_consume(queue, w);
			if (w < len)
			{
				/* We didn't write all the data */
			}
                        skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [dcb_write] Wrote %d Bytes to fd %d",
                                pthread_self(),
                                w,
                                dcb->fd);
		}
		/* Buffer the balance of any data */
		dcb->writeq = queue;
		if (queue)
		{
			dcb->stats.n_buffered++;
		}
	}
	spinlock_release(&dcb->writeqlock);

	if (queue && (saved_errno != EAGAIN || saved_errno != EWOULDBLOCK))
	{
		/* We had a real write failure that we must deal with */
		return 0;
	}

	return 1;
}

/**
 * Drain the write queue of a DCB. THis is called as part of the EPOLLOUT handling
 * of a socket and will try to send any buffered data from the write queue
 * up until the point the write would block.
 *
 * @param dcb	DCB to drain the write queue of
 * @return The number of bytes written
 */
int
dcb_drain_writeq(DCB *dcb)
{
int n = 0;
int w;
int saved_errno = 0;

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
			GW_NOINTR_CALL(w = write(dcb->fd, GWBUF_DATA(dcb->writeq), len););
			saved_errno = errno;
			if (w < 0)
			{
                            skygw_log_write(
                                    LOGFILE_ERROR,
                                    "%lu [dcb_drain_writeq] Write to fd %d failed, "
                                    "errno %d",
                                    pthread_self(),
                                    dcb->fd,
                                    saved_errno);
                            break;
			}

			/*
			 * Pull the number of bytes we have written from
			 * queue with have.
			 */
			dcb->writeq = gwbuf_consume(dcb->writeq, w);
			if (w < len)
			{
				/* We didn't write all the data */
			}
                        skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [dcb_drain_writeq] Wrote %d Bytes to fd %d",
                                pthread_self(),
                                w,
                                dcb->fd);                                                
			n += w;
		}
	}
	spinlock_release(&dcb->writeqlock);
	return n;
}

/**
 * Close a DCB
 *
 * Generic, non-protocol specific close funcitonality
 * @param	dcb	The DCB to close
 */
void
dcb_close(DCB *dcb)
{
        /** protect state check and set */
        spinlock_acquire(&dcb->writeqlock);

        if (dcb->state == DCB_STATE_DISCONNECTED ||
            dcb->state == DCB_STATE_FREED ||
            dcb->state == DCB_STATE_ZOMBIE)
        {
                spinlock_release(&dcb->writeqlock);
                return;
        }
	poll_remove_dcb(dcb);
	close(dcb->fd);
	dcb->state = DCB_STATE_DISCONNECTED;
        spinlock_release(&dcb->writeqlock);
        
	if (dcb_isclient(dcb))
	{
		/*
		 * If the DCB we are closing is a client side DCB then shutdown
                 * the router session. This will close any backend connections.
		 */
		SERVICE *service = dcb->session->service;

		if (service && service->router && dcb->session->router_session)
		{
			service->router->closeSession(
                                service->router_instance,
                                dcb->session->router_session);
		}
		session_free(dcb->session);
	}
	dcb_free(dcb);
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
	printf("\tQueued write data:	%d\n", gwbuf_length(dcb->writeq));
	printf("\tStatistics:\n");
	printf("\t\tNo. of Reads: 	%d\n", dcb->stats.n_reads);
	printf("\t\tNo. of Writes:	%d\n", dcb->stats.n_writes);
	printf("\t\tNo. of Buffered Writes:	%d\n", dcb->stats.n_buffered);
	printf("\t\tNo. of Accepts: %d\n", dcb->stats.n_accepts);
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
	dcb = allDCBs;
	while (dcb)
	{
		dcb_printf(pdcb, "DCB: %p\n", (void *)dcb);
		dcb_printf(pdcb, "\tDCB state:          %s\n", gw_dcb_state2string(dcb->state));
		if (dcb->session && dcb->session->service)
			dcb_printf(pdcb, "\tService:            %s\n", dcb->session->service->name);
		if (dcb->remote)
			dcb_printf(pdcb, "\tConnected to:       %s\n", dcb->remote);
		dcb_printf(pdcb, "\tQueued write data:  %d\n", gwbuf_length(dcb->writeq));
		dcb_printf(pdcb, "\tStatistics:\n");
		dcb_printf(pdcb, "\t\tNo. of Reads:           %d\n", dcb->stats.n_reads);
		dcb_printf(pdcb, "\t\tNo. of Writes:          %d\n", dcb->stats.n_writes);
		dcb_printf(pdcb, "\t\tNo. of Buffered Writes: %d\n", dcb->stats.n_buffered);
		dcb_printf(pdcb, "\t\tNo. of Accepts:         %d\n", dcb->stats.n_accepts);
		dcb = dcb->next;
	}
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
	if (dcb->remote)
		dcb_printf(pdcb, "\tConnected to:		%s\n", dcb->remote);
	dcb_printf(pdcb, "\tOwning Session:   	%d\n", dcb->session);
	dcb_printf(pdcb, "\tQueued write data:	%d\n", gwbuf_length(dcb->writeq));
	dcb_printf(pdcb, "\tStatistics:\n");
	dcb_printf(pdcb, "\t\tNo. of Reads: 	%d\n", dcb->stats.n_reads);
	dcb_printf(pdcb, "\t\tNo. of Writes:	%d\n", dcb->stats.n_writes);
	dcb_printf(pdcb, "\t\tNo. of Buffered Writes:	%d\n", dcb->stats.n_buffered);
	dcb_printf(pdcb, "\t\tNo. of Accepts: %d\n", dcb->stats.n_accepts);
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
		case DCB_STATE_IDLE:
			return "DCB not yet in polling";
		case DCB_STATE_POLLING:
			return "DCB in the polling loop";
		case DCB_STATE_PROCESSING:
			return "DCB processing event";
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
 * A  DCB based wrapper for printf. Allows formattign printing to
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
	if(dcb->session) {
		if (dcb->session->client) {
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
	dcb_printf(dcb, "\tAverage chain length:	%.1f\n", (float)total / hashsize);
	dcb_printf(dcb, "\tLongest chain length:	%d\n", longest);
}

