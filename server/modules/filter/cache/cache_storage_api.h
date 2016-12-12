#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <stdint.h>
#include <jansson.h>
#include <maxscale/buffer.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/debug.h>

MXS_BEGIN_DECLS

typedef enum cache_result
{
    CACHE_RESULT_OK,
    CACHE_RESULT_NOT_FOUND,
    CACHE_RESULT_STALE,
    CACHE_RESULT_OUT_OF_RESOURCES,
    CACHE_RESULT_ERROR
} cache_result_t;

typedef enum cache_flags
{
    CACHE_FLAGS_NONE          = 0x00,
    CACHE_FLAGS_INCLUDE_STALE = 0x01,
} cache_flags_t;

typedef enum cache_storage_info
{
    // TODO: Provide more granularity.
    CACHE_STORAGE_INFO_ALL = 0
} cache_storage_info_t;

typedef enum cache_thread_model
{
    CACHE_THREAD_MODEL_ST,
    CACHE_THREAD_MODEL_MT
} cache_thread_model_t;

typedef void* CACHE_STORAGE;

enum
{
    CACHE_KEY_MAXLEN = 128
};

typedef struct cache_key
{
    char data[CACHE_KEY_MAXLEN];
} CACHE_KEY;

/**
 * Hashes a CACHE_KEY to a size_t
 *
 * @param key  The key to be hashed.
 *
 * @return The corresponding hash.
 */
size_t cache_key_hash(const CACHE_KEY* key);

/**
 * Are two CACHE_KEYs equal.
 *
 * @param lhs One cache key.
 * @param rhs Another cache key.
 *
 * @return True, if the keys are equal.
 */
bool cache_key_equal_to(const CACHE_KEY* lhs, const CACHE_KEY* rhs);

typedef enum cache_storage_capabilities
{
    CACHE_STORAGE_CAP_NONE      = 0x00,
    CACHE_STORAGE_CAP_ST        = 0x01, /*< Storage can optimize for single thread. */
    CACHE_STORAGE_CAP_MT        = 0x02, /*< Storage can handle multiple threads. */
    CACHE_STORAGE_CAP_LRU       = 0x04, /*< Storage capable of LRU eviction. */
    CACHE_STORAGE_CAP_MAX_COUNT = 0x08, /*< Storage capable of capping number of entries.*/
    CACHE_STORAGE_CAP_MAX_SIZE  = 0x10, /*< Storage capable of capping size of cache.*/
} cache_storage_capabilities_t;

static inline bool cache_storage_has_cap(uint32_t capabilities, uint32_t mask)
{
    return (capabilities & mask) == mask;
}

typedef struct cache_storage_api
{
    /**
     * Called immediately after the storage module has been loaded.
     *
     * @param capabilities On successful return, contains a bitmask of
     *                     cache_storage_capabilities_t values.
     * @return True if the initialization succeeded, false otherwise.
     */
    bool (*initialize)(uint32_t* capabilities);

    /**
     * Creates an instance of cache storage. This function should, if necessary,
     * create the actual storage, initialize it and prepare to put and get
     * cache items.
     *
     * @param model     Whether the storage will be used in a single thread or
     *                  multi thread context. In the latter case the storage must
     *                  perform thread synchronization as appropriate, in the former
     *                  case it need not.
     * @param name      The name of the cache instance.
     * @param ttl       Time to live; number of seconds the value is valid.
     *                  A value of 0 means that there is no time-to-live, but that
     *                  the value is considered fresh as long as it is available.
     * @param max_count The maximum number of items the storage may store, before
     *                  it should evict some items. A value of 0 means that there is
     *                  no limit. The caller should specify 0, unless
     *                  CACHE_STORAGE_CAP_MAX_COUNT is returned at initialization.
     * @param max_count The maximum size of the storage may may occupy, before it
     *                  should evict some items. A value if 0 means that there is
     *                  no limit. The caller should specify 0, unless
     *                  CACHE_STORAGE_CAP_MAX_SIZE is returned at initialization.
     * @param argc      The number of elements in the argv array.
     * @param argv      Array of arguments, as passed in the `storage_options`
     *                  parameter in the cache section in the MaxScale configuration
     *                  file.
     *
     * @return A new cache instance, or NULL if the instance could not be
     *         created.
     */
    CACHE_STORAGE* (*createInstance)(cache_thread_model_t model,
                                     const char *name,
                                     uint32_t ttl,
                                     uint32_t max_count,
                                     uint64_t max_size,
                                     int argc, char* argv[]);

    /**
     * Frees an CACHE_STORAGE instance earlier created with createInstance.
     *
     * @param instance The CACHE_STORAGE instance to be freed.
     */
    void (*freeInstance)(CACHE_STORAGE* instance);

    /**
     * Returns information about the storage.
     *
     * @param storage  Pointer to a CACHE_STORAGE.
     * @param what     Bitmask of cache_storage_info_t values.
     * @param info     Upon successful return points to json_t object containing
     *                 information. The caller should call @c json_decref on the
     *                 object when it is no longer needed.
     *
     * @return CACHE_RESULT_OK if a json object could be created.
     */
    cache_result_t (*getInfo)(CACHE_STORAGE* storage,
                              uint32_t what,
                              json_t** info);
    /**
     * Create a key for a GWBUF.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param query      An SQL query. Must be one contiguous buffer.
     * @param key        Pointer to key.
     *
     * @return CACHE_RESULT_OK if a key was created, otherwise some error code.
     */
    cache_result_t (*getKey)(CACHE_STORAGE* storage,
                             const char* default_db,
                             const GWBUF* query,
                             CACHE_KEY* key);
    /**
     * Get a value from the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     * @param flags      Mask of cache_flags_t values.
     * @param result     Pointer to variable that after a successful return will
     *                   point to a GWBUF.
     * @return CACHE_RESULT_OK if item was found,
     *         CACHE_RESULT_STALE if CACHE_FLAGS_INCLUDE_STALE was specified in
     *         flags and the item was found but stale,
     *         CACHE_RESULT_NOT_FOUND if item was not found (which may be because
     *         the ttl was reached), or some other error code.
     */
    cache_result_t (*getValue)(CACHE_STORAGE* storage,
                               const CACHE_KEY* key,
                               uint32_t flags,
                               GWBUF** result);

    /**
     * Put a value to the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     * @param value      Pointer to GWBUF containing the value to be stored.
     *                   Must be one contiguous buffer.
     * @return CACHE_RESULT_OK if item was successfully put,
     *         CACHE_RESULT_OUT_OF_RESOURCES if item could not be put, due to
     *         some resource having become exhausted, or some other error code.
     */
    cache_result_t (*putValue)(CACHE_STORAGE* storage,
                               const CACHE_KEY* key,
                               const GWBUF* value);

    /**
     * Delete a value from the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     * @return CACHE_RESULT_OK if item was successfully deleted.  Note that
     *         CACHE_RESULT_OK may be returned also if the entry was not present.
     */
    cache_result_t (*delValue)(CACHE_STORAGE* storage,
                               const CACHE_KEY* key);
} CACHE_STORAGE_API;

#define CACHE_STORAGE_ENTRY_POINT "CacheGetStorageAPI"
typedef CACHE_STORAGE_API* (*CacheGetStorageAPIFN)();

MXS_END_DECLS
