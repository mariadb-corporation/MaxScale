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

#include "shard_map.hh"

#include <maxscale/alloc.h>

int hashkeyfun(const void* key)
{
    if (key == NULL)
    {
        return 0;
    }
    int hash = 0, c = 0;
    const char* ptr = (const char*)key;
    while ((c = *ptr++))
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

int hashcmpfun(const void* v1, const void* v2)
{
    const char* i1 = (const char*) v1;
    const char* i2 = (const char*) v2;

    return strcmp(i1, i2);
}

void keyfreefun(void* data)
{
    MXS_FREE(data);
}

shard_map_t* shard_map_alloc()
{
    shard_map_t *rval = (shard_map_t*) MXS_MALLOC(sizeof(shard_map_t));

    if (rval)
    {
        if ((rval->hash = hashtable_alloc(SCHEMAROUTER_HASHSIZE, hashkeyfun, hashcmpfun)))
        {
            HASHCOPYFN kcopy = (HASHCOPYFN)strdup;
            hashtable_memory_fns(rval->hash, kcopy, kcopy, keyfreefun, keyfreefun);
            spinlock_init(&rval->lock);
            rval->last_updated = 0;
            rval->state = SHMAP_UNINIT;
        }
        else
        {
            MXS_FREE(rval);
            rval = NULL;
        }
    }
    return rval;
}

enum shard_map_state shard_map_update_state(shard_map_t *self, double refresh_min_interval)
{
    spinlock_acquire(&self->lock);
    double tdiff = difftime(time(NULL), self->last_updated);
    if (tdiff > refresh_min_interval)
    {
        self->state = SHMAP_STALE;
    }
    enum shard_map_state state = self->state;
    spinlock_release(&self->lock);
    return state;
}

void replace_shard_map(shard_map_t **target, shard_map_t **source)
{
    shard_map_t *tgt = *target;
    shard_map_t *src = *source;
    tgt->last_updated = src->last_updated;
    tgt->state = src->state;
    hashtable_free(tgt->hash);
    tgt->hash = src->hash;
    MXS_FREE(src);
    *source = NULL;
}

shard_map_t* get_latest_shard_map(shard_map_t *stored, shard_map_t *current)
{
    shard_map_t *map = stored;

    spinlock_acquire(&map->lock);

    if (map->state == SHMAP_STALE)
    {
        replace_shard_map(&map, &current);
    }
    else if (map->state != SHMAP_READY)
    {
        MXS_WARNING("Shard map state is not ready but"
                    "it is in use. Replacing it with a newer one.");
        replace_shard_map(&map, &current);
    }
    else
    {
        /**
         * Another thread has already updated the shard map for this user
         */
        hashtable_free(current->hash);
        MXS_FREE(current);
    }

    spinlock_release(&map->lock);

    return map;
}