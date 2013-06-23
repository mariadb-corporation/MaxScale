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
#include <stdlib.h>
#include <string.h>
#include <users.h>

/**
 * @file users.c User table maintenance routines
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
 * The hash function we user for storing users.
 *
 * @param key	The key value, i.e. username
 * @return The hash key
 */
static int
user_hash(char *key)
{
	return (*key + *(key + 1)) % 52;
}

/**
 * Allocate a new users table
 *
 * @return The users table
 */
USERS *
users_alloc()
{
USERS 	*rval;

	if ((rval = malloc(sizeof(USERS))) == NULL)
		return NULL;

	if ((rval->data = hashtable_alloc(52, user_hash, strcmp)) == NULL)
	{
		free(rval);
		return NULL;
	}

	return rval;
}

