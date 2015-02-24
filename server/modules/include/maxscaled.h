#ifndef _MAXSCALED_H
#define _MAXSCALED_H
/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file maxscaled.h The maxscaled protocol module header file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 13/06/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <dcb.h>
#include <spinlock.h>
#include <housekeeper.h>
/**
 * The telnetd specific protocol structure to put in the DCB.
 */
typedef struct	maxscaled {
	SPINLOCK	lock;		/**< Protocol structure lock */
	int		state;		/**< The connection state */
	char		*username;	/**< The login name of the user */
} MAXSCALED;

#define	MAXSCALED_STATE_LOGIN	1	/**< Issued login prompt */
#define MAXSCALED_STATE_PASSWD	2	/**< Issued password prompt */
#define MAXSCALED_STATE_DATA	3	/**< User logged in */

#endif
