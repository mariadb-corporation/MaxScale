/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/cppdefs.hh>

#include <maxscale/service.h>
#include <maxscale/hashtable.h>
#include <maxscale/spinlock.hh>

enum shard_map_state
{
    SHMAP_UNINIT, /*< No databases have been added to this shard map */
    SHMAP_READY, /*< All available databases have been added */
    SHMAP_STALE /*< The shard map has old data or has not been updated recently */
};

/**
 * A map of the shards tied to a single user.
 */
typedef struct shard_map
{
    HASHTABLE *hash; /*< A hashtable of database names and the servers which
                       * have these databases. */
    SPINLOCK lock;
    time_t last_updated;
    enum shard_map_state state; /*< State of the shard map */
} shard_map_t;

/** TODO: Replace these */
int hashkeyfun(const void* key);
int hashcmpfun(const void *, const void *);
void keyfreefun(void* data);

/** TODO: Don't use this everywhere */
/** Size of the hashtable used to store ignored databases */
#define SCHEMAROUTER_HASHSIZE 100

/**
 * Allocate a shard map and initialize it.
 * @return Pointer to new shard_map_t or NULL if memory allocation failed
 */
shard_map_t* shard_map_alloc();

/**
 * Check if the shard map is out of date and update its state if necessary.
 * @param router Router instance
 * @param map Shard map to update
 * @return Current state of the shard map
 */
enum shard_map_state shard_map_update_state(shard_map_t *self, double refresh_min_interval);

/**
 * Replace a shard map with another one. This function copies the contents of
 * the source shard map to the target and frees the source memory.
 * @param target Target shard map to replace
 * @param source Source shard map to use
 */
void replace_shard_map(shard_map_t **target, shard_map_t **source);

/**
 * Return the newer of two shard maps
 *
 * @param stored The currently stored shard map
 * @param current The replacement map the current client is using
 * @return The newer of the two shard maps
 */
shard_map_t* get_latest_shard_map(shard_map_t *stored, shard_map_t *current);
