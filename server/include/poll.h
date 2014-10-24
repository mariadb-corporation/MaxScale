#ifndef _POLL_H
#define _POLL_H
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
#include <dcb.h>
#include <gwbitmask.h>

/**
 * @file poll.h	The poll related functionality 
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 19/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#define	MAX_EVENTS	1000
#define	EPOLL_TIMEOUT	1000	/**< The epoll timeout in milliseconds */

extern	void		poll_init();
extern	int		poll_add_dcb(DCB *);
extern	int		poll_remove_dcb(DCB *);
extern	void		poll_waitevents(void *);
extern	void		poll_shutdown();
extern	GWBITMASK	*poll_bitmask();
extern	void		dprintPollStats(DCB *);
extern	void		dShowThreads(DCB *dcb);
#endif
