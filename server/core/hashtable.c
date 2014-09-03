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
#include <stdlib.h>
#include <string.h>
#include <hashtable.h>

/**
 * @file hashtable.c General purpose hashtable routines
 *
 * The hashtable can be create with a custom number of hash buckets,
 * a hash function and optional functions to call make copies of the key
 * and value and to free them.
 *
 * The hashtable is arrange as a set of linked lists, the number of linked
 * lists beign the hashsize as requested by the user. Entries are hashed by
 * calling the hash function that is passed in by the user, this is used as
 * an index into the array of linked lists, usign modulo hashsize.
 *
 * The linked lists are searched using the key comparison function that is
 * passed into the hash table creation routine.
 *
 * By default the hash table keeps the original pointers that are passed in
 * for the keys and values, however two functions can be supplied to copy these
 * a copy function and a free function. Please note the same function is used for
 * the key and the value, if the actions required are different the called functions
 * must understand how to differenate the key and value.
 *
 * The hash table implements a single write, multiple reader locking policy by
 * using a pair of counters and a spinlock. The spinlock is used to protect the
 * number of readers and writers counters when taking out locks. Releasing of
 * locks uses pure atomic actions and thus does not require spinlock protection.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 23/06/2013	Mark Riddoch		Initial implementation
 * 23/07/2013	Mark Riddoch		Addition of hashtable iterator
 * 08/01/2014	Massimiliano Pinto	Added copy and free funtion pointers for keys and values:
 *					it's possible to copy and free different data types via
 *					kcopyfn/kfreefn, vcopyfn/vfreefn
 *
 * @endverbatim
 */

static	void hashtable_read_lock(HASHTABLE *table);
static	void hashtable_read_unlock(HASHTABLE *table);
static	void hashtable_write_lock(HASHTABLE *table);
static	void hashtable_write_unlock(HASHTABLE *table);
static HASHTABLE *hashtable_alloc_real(HASHTABLE* target, 
					int size, 
					int (*hashfn)(), 
					int (*cmpfn)());

/**
 * Special null function used as default memory allfunctions in the hashtable
 * implementation. This avoids having to special case the code that manipulates
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
	return hashtable_alloc_real(NULL, size, hashfn, cmpfn);
}

HASHTABLE* hashtable_alloc_flat(
	HASHTABLE* target, 
	int size, 
	int (*hashfn)(), 
	int (*cmpfn)())
{
	return hashtable_alloc_real(target, size, hashfn, cmpfn);
}

static HASHTABLE *
hashtable_alloc_real(
	HASHTABLE* target, 
	int        size, 
	int (*hashfn)(), 
	int (*cmpfn)())
{
	HASHTABLE       *rval;
	
	if (target == NULL)
	{
		if ((rval = malloc(sizeof(HASHTABLE))) == NULL)
			return NULL;
		rval->ht_isflat = false;
	}
	else
	{
		rval = target;
		rval->ht_isflat = true;
	}
	
#if defined(SS_DEBUG)
	rval->ht_chk_top = CHK_NUM_HASHTABLE;
	rval->ht_chk_tail = CHK_NUM_HASHTABLE;
#endif
	rval->hashsize = size;
	rval->hashfn = hashfn;
	rval->cmpfn = cmpfn;
	rval->kcopyfn = nullfn;
	rval->vcopyfn = nullfn;
	rval->kfreefn = nullfn;
	rval->vfreefn = nullfn;
	rval->n_readers = 0;
	rval->writelock = 0;
	spinlock_init(&rval->spin);
	if ((rval->entries = (HASHENTRIES **)calloc(size, sizeof(HASHENTRIES *))) == NULL)
	{
		free(rval);
		return NULL;
	}
	memset(rval->entries, 0, size * sizeof(HASHENTRIES *));
	
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

	hashtable_write_lock(table);
	for (i = 0; i < table->hashsize; i++)
	{
		entry = table->entries[i];
		while (entry)
		{
			ptr = entry->next;
			table->kfreefn(entry->key);
			table->vfreefn(entry->value);
			free(entry);
			entry = ptr;
		}
	}
	free(table->entries);
	
	if (!table->ht_isflat)
	{
		free(table);
	}
}

/**
 * Provide memory management functions to the hash table. This allows
 * function pointers to be registered that can make copies of the
 * key and value and free them as well.
 *
 * @param table		The hash table
 * @param kcopyfn	The copy function for the key
 * @param vcopyfn	The copy function for the value
 * @param kfreefn	The free function for the key
 * @param vfreefn	The free function for the value
 */
void
hashtable_memory_fns(HASHTABLE *table, HASHMEMORYFN kcopyfn, HASHMEMORYFN vcopyfn, HASHMEMORYFN kfreefn, HASHMEMORYFN vfreefn)
{
	if (kcopyfn != NULL)
		table->kcopyfn = kcopyfn;
	if (vcopyfn != NULL)
		table->vcopyfn = vcopyfn;
	if (kfreefn != NULL)
		table->kfreefn = kfreefn;
	if (vfreefn != NULL)
		table->vfreefn = vfreefn;
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
        int		hashkey;
        HASHENTRIES	*entry;

        if (key == NULL || value == NULL)
            return 0;

        if (table->hashsize <= 0) {            
            return 0;
        } else {
            hashkey = table->hashfn(key) % table->hashsize;
        }
	hashtable_write_lock(table);
	entry = table->entries[hashkey % table->hashsize];
	while (entry && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry && table->cmpfn(key, entry->key) == 0)
	{
		/* Duplicate key value */
		hashtable_write_unlock(table);
		return 0;
	}
	else
	{
		HASHENTRIES	*ptr = (HASHENTRIES *)malloc(sizeof(HASHENTRIES));
		if (ptr == NULL)
		{
			hashtable_write_unlock(table);
			return 0;
		}

		/* copy the key */
		ptr->key = table->kcopyfn(key);

		/* check succesfull key copy */
		if ( ptr->key  == NULL) {
			return 0;
		}

		/* copy the value */
		ptr->value = table->vcopyfn(value);

		/* check succesfull value copy */
		if  ( ptr->value == NULL) {
			/* remove the key ! */
			table->kfreefn(ptr->key);

			/* value not copied, return */
			return 0;
		}

		ptr->next = table->entries[hashkey % table->hashsize];
		table->entries[hashkey % table->hashsize] = ptr;
	}
	hashtable_write_unlock(table);
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
int		hashkey = table->hashfn(key) % table->hashsize;
HASHENTRIES	*entry, *ptr;

	hashtable_write_lock(table);
	entry = table->entries[hashkey % table->hashsize];
	while (entry && entry->key && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry == NULL)
	{
		/* Not found */
		hashtable_write_unlock(table);
		return 0;
	}

	if (entry == table->entries[hashkey % table->hashsize])
	{
		/* We are removing from the first entry */
		table->entries[hashkey % table->hashsize] = entry->next;
		table->kfreefn(entry->key);
		table->vfreefn(entry->value);

                if (entry->next != NULL) {
                    entry->key = entry->next->key;
                    entry->value = entry->next->value;
                } else {
                    entry->key = NULL;
                    entry->value = NULL;
                }
		free(entry);
	}
	else
	{
		ptr = table->entries[hashkey % table->hashsize];
		while (ptr && ptr->next != entry)
			ptr = ptr->next;
		if (ptr == NULL)
		{
			hashtable_write_unlock(table);
			return 0;	/* This should never happen */
		}
		ptr->next = entry->next;
		table->kfreefn(entry->key);
		table->vfreefn(entry->value);
		free(entry);
	}
	hashtable_write_unlock(table);
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
int		hashkey = table->hashfn(key) % table->hashsize;
HASHENTRIES	*entry;

	hashtable_read_lock(table);
	entry = table->entries[hashkey % table->hashsize];
	while (entry && entry->key && table->cmpfn(key, entry->key) != 0)
	{
		entry = entry->next;
	}
	if (entry == NULL)
	{
		hashtable_read_unlock(table);
		return NULL;
	}
	else
	{
		hashtable_read_unlock(table);
		return entry->value;
	}
}

/**
 * Print hash table statistics to the standard output
 *
 * @param table		The hash table
 */
void
hashtable_stats(HASHTABLE *table)
{
int		total, longest, i, j;
HASHENTRIES	*entries;

	printf("Hashtable: %p, size %d\n", table, table->hashsize);
	total = 0;
	longest = 0;
	hashtable_read_lock(table);
	for (i = 0; i < table->hashsize; i++)
	{
		j = 0;
		entries = table->entries[i];
		while (entries)
		{
			j++;
			entries = entries->next;
		}
		total += j;
		if (j > longest)
			longest = j;
	}
	hashtable_read_unlock(table);
	printf("\tNo. of entries:     	%d\n", total);
	printf("\tAverage chain length:	%.1f\n", (float)total / table->hashsize);
	printf("\tLongest chain length:	%d\n", longest);
}

/** 
 * Produces stat output about hashtable 
 *
 * Parameters:
 * @param table - <usage>
 *          <description>
 *
 * @param hashsize - <usage>
 *          <description>
 *
 * @param nelems - <usage>
 *          <description>
 *
 * @param longest - <usage>
 *          <description>
 *
 * @return void
 *
 *
 */
void hashtable_get_stats(
        void* table,
        int*  hashsize,
        int*  nelems,
        int*  longest)
{
        HASHTABLE*   ht;
        HASHENTRIES* entries;
        int          i;
        int          j;

        ht = (HASHTABLE *)table;
        CHK_HASHTABLE(ht);
        *nelems = 0;
        *longest = 0;
	hashtable_read_lock(ht);
        
	for (i = 0; i < ht->hashsize; i++)
	{
		j = 0;
		entries = ht->entries[i];
		while (entries)
		{
			j++;
			entries = entries->next;
		}
		*nelems += j;
		if (j > *longest) {
			*longest = j;
                }
	}
        *hashsize = ht->hashsize;
	hashtable_read_unlock(ht);
}


/**
 * Take a read lock on the hashtable.
 *
 * The hashtable support multiple readers and a single writer,
 * we have a spinlock to protect the two counts, n_readers and
 * writelock.
 *
 * We take the hashtable spinlock and then check that writelock
 * is set to zero. If not we release the spinlock and do dirty
 * reads of writelock until it goes to 0. Once it is zero we
 * acquire the spinlock again and test that writelock is still
 * 0.
 *
 * With writelock set to zero we increment n_readers with the
 * spinlock still held.
 *
 * @param table		The hashtable to lock.
 */
static void
hashtable_read_lock(HASHTABLE *table)
{
	spinlock_acquire(&table->spin);
	while (table->writelock)
	{
		spinlock_release(&table->spin);
		while (table->writelock)
			;
		spinlock_acquire(&table->spin);
	}
	table->n_readers++;
	spinlock_release(&table->spin);
}

/**
 * Release a previously obtained readlock.
 *
 * Simply decrement the n_readers value for the hash table
 *
 * @param table		The hash table to unlock
 */
static void
hashtable_read_unlock(HASHTABLE *table)
{
	atomic_add(&table->n_readers, -1);
}

/**
 * Obtain an exclusive write lock for the hash table.
 *
 * We acquire the hashtable spinlock, check for the number of
 * readers beign zero. If it is not we hold the spinlock and
 * loop waiting for the n_readers to reach zero. This will prevent
 * any new readers beign granted access but will not prevent current
 * readers releasing the read lock.
 *
 * Once we have no readers we increment writelock and test if we are
 * the only writelock holder, if not we repeat the process. We hold
 * the spinlock throughout the process since both read and write
 * locks do not require the spinlock to be acquired.
 *
 * @param table	The table to lock for updates
 */
static void
hashtable_write_lock(HASHTABLE *table)
{
int	available;

	spinlock_acquire(&table->spin);
	do {
		while (table->n_readers)
			;
		available = atomic_add(&table->writelock, 1);
		if (available != 0)
			atomic_add(&table->writelock, -1);
	} while (available != 0);
	spinlock_release(&table->spin);
}

/**
 * Release the write lock on the hash table.
 *
 * @param table The hash table to unlock
 */
static void
hashtable_write_unlock(HASHTABLE *table)
{
	atomic_add(&table->writelock, -1);
}

/**
 * Create an iterator on a hash table
 *
 * @param table		The table to ceate an iterator on
 * @return	An iterator to use in future calls
 */
HASHITERATOR *
hashtable_iterator(HASHTABLE *table)
{
HASHITERATOR	*rval;

	if ((rval = (HASHITERATOR *)malloc(sizeof(HASHITERATOR))) != NULL)
	{
		rval->table = table;
		rval->chain = 0;
		rval->depth = -1;
	}
	return rval;
}

/**
 * Return the next key for a hashtable iterator
 *
 * @param iter	The hashtable iterator
 * @return	The next key value or NULL
 */
void *
hashtable_next(HASHITERATOR *iter)
{
int		i;
HASHENTRIES	*entries;

	iter->depth++;
	while (iter->chain < iter->table->hashsize)
	{
		if ((entries = iter->table->entries[iter->chain]) != NULL)
		{
			i = 0;
			hashtable_read_lock(iter->table);
			while (entries && i < iter->depth)
			{
				entries = entries->next;
				i++;
			}
			hashtable_read_unlock(iter->table);
			if (entries)
				return entries->key;
		}
		iter->depth = 0;
		iter->chain++;
	}
	return NULL;
}

/**
 * Free a hashtable iterator
 *
 * @param iter	The iterator to free
 */
void
hashtable_iterator_free(HASHITERATOR *iter)
{
	free(iter);
}
