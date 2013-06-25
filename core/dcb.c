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
 * Date		Who		Description
 * 12/06/13	Mark Riddoch	Initial implementation
 * 21/06/13	Massimiliano Pinto	free_dcb is used
 * 25/06/13	Massimiliano Pinto	Added checks to session and router_session
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

static	DCB		*allDCBs = NULL;	/* Diagnotics need a list of DCBs */
static	SPINLOCK	*dcbspin = NULL;

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

	if (dcbspin == NULL)
	{
		if ((dcbspin = malloc(sizeof(SPINLOCK))) == NULL)
			return NULL;
		spinlock_init(dcbspin);
	}

	if ((rval = malloc(sizeof(DCB))) == NULL)
	{
		return NULL;
	}
	spinlock_init(&rval->writeqlock);
	rval->writeq = NULL;
	rval->remote = NULL;
	rval->state = DCB_STATE_ALLOC;
	rval->next = NULL;
	rval->data = NULL;
	rval->protocol = NULL;
	rval->session = NULL;
	memset(&rval->stats, 0, sizeof(DCBSTATS));	// Zero the statistics

	spinlock_acquire(dcbspin);
	if (allDCBs == NULL)
		allDCBs = rval;
	else
	{
		DCB *ptr = allDCBs;
		while (ptr->next)
			ptr = ptr->next;
		ptr->next = rval;
	}
	spinlock_release(dcbspin);
	return rval;
}

/**
 * Free a DCB and remove it from the chain of all DCBs
 *
 * @param dcb The DCB to free
 */
void
dcb_free(DCB *dcb)
{
	dcb->state = DCB_STATE_FREED;

	/* First remove this DCB from the chain */
	spinlock_acquire(dcbspin);
	if (allDCBs == dcb)
	{
		/*
		 * Deal with the special case of removign the DCB at the head of
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
	spinlock_release(dcbspin);

	if (dcb->protocol)
		free(dcb->protocol);
	if (dcb->data)
		free(dcb->data);
	if (dcb->remote)
		free(dcb->remote);
	free(dcb);
}

/**
 * Connect to a server
 *
 * @param server	The server to connect to
 * @param session	The session this connection is being made for
 * @param protocol	The protocol module to use
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
		dcb_free(dcb);
		return NULL;
	}
	memcpy(&(dcb->func), funcs, sizeof(GWPROTOCOL));
	dcb->session = session;

	if ((dcb->fd = dcb->func.connect(dcb, server, session)) == -1)
	{
		dcb_free(dcb);
		return NULL;
	}
	server->stats.n_connections++;

	poll_add_dcb(dcb);
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
GWBUF 	*buffer = NULL;
int 	b, n = 0;

	ioctl(dcb->fd, FIONREAD, &b);
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

		// append read data to the gwbuf
		*head = gwbuf_append(*head, buffer);

		/* Re issue the ioctl as the amount of data buffered may have changed */
		ioctl(dcb->fd, FIONREAD, &b);
	}

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
	poll_remove_dcb(dcb);
	close(dcb->fd);
	dcb->state = DCB_STATE_DISCONNECTED;

	if (dcb_isclient(dcb))
	{
		/*
		 * If the DCB we are closing is a client side DCB then shutdown the
		 * router session. This will close any backend connections.
		 */
		SERVICE *service = dcb->session->service;

		if (service && service->router && dcb->session->router_session)
		{
			service->router->closeSession(service->router_instance,
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

	if (dcbspin == NULL)
	{
		if ((dcbspin = malloc(sizeof(SPINLOCK))) == NULL)
			return;
		spinlock_init(dcbspin);
	}
	spinlock_acquire(dcbspin);
	dcb = allDCBs;
	while (dcb)
	{
		printDCB(dcb);
		dcb = dcb->next;
	}
	spinlock_release(dcbspin);
}


/**
 * Diagnostic to print all DCB allocated in the system
 *
 */
void dprintAllDCBs(DCB *pdcb)
{
DCB	*dcb;

	if (dcbspin == NULL)
	{
		if ((dcbspin = malloc(sizeof(SPINLOCK))) == NULL)
			return;
		spinlock_init(dcbspin);
	}
	spinlock_acquire(dcbspin);
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
	spinlock_release(dcbspin);
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

	buf->end = GWBUF_DATA(buf) + strlen(GWBUF_DATA(buf)) + 1;
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
