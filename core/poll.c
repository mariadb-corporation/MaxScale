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
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <poll.h>
#include <dcb.h>
#include <atomic.h>

/**
 * @file poll.c  - Abraction of the epoll functionality
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 19/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

static	int	epoll_fd = -1;	/**< The epoll file descriptor */

/**
 * The polling statistics
 */
static struct {
	int	n_read;		/**< Number of read events */
	int	n_write;	/**< Number of write events */
	int	n_error;	/**< Number of error events */
	int	n_hup;		/**< Number of hangup events */
	int	n_accept;	/**< Number of accept events */
	int	n_polls;	/**< Number of poll cycles */
} pollStats;


/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
	if (epoll_fd != -1)
		return;
	if ((epoll_fd = epoll_create(MAX_EVENTS)) == -1)
	{
		perror("epoll_create");
		exit(-1);
	}
	memset(&pollStats, 0, sizeof(pollStats));
}

/**
 * Add a DCB to the set of descriptors within the polling
 * environment.
 *
 * @param dcb	The descriptor to add to the poll
 * @return	-1 on error or 0 on success
 */
int
poll_add_dcb(DCB *dcb)
{
struct	epoll_event	ev;

	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
	ev.data.ptr = dcb;

	return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dcb->fd, &ev);
	
}

/**
 * Remove a descriptor from the set of descriptors within the
 * polling environment.
 *
 * @param dcb	The descriptor to remove
 * @return	-1 on error or 0 on success
 */
int
poll_remove_dcb(DCB *dcb)
{
struct	epoll_event	ev;

	return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dcb->fd, &ev);
}

/**
 * The main polling loop
 *
 * This routine does the polling and despatches of IO events
 * to the DCB's
 */
void
poll()
{
struct	epoll_event	events[MAX_EVENTS];
int			i, nfds;

	while (1)
	{
		atomic_add(&pollStats.n_polls, 1);
		if ((nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1)) == -1)
		{
		}
		else
		{
			for (i = 0; i < nfds; i++)
			{
				DCB 		*dcb = (DCB *)events[i].data.ptr;
				__uint32_t	ev = events[i].events;

				if (ev & EPOLLERR)
				{
					atomic_add(&pollStats.n_error, 1);
					dcb->func.error(dcb);
				}
				if (ev & EPOLLHUP)
				{
					atomic_add(&pollStats.n_hup, 1);
					dcb->func.hangup(dcb);
				}
				if (ev & EPOLLIN)
				{
					if (dcb->state == DCB_STATE_LISTENING)
					{
						atomic_add(&pollStats.n_accept, 1);
						dcb->func.accept(dcb);
					}
					else
					{
						atomic_add(&pollStats.n_read, 1);
						dcb->func.read(dcb);
					}
				}
				if (ev & EPOLLOUT)
				{
					atomic_add(&pollStats.n_write, 1);
					dcb->func.write_ready(dcb);
				}
			}
		}
	}	
}

/**
 * Debug routine to print the polling statistics
 *
 * @param dcb	DCB to print to
 */
void
dprintPollStats(DCB *dcb)
{
	dcb_printf(dcb, "Number of epoll cycles: 	%d\n", pollStats.n_polls);
	dcb_printf(dcb, "Number of read events:   	%d\n", pollStats.n_read);
	dcb_printf(dcb, "Number of write events: 	%d\n", pollStats.n_write);
	dcb_printf(dcb, "Number of error events: 	%d\n", pollStats.n_error);
	dcb_printf(dcb, "Number of hangup events:	%d\n", pollStats.n_hup);
	dcb_printf(dcb, "Number of accept events:	%d\n", pollStats.n_accept);
}
