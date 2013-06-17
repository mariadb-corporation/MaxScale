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
#include <modules.h>

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
alloc_dcb()
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
	rval->state = DCB_STATE_ALLOC;
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
 * @param dcb THe DCB to free
 */
void
free_dcb(DCB *dcb)
{
	dcb->state = DCB_STATE_FREED;

	/* First remove this DCB from the chain */
	spinlock_acquire(dcbspin);
	if (allDCBs == dcb)
		allDCBs = dcb->next;
	else
	{
		DCB *ptr = allDCBs;
		while (ptr && ptr->next != dcb)
			ptr = ptr->next;
		if (ptr)
			ptr->next = dcb->next;
	}
	spinlock_release(dcbspin);

	free(dcb);
}

/**
 * Connect to a server
 *
 * @param server	The server to connect to
 * @param session	The session this connection is being made for
 * @param protcol	The protocol module to use
 */
DCB *
connect_dcb(SERVER *server, SESSION *session, const char *protocol)
{
DCB		*dcb;
GWPROTOCOL	*funcs;
int		epollfd = -1;	// Need to work out how to get this

	if ((dcb = alloc_dcb()) == NULL)
	{
		return NULL;
	}
	if ((funcs = (GWPROTOCOL *)load_module(protocol, "Protocol")) == NULL)
	{
		free(dcb);
		return NULL;
	}
	memcpy(&(dcb->func), funcs, sizeof(GWPROTOCOL));
	dcb->session = session;

	if ((dcb->fd = dcb->func.connect(server, session, epollfd)) == -1)
	{
		free(dcb);
		return NULL;
	}
	/*
	 * We are now connected, the authentication etc will happen as
	 * part of the EPOLLOUT event that will be received once the connection
	 * is established.
	 */
	return dcb;
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
	(void)printf("DCB: 0x%x\n", (void *)dcb);
	(void)printf("\tDCB state: %s\n", gw_dcb_state2string(dcb->state));
	(void)printf("\tQueued write data: %d\n", gwbuf_length(dcb->writeq));
	(void)printf("\tStatistics:\n");
	(void)printf("\t\tNo. of Reads: %d\n", dcb->stats.n_reads);
	(void)printf("\t\tNo. of Writes: %d\n", dcb->stats.n_writes);
	(void)printf("\t\tNo. of Buffered Writes: %d\n", dcb->stats.n_buffered);
	(void)printf("\t\tNo. of Accepts: %d\n", dcb->stats.n_accepts);
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
			return "DCB in the EPOLL";
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

	dcb->func.write(dcb, buf);
}
