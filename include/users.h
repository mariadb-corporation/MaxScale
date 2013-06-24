#ifndef _USERS_H
#define _USERS_H
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
#include <hashtable.h>
/**
 * @file users.h The functions to manipulate the table of users maintained
 * for each service
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 23/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The users table statistics structure
 */
typedef struct {
	int	n_entries;		/**< The number of entries */
	int	n_adds;			/**< The number of inserts */
	int	n_deletes;		/**< The number of deletes */
	int	n_fetches;		/**< The number of fetchs */
} USERS_STATS;
/**
 * The user table, this contains the username and authentication data required
 * for the authentication implementation within the gateway.
 */
typedef struct users {
	HASHTABLE	*data;		/**< The hashtable containing the actual data */
	USERS_STATS	stats;		/**< The statistics for the users table */
} USERS;

extern USERS	*users_alloc();				/**< Allocate a users table */
extern void	users_free(USERS *);			/**< Free a users table */
extern int	users_add(USERS *, char *, char *);	/**< Add a user to the users table */
extern int	users_delete(USERS *, char *);		/**< Delete a user from the users table */
extern char	*users_fetch(USERS *, char *);		/**< Fetch the authentication data for a user */
#endif
