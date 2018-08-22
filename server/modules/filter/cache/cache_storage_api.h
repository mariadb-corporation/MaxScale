/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
 #pragma once

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <stdint.h>
#include <jansson.h>
#include <maxscale/buffer.h>
#include <maxscale/protocol/mysql.h>

MXS_BEGIN_DECLS

typedef enum cache_result_bits
{
    CACHE_RESULT_OK               = 0x01,
    CACHE_RESULT_NOT_FOUND        = 0x02,
    CACHE_RESULT_ERROR            = 0x03,
    CACHE_RESULT_OUT_OF_RESOURCES = 0x04,

    CACHE_RESULT_STALE            = 0x10000, /*< Possibly combined with OK and NOT_FOUND. */
    CACHE_RESULT_DISCARDED        = 0x20000, /*< Possibly combined with NOT_FOUND. */
} cache_result_bits_t;

typedef uint32_t cache_result_t;

#define CACHE_RESULT_IS_OK(result)               (result & CACHE_RESULT_OK)
#define CACHE_RESULT_IS_NOT_FOUND(result)        (result & CACHE_RESULT_NOT_FOUND)
#define CACHE_RESULT_IS_ERROR(result)            (result & CACHE_RESULT_ERROR)
#define CACHE_RESULT_IS_OUT_OF_RESOURCES(result) (result & CACHE_RESULT_OUT_OF_RESOURCES)
#define CACHE_RESULT_IS_STALE(result)            (result & CACHE_RESULT_STALE)
#define CACHE_RESULT_IS_DISCARDED(result)        (result & CACHE_RESULT_DISCARDED)

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

typedef struct cache_key
{
    uint64_t data;
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

typedef struct cache_storage_config_t
{
    /**
     * Specifies whether the storage will be used in a single thread or multi
     * thread context. In the latter case the storage must perform thread
     * synchronization as appropriate, in the former  case it need not.
     */
    cache_thread_model_t thread_model;

    /**
     * Hard Time-to-live; number of seconds the value is valid. A value of 0 means
     * that there is no time-to-live, but that the value is considered fresh
     * as long as it is available.
     */
    uint32_t hard_ttl;

    /**
     * Soft Time-to-live; number of seconds the value is valid. A value of 0 means
     * that there is no time-to-live, but that the value is considered fresh
     * as long as it is available. When the soft TTL has passed, but the hard TTL
     * has not yet been reached, the stale cached value will be returned, provided
     * the flag @c CACHE_FLAGS_INCLUDE_STALE is specified when getting the value.
     */
    uint32_t soft_ttl;

    /**
     * The maximum number of items the storage may store, before it should
     * evict some items. A value of 0 means that there is no limit. The caller
     * should specify 0, unless CACHE_STORAGE_CAP_MAX_COUNT is returned at
     * initialization.
     */
    uint32_t max_count;

    /**
     * The maximum size of the storage may may occupy, before it should evict
     * some items. A value of 0 means that there is no limit. The caller should
     * specify 0, unless CACHE_STORAGE_CAP_MAX_SIZE is returned at initialization.
     */
    uint64_t max_size;
} CACHE_STORAGE_CONFIG;

typedef struct cache_storage_api
{
    /**
     * Called immediately after the storage module has been loaded.
     *
     * @param capabilities On successful return, contains a bitmask of
     *                     cache_storage_capabilities_t values.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    bool (*initialize)(uint32_t* capabilities);

    /**
     * Creates an instance of cache storage. This function should, if necessary,
     * create the actual storage, initialize it and prepare to put and get
     * cache items.
     *
     * @param name      The name of the cache instance.
     * @param config    The storage configuration.
     * @param argc      The number of elements in the argv array.
     * @param argv      Array of arguments, as passed in the `storage_options`
     *                  parameter in the cache section in the MaxScale configuration
     *                  file.
     *
     * @return A new cache instance, or NULL if the instance could not be
     *         created.
     */
    CACHE_STORAGE* (*createInstance)(const char *name,
                                     const CACHE_STORAGE_CONFIG* config,
                                     int argc, char* argv[]);

    /**
     * Frees an CACHE_STORAGE instance earlier created with createInstance.
     *
     * @param instance The CACHE_STORAGE instance to be freed.
     */
    void (*freeInstance)(CACHE_STORAGE* instance);

    /**
     * Returns the configuration the storage was created with.
     *
     * @param storage  Pointer to a CACHE_STORAGE
     * @param config   Pointer to variable that will be updated with the config.
     */
    void (*getConfig)(CACHE_STORAGE* storage,
                      CACHE_STORAGE_CONFIG* config);

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
     * Get a value from the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     * @param flags      Mask of cache_flags_t values.
     * @param soft_ttl   The soft TTL. A value of CACHE_USE_CONFIG_TTL (-1) indicates
     *                   that the value specfied in the config, used in the creation,
     *                   should be used.
     * @param hard_ttl   The hard TTL. A value of CACHE_USE_CONFIG_TTL (-1) indicates
     *                   that the value specfied in the config, used in the creation,
     *                   should be used.
     * @param result     Pointer to variable that after a successful return will
     *                   point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if item was found, CACHE_RESULT_NOT_FOUND if
     *         item was not found or some other error code. In the OK an NOT_FOUND
     *         cases, the bit CACHE_RESULT_STALE is set if the item exists but the
     *         soft TTL has passed. In the NOT_FOUND case, the but CACHE_RESULT_DISCARDED
     *         if the item existed but the hard TTL had passed.
     */
    cache_result_t (*getValue)(CACHE_STORAGE* storage,
                               const CACHE_KEY* key,
                               uint32_t flags,
                               uint32_t soft_ttl,
                               uint32_t hard_ttl,
                               GWBUF** result);

    /**
     * Put a value to the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     * @param value      Pointer to GWBUF containing the value to be stored.
     *                   Must be one contiguous buffer.
     *
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
     *
     * @return CACHE_RESULT_OK if item was successfully deleted.  Note that
     *         CACHE_RESULT_OK may be returned also if the entry was not present.
     */
    cache_result_t (*delValue)(CACHE_STORAGE* storage,
                               const CACHE_KEY* key);

    /**
     * Get the head item from the storage. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may be incorrect the moment it has
     * been returned.
     *
     * @param storage  Pointer to a CACHE_STORAGE.
     * @param key      Pointer to variable that after a successful return will
     *                 contain the key.
     * @param head     Pointer to variable that after a successful return will
     *                 point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if the head item was returned,
     *         CACHE_RESULT_NOT_FOUND if the cache is empty,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage is incapable of
     *         returning the head, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    cache_result_t (*getHead)(CACHE_STORAGE* storage,
                              CACHE_KEY* key,
                              GWBUF** head);

    /**
     * Get the tail item from the cache. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may become incorrect the moment it
     * has been returned.
     *
     * @param storage  Pointer to a CACHE_STORAGE.
     * @param key      Pointer to variable that after a successful return will
     *                 contain the key.
     * @param tail     Pointer to variable that after a successful return will
     *                 point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if the head item was returned,
     *         CACHE_RESULT_NOT_FOUND if the cache is empty,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage is incapable of
     *         returning the tail, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    cache_result_t (*getTail)(CACHE_STORAGE* storage,
                              CACHE_KEY* key,
                              GWBUF** tail);

    /**
     * Get the current size of the storage. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may become incorrect the moment it
     * has been returned.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param size       Pointer to variable that after a successful return will
     *                   contain the current size of the storage.
     *
     * @return CACHE_RESULT_OK if the size was returned,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage
     *         is incapable of returning the size, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    cache_result_t (*getSize)(CACHE_STORAGE* storage,
                              uint64_t* size);

    /**
     * Get the current number of items in the storage. This is only intended for
     * testing and debugging purposes and if the storage is being used by different
     * threads at the same time, the returned result may become incorrect the moment
     * it has been returned.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param items      Pointer to variable that after a successful return will
     *                   contain the current number of items in the storage.
     *
     * @return CACHE_RESULT_OK if the size was returned,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage
     *         is incapable of returning the size, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    cache_result_t (*getItems)(CACHE_STORAGE* storage,
                               uint64_t* items);
} CACHE_STORAGE_API;

#if defined __cplusplus
const uint32_t CACHE_USE_CONFIG_TTL = static_cast<uint32_t>(-1);
#else
#define CACHE_USE_CONFIG_TTL ((uint32_t)-1)
#endif

#define CACHE_STORAGE_ENTRY_POINT "CacheGetStorageAPI"
typedef CACHE_STORAGE_API* (*CacheGetStorageAPIFN)();

MXS_END_DECLS
