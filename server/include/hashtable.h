#ifndef _HASTABLE_H
#define _HASTABLE_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file hashtable.h A general purpose hashtable mechanism for use within the
 * gateway
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 23/06/2013   Mark Riddoch            Initial implementation
 * 23/07/2013   Mark Riddoch            Addition of iterator mechanism
 * 08/01/2014   Massimiliano Pinto      Added function pointers for key/value copy and free
 *                                      the routine hashtable_memory_fns() changed accordingly
 *
 * @endverbatim
 */
#include <skygw_debug.h>
#include <spinlock.h>
#include <atomic.h>
#include <dcb.h>

/**
 * The entries within a hashtable.
 *
 * A NULL value for key indicates an empty entry.
 * The next pointer is the overflow chain for this hashentry.
 */
typedef struct hashentry
{
    void *key;              /**< The value of the key or NULL if empty entry */
    void *value;            /**< The value associated with key */
    struct hashentry *next; /**< The overflow chain */
} HASHENTRIES;

/**
 * HASHTABLE iterator - used to walk the hashtable in a thread safe
 * way
 */
typedef struct hashiterator
{
    struct hashtable *table; /**< The hashtable the iterator refers to */
    int chain;               /**< The current chain we are walking */
    int depth;               /**< The current depth down the chain */
} HASHITERATOR;

/**
 * The type definition for the memory allocation functions
 */
typedef void *(*HASHMEMORYFN)(void *);

/**
 * The general purpose hashtable struct.
 */
typedef struct hashtable
{
#if defined(SS_DEBUG)
    skygw_chk_t ht_chk_top;
#endif
    int hashsize;                 /**< The number of HASHENTRIES */
    HASHENTRIES **entries;        /**< The entries themselves */
    int (*hashfn)(void *);        /**< The hash function */
    int (*cmpfn)(void *, void *); /**< The key comparison function */
    HASHMEMORYFN kcopyfn;         /**< Optional key copy function */
    HASHMEMORYFN vcopyfn;         /**< Optional value copy function */
    HASHMEMORYFN kfreefn;         /**< Optional key free function */
    HASHMEMORYFN vfreefn;         /**< Optional value free function */
    SPINLOCK spin;                /**< Internal spinlock for the hashtable */
    int n_readers;                /**< Number of clients reading the table */
    int writelock;                /**< The table is locked by a writer */
    bool ht_isflat;               /**< Indicates whether hashtable is in stack or heap */
    int n_elements;               /**< Number of added elements */
#if defined(SS_DEBUG)
    skygw_chk_t ht_chk_tail;
#endif
} HASHTABLE;

extern HASHTABLE *hashtable_alloc(int, int (*hashfn)(), int (*cmpfn)());
HASHTABLE *hashtable_alloc_flat(HASHTABLE* target,
                                int size,
                                int (*hashfn)(),
                                int (*cmpfn)());
/**< Allocate a hashtable */
extern void hashtable_memory_fns(HASHTABLE   *table,
                                 HASHMEMORYFN kcopyfn,
                                 HASHMEMORYFN vcopyfn,
                                 HASHMEMORYFN kfreefn,
                                 HASHMEMORYFN vfreefn);
/**< Provide an interface to control key/value memory
 * manipulation
 */
extern void hashtable_free(HASHTABLE *);                    /**< Free a hashtable */
extern int hashtable_add(HASHTABLE *, void *, void *);     /**< Add an entry */
extern int hashtable_delete(HASHTABLE *, void *);
/**< Delete an entry table */
extern void *hashtable_fetch(HASHTABLE *, void *);
/**< Fetch the data for a given key */
extern void hashtable_stats(HASHTABLE *);                   /**< Print statisitics */
void hashtable_get_stats(void* hashtable,
                         int*  hashsize,
                         int*  nelems,
                         int*  longest);
extern int hashtable_save(HASHTABLE *,
                          char *,
                          int (*keywrite)(int, void*),
                          int (*valuewrite)(int, void*));
extern int hashtable_load(HASHTABLE *,
                          char *,
                          void *(*keyread)(int),
                          void *(*valueread)(int));

extern HASHITERATOR *hashtable_iterator(HASHTABLE *);
/**< Allocate an iterator on the hashtable */
extern void *hashtable_next(HASHITERATOR *);
/**< Return the key of the hash table iterator */
extern void hashtable_iterator_free(HASHITERATOR *);
extern int hashtable_size(HASHTABLE *table);
#endif
