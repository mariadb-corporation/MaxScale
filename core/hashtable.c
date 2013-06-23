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
#include <hashtable.h>

/**
 * @file hashtable.c General purpose hashtable routines
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
 * Allocate a new users table
 *
 * @param size		The size of the hash table
 * @param hashfn	The user supplied hash function
 * @param cmpfn		The user supplied key comparison function
 * @return The hashtable table
 */
HASHTABLE *
hashtable_alloc(int size, int (*hashfn)(), int (*cmpfn)())
{
HASHTABLE 	*rval;

	if ((rval = malloc(sizeof(HASHTABLE))) == NULL)
		return NULL;
	rval->hashsize = size;
	rval->hashfn = hashfn;
	rval->cmpfn = cmpfn;
	if ((rval->entries = calloc(size, sizeof(HASHENTRIES))) == NULL)
	{
		free(rval);
		return NULL;
	}
	memset(rval->entries, 0, size * sizeof(HASHENTRIES));

	return rval;
}

