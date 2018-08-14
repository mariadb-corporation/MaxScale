#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file hashtable.h A general purpose hashtable mechanism for use within the
 * gateway
 */
#include <maxscale/cdefs.h>
#include <maxscale/debug.h>
#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

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
 * The type definition for the hash function
 */
typedef int (*HASHHASHFN)(const void *);

/**
 * The type definition for the comparison function
 */
typedef int (*HASHCMPFN)(const void *, const void *);

/**
 * The type definition for the key/value copying functions
 */
typedef void *(*HASHCOPYFN)(const void *);

/**
 * The type definition for the key/value freeing functions
 */
typedef void (*HASHFREEFN)(void *);

/**
 * The general purpose hashtable struct.
 */
typedef struct hashtable
{
    int hashsize;                 /**< The number of HASHENTRIES */
    HASHENTRIES **entries;        /**< The entries themselves */
    HASHHASHFN hashfn;            /**< The hash function */
    HASHCMPFN cmpfn;              /**< The key comparison function */
    HASHCOPYFN kcopyfn;           /**< Optional key copy function */
    HASHCOPYFN vcopyfn;           /**< Optional value copy function */
    HASHFREEFN kfreefn;           /**< Optional key free function */
    HASHFREEFN vfreefn;           /**< Optional value free function */
    SPINLOCK spin;                /**< Internal spinlock for the hashtable */
    int n_readers;                /**< Number of clients reading the table */
    int writelock;                /**< The table is locked by a writer */
    bool ht_isflat;               /**< Indicates whether hashtable is in stack or heap */
    int n_elements;               /**< Number of added elements */
} HASHTABLE;

extern HASHTABLE *hashtable_alloc(int, HASHHASHFN hashfn, HASHCMPFN cmpfn);
HASHTABLE *hashtable_alloc_flat(HASHTABLE* target,
                                int size,
                                HASHHASHFN hashfn,
                                HASHCMPFN cmpfn);
/**< Allocate a hashtable */
extern void hashtable_memory_fns(HASHTABLE   *table,
                                 HASHCOPYFN kcopyfn,
                                 HASHCOPYFN vcopyfn,
                                 HASHFREEFN kfreefn,
                                 HASHFREEFN vfreefn);
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
                          const char *filename,
                          int (*keywrite)(int, void*),
                          int (*valuewrite)(int, void*));
extern int hashtable_load(HASHTABLE *,
                          const char *filename,
                          void *(*keyread)(int),
                          void *(*valueread)(int));

extern HASHITERATOR *hashtable_iterator(HASHTABLE *);
/**< Allocate an iterator on the hashtable */
extern void *hashtable_next(HASHITERATOR *);
/**< Return the key of the hash table iterator */
extern void hashtable_iterator_free(HASHITERATOR *);
extern int hashtable_size(HASHTABLE *table);

extern void hashtable_item_free(void *data);
extern int hashtable_item_strcasecmp(const void* str1, const void* str2);
extern int hashtable_item_strcmp(const void* str1, const void* str2);
extern void* hashtable_item_strdup(const void *str);
extern int hashtable_item_strhash(const void *str);

MXS_END_DECLS
