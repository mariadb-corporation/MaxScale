#ifndef _HOUSEKEEPER_H
#define _HOUSEKEEPER_H
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
 * Copyright SkySQL Ab 2014
 */
#include <time.h>

/**
 * @file housekeeper.h A mechanism to have task run periodically
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 29/08/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The housekeeper task list
 */
typedef struct hktask {
	char	*name;			/*< A simple task name */
	void	(*task)(void *data);	/*< The task to call */
	void	*data;			/*< Data to pass the task */
	int	frequency;		/*< How often to call the tasks (seconds) */
	time_t	nextdue;		/*< When the task should be next run */
	struct	hktask
		*next;			/*< Next task in the list */
} HKTASK;

extern void hkinit();
extern int  hktask_add(char *name, void (*task)(void *), void *data, int frequency);
extern int  hktask_remove(char *name);
#endif
