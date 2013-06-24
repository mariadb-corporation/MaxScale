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
 * Special null function used as default memory allfunctions in the hashtable
 * implementation. This avoids havign to special case the code that manipulates
 * the keys and values
 *
 * @param	data	The data pointer
 * @return	Return the value we were called with
 */
static void *
nullfn(void *data)
{
	return data;
}

/**
 * Allocate a new hash table
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
	rval->copyfn = nullfn;
	rval->freefn = nullfn;
	if ((rval->entries = calloc(size, sizeof(HASHENTRIES))) == NULL)
	{
		free(rval);
		return NULL;
	}
	memset(rval->entries, 0, size * sizeof(HASHENTRIES));

	return rval;
}

/**
 * Delete an entire hash table
 *
 * @param	table	The hash table to delete
 */
void
hashtable_free(HASHTABLE *table)
{
int		i;
HASHENTRIES	*entry, *ptr;

	for (i = 0; i < table->hashsize; i++)
	{
		entry = table->entries[i];
		if (entry->key)
		{
			table->freefn(entry->key);
			table->freefn(entry->value);
		}
		entry = entry->next;
		while (entry)
		{
			ptr = entry->next;
			table->freefn(entry->key);
			table->freefn(entry->value);
			free(entry);
			entry = ptr;
		}
	}
	free(table->entries);
	free(table);
}

/**
 * Provide memory management functions to the hash table. This allows
 * function pointers to be registered that can make copies of the
 * key and value.
 *
 * @param table		The hash table
 * @param copyfn	The copy function
 * @param freefn	The free function
 */
void
hashtable_memory_fns(HASHTABLE *table, HASHMEMORYFN copyfn, HASHMEMORYFN freefn)
{
	table->copyfn = copyfn;
	table->freefn = freefn;
}

/**
 * Add an item to the hash table.
 *
 * @param table		The hash table to which to add the item
 * @param key		The key of the item
 * @param value		The value for the item
 * @return	Return the number of items added
 */
int
hashtable_add(HASHTABLE *table, void *key, void *value)
{
int		hashkey = table->hashfn(key);
HASHENTRIES	*entry;

	entry = table->entries[hashkey % table->hashsize];
	while (entry->next && entry->key && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry->key == NULL)
	{
		/* Entry is empty - special case for first insert */
		entry->key = table->copyfn(key);
		entry->value = table->copyfn(value);
	}
	else if (table->cmpfn(key, entry->key) == 0)
	{
		/* Duplicate key value */
		return 0;
	}
	else
	{
		HASHENTRIES	*ptr = (HASHENTRIES *)malloc(sizeof(HASHENTRIES));
		if (ptr == NULL)
			return 0;
		ptr->key = table->copyfn(key);
		ptr->value = table->copyfn(value);
		ptr->next = NULL;
		entry->next = ptr;
	}
	return 1;
}

/**
 * Delete an item from the hash table that has a given key
 *
 * @param table		The hash table to delete from
 * @param key		The key value of the item to remove
 * @return Return the number of items deleted
 */
int
hashtable_delete(HASHTABLE *table, void *key)
{
int		hashkey = table->hashfn(key);
HASHENTRIES	*entry, *ptr;

	entry = table->entries[hashkey % table->hashsize];
	while (entry && entry->key && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry == NULL || entry->key == NULL)
	{
		/* Not found */
		return 0;
	}

	if (entry == table->entries[hashkey % table->hashsize])
	{
		/* We are removing from the special first entry */
		if (entry->next)
		{
			table->freefn(entry->key);
			table->freefn(entry->value);
			entry->key = entry->next->key;
			entry->value = entry->next->value;
			ptr = entry->next;
			entry->next = ptr->next;
			free(ptr);
		}
		else
		{
			table->freefn(entry->key);
			table->freefn(entry->value);
			entry->key = NULL;
			entry->value = NULL;
		}
	}
	else
	{
		ptr = table->entries[hashkey % table->hashsize];
		while (ptr && ptr->next != entry)
			ptr = ptr->next;
		if (ptr == NULL)
			return 0;	/* This should never happen */
		ptr->next = entry->next;
		table->freefn(entry->key);
		table->freefn(entry->value);
		free(entry);
	}
	return 1;
}

/**
 * Fetch an item with a given key value from the hash table
 *
 * @param table		The hash table
 * @param key		The key value
 * @return The item or NULL if the item was not found
 */
void *
hashtable_fetch(HASHTABLE *table, void *key)
{
int		hashkey = table->hashfn(key);
HASHENTRIES	*entry;

	entry = table->entries[hashkey % table->hashsize];
	while (entry && entry->key && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry == NULL || entry->key == NULL)
	{
		return NULL;
	}
	else
	{
		return entry->value;
	}
}

