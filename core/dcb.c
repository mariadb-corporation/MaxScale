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
 * dcb.c  -  Descriptor Control Block generic functions
 *
 * Revision History
 *
 * Date		Who		Description
 * 12/06/13	Mark Riddoch	Initial implementation
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <dcb.h>
#include <spinlock.h>

static	DCB		*allDCBs = NULL;	/* Diagnotics need a list of DCBs */
static	SPINLOCK	*dcbspin = NULL;

/*
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

/*
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

/*
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
}

/*
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

/*
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
		default:
			return "DCB (unknown)";
	}
}
