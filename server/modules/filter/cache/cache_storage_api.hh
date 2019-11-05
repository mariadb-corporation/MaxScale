/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <cstdbool>
#include <cstdint>
#include <jansson.h>

#include <functional>
#include <string>

#include <maxscale/buffer.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

enum cache_result_bits_t
{
    CACHE_RESULT_OK               = 0x01,
    CACHE_RESULT_NOT_FOUND        = 0x02,
    CACHE_RESULT_ERROR            = 0x03,
    CACHE_RESULT_OUT_OF_RESOURCES = 0x04,

    CACHE_RESULT_STALE     = 0x10000,   /*< Possibly combined with OK and NOT_FOUND. */
    CACHE_RESULT_DISCARDED = 0x20000,   /*< Possibly combined with NOT_FOUND. */
};

typedef uint32_t cache_result_t;

#define CACHE_RESULT_IS_OK(result)               (result & CACHE_RESULT_OK)
#define CACHE_RESULT_IS_NOT_FOUND(result)        (result & CACHE_RESULT_NOT_FOUND)
#define CACHE_RESULT_IS_ERROR(result)            (result & CACHE_RESULT_ERROR)
#define CACHE_RESULT_IS_OUT_OF_RESOURCES(result) (result & CACHE_RESULT_OUT_OF_RESOURCES)
#define CACHE_RESULT_IS_STALE(result)            (result & CACHE_RESULT_STALE)
#define CACHE_RESULT_IS_DISCARDED(result)        (result & CACHE_RESULT_DISCARDED)

enum cache_flags_t
{
    CACHE_FLAGS_NONE          = 0x00,
    CACHE_FLAGS_INCLUDE_STALE = 0x01,
};

enum cache_storage_info_t
{
    // TODO: Provide more granularity.
    CACHE_STORAGE_INFO_ALL = 0
};

enum cache_thread_model_t
{
    CACHE_THREAD_MODEL_ST,
    CACHE_THREAD_MODEL_MT
};

enum cache_invalidate_t
{
    CACHE_INVALIDATE_NEVER,
    CACHE_INVALIDATE_CURRENT,
};

// This is the structure defining the key of the cache.
//
// The user and host are stored explicitly and used when comparing
// for equality to ensure that it will not be possible for a user to
// accidentally gain access to another user's data if there happens to
// be a hash value clash, however unlikely that may be.
struct CACHE_KEY
{
    std::string user;      // The user of the value; empty if shared.
    std::string host;      // The host of the user of the value; empty is shared.
    uint64_t    data_hash; // Hash of the default db and GWBUF given to Cache::get_key().
    uint64_t    full_hash; // Hash of the entire CACHE_KEY.
};

/**
 * Hashes a CACHE_KEY to a size_t
 *
 * @param key  The key to be hashed.
 *
 * @return The corresponding hash.
 */
size_t cache_key_hash(const CACHE_KEY& key);

/**
 * Are two CACHE_KEYs equal.
 *
 * @param lhs One cache key.
 * @param rhs Another cache key.
 *
 * @return True, if the keys are equal.
 */
bool cache_key_equal_to(const CACHE_KEY& lhs, const CACHE_KEY& rhs);

enum cache_storage_capabilities_t
{
    CACHE_STORAGE_CAP_NONE         = 0x00,
    CACHE_STORAGE_CAP_ST           = 0x01, /*< Storage can optimize for single thread. */
    CACHE_STORAGE_CAP_MT           = 0x02, /*< Storage can handle multiple threads. */
    CACHE_STORAGE_CAP_LRU          = 0x04, /*< Storage capable of LRU eviction. */
    CACHE_STORAGE_CAP_MAX_COUNT    = 0x08, /*< Storage capable of capping number of entries.*/
    CACHE_STORAGE_CAP_MAX_SIZE     = 0x10, /*< Storage capable of capping size of cache.*/
    CACHE_STORAGE_CAP_INVALIDATION = 0x20, /*< Storage capable of invalidation.*/
};

static inline bool cache_storage_has_cap(uint32_t capabilities, uint32_t mask)
{
    return (capabilities & mask) == mask;
}

const uint32_t CACHE_USE_CONFIG_TTL = static_cast<uint32_t>(-1);

class Storage
{
public:
    Storage(const Storage&) = delete;
    Storage& operator=(const Storage&) = delete;

    enum what_info_t
    {
        INFO_ALL = CACHE_STORAGE_INFO_ALL
    };

    struct Config
    {
        Config()
        {
        }

        Config(cache_thread_model_t thread_model)
            : thread_model(thread_model)
        {
        }

        Config(cache_thread_model_t thread_model,
               uint32_t hard_ttl,
               uint32_t soft_ttl,
               uint32_t max_count,
               uint64_t max_size,
               cache_invalidate_t invalidate)
            : thread_model(thread_model)
            , hard_ttl(hard_ttl)
            , soft_ttl(soft_ttl)
            , max_count(max_count)
            , max_size(max_size)
            , invalidate(invalidate)
        {
        }

        /**
         * Specifies whether the storage will be used in a single thread or multi
         * thread context. In the latter case the storage must perform thread
         * synchronization as appropriate, in the former  case it need not.
         */
        cache_thread_model_t thread_model = CACHE_THREAD_MODEL_MT;

        /**
         * Hard Time-to-live; number of seconds the value is valid. A value of 0 means
         * that there is no time-to-live, but that the value is considered fresh
         * as long as it is available.
         */
        uint32_t hard_ttl = 0;

        /**
         * Soft Time-to-live; number of seconds the value is valid. A value of 0 means
         * that there is no time-to-live, but that the value is considered fresh
         * as long as it is available. When the soft TTL has passed, but the hard TTL
         * has not yet been reached, the stale cached value will be returned, provided
         * the flag @c CACHE_FLAGS_INCLUDE_STALE is specified when getting the value.
         */
        uint32_t soft_ttl = 0;

        /**
         * The maximum number of items the storage may store, before it should
         * evict some items. A value of 0 means that there is no limit. The caller
         * should specify 0, unless CACHE_STORAGE_CAP_MAX_COUNT is returned at
         * initialization.
         */
        uint32_t max_count = 0;

        /**
         * The maximum size of the storage may may occupy, before it should evict
         * some items. A value of 0 means that there is no limit. The caller should
         * specify 0, unless CACHE_STORAGE_CAP_MAX_SIZE is returned at initialization.
         */
        uint64_t max_size = 0;

        /**
         * Whether or not the storage should perform invalidation. The caller should
         * specify CACHE_INVALIDATE_NEVER, unless CACHE_STORAGE_CAP_INVALIDATION is
         * returned at initialization.
         */
        cache_invalidate_t invalidate = CACHE_INVALIDATE_NEVER;
    };

    virtual ~Storage();

    /**
     * Returns the configuration the storage was created with.
     *
     * @param pConfig  Pointer to object that will be updated.
     */
    virtual void get_config(Config* pConfig) = 0;

    /**
     * Returns information about the storage.
     *
     * @param what  Bitmask of cache_storage_info_t values.
     * @param info  Upon successful return points to json_t object containing
     *              information. The caller should call @c json_decref on the
     *              object when it is no longer needed.
     *
     * @return CACHE_RESULT_OK if a json object could be created.
     */
    virtual cache_result_t get_info(uint32_t what, json_t** ppInfo) const = 0;

    /**
     * Get a value from the cache.
     *
     * @param key       A key generated with get_key.
     * @param flags     Mask of cache_flags_t values.
     * @param soft_ttl  The soft TTL. A value of CACHE_USE_CONFIG_TTL (-1) indicates
     *                   that the value specfied in the config, used in the creation,
     *                   should be used.
     * @param hard_ttl  The hard TTL. A value of CACHE_USE_CONFIG_TTL (-1) indicates
     *                   that the value specfied in the config, used in the creation,
     *                   should be used.
     * @param ppValue   Pointer to variable that after a successful return will
     *                  point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if item was found, CACHE_RESULT_NOT_FOUND if
     *         item was not found or some other error code. In the OK an NOT_FOUND
     *         cases, the bit CACHE_RESULT_STALE is set if the item exists but the
     *         soft TTL has passed. In the NOT_FOUND case, the but CACHE_RESULT_DISCARDED
     *         if the item existed but the hard TTL had passed.
     */
    virtual cache_result_t get_value(const CACHE_KEY& key,
                                     uint32_t flags,
                                     uint32_t soft_ttl,
                                     uint32_t hard_ttl,
                                     GWBUF** ppValue) = 0;

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             GWBUF** ppValue)
    {
        return get_value(key, flags, CACHE_USE_CONFIG_TTL, CACHE_USE_CONFIG_TTL, ppValue);
    }

    /**
     * Put a value to the cache.
     *
     * @param key                 A key generated with get_key.
     * @param invalidation_words  Words that may be used for invalidating the entry.
     * @param pValue              Pointer to GWBUF containing the value to be stored.
     *                            Must be one contiguous buffer.
     * @return CACHE_RESULT_OK if item was successfully put,
     *         CACHE_RESULT_OUT_OF_RESOURCES if item could not be put, due to
     *         some resource having become exhausted, or some other error code.
     */
    virtual cache_result_t put_value(const CACHE_KEY& key,
                                     const std::vector<std::string>& invalidation_words,
                                     const GWBUF* pValue) = 0;

    /**
     * Delete a value from the cache.
     *
     * @param storage    Pointer to a CACHE_STORAGE.
     * @param key        A key generated with get_key.
     *
     * @return CACHE_RESULT_OK if item was successfully deleted.  Note that
     *         CACHE_RESULT_OK may be returned also if the entry was not present.
     */
    virtual cache_result_t del_value(const CACHE_KEY& key) = 0;

    /**
     * Invalidate entries
     *
     * @param words  Words that decide what entries are invalidated.
     *
     * @return CACHE_RESULT_OK if the invalidation succeeded.
     */
    virtual cache_result_t invalidate(const std::vector<std::string>& words) = 0;

    /**
     * Clear storage
     *
     * @return CACHE_RESULT_OK is the clearing succeeded.
     */
    virtual cache_result_t clear() = 0;

    /**
     * Get the head item from the storage. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may be incorrect the moment it has
     * been returned.
     *
     * @param key     Pointer to variable that after a successful return will
     *                contain the key.
     * @param ppHead  Pointer to variable that after a successful return will
     *                point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if the head item was returned,
     *         CACHE_RESULT_NOT_FOUND if the cache is empty,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage is incapable of
     *         returning the head, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    virtual cache_result_t get_head(CACHE_KEY* pKey, GWBUF** ppHead) = 0;

    /**
     * Get the tail item from the cache. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may become incorrect the moment it
     * has been returned.
     *
     * @param key     Pointer to variable that after a successful return will
     *                contain the key.
     * @param ppTail  Pointer to variable that after a successful return will
     *                point to a GWBUF.
     *
     * @return CACHE_RESULT_OK if the head item was returned,
     *         CACHE_RESULT_NOT_FOUND if the cache is empty,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage is incapable of
     *         returning the tail, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    virtual cache_result_t get_tail(CACHE_KEY* pKey, GWBUF** ppTail) = 0;

    /**
     * Get the current size of the storage. This is only intended for testing and
     * debugging purposes and if the storage is being used by different threads
     * at the same time, the returned result may become incorrect the moment it
     * has been returned.
     *
     * @param pSize  Pointer to variable that after a successful return will
     *               contain the current size of the storage.
     *
     * @return CACHE_RESULT_OK if the size was returned,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage
     *         is incapable of returning the size, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    virtual cache_result_t get_size(uint64_t* pSize) const = 0;

    /**
     * Get the current number of items in the storage. This is only intended for
     * testing and debugging purposes and if the storage is being used by different
     * threads at the same time, the returned result may become incorrect the moment
     * it has been returned.
     *
     * @param pItems  Pointer to variable that after a successful return will
     *                contain the current number of items in the storage.
     *
     * @return CACHE_RESULT_OK if the size was returned,
     *         CACHE_RESULT_OUT_OF_RESOURCES if the storage
     *         is incapable of returning the size, and
     *         CACHE_RESULT_ERROR otherwise.
     */
    virtual cache_result_t get_items(uint64_t* pItems) const = 0;

protected:
    Storage() {}
};

class StorageModule
{
public:
    /**
     * Called immediately after the storage module has been loaded.
     *
     * @param capabilities On successful return, contains a bitmask of
     *                     cache_storage_capabilities_t values.
     *
     * @return True if the initialization succeeded, false otherwise.
     */
    virtual bool initialize(uint32_t* capabilities) = 0;

    /**
     * Called immediately before the storage module will be unloaded.
     */
    virtual void finalize() = 0;

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
    virtual Storage* create_storage(const char* name,
                                    const Storage::Config& config,
                                    int argc, char* argv[]) = 0;
};


#define CACHE_STORAGE_ENTRY_POINT "CacheGetStorageModule"
typedef StorageModule* (* CacheGetStorageModuleFN)();

namespace std
{

template<>
struct equal_to<CACHE_KEY>
{
    bool operator()(const CACHE_KEY& lhs, const CACHE_KEY& rhs) const
    {
        return cache_key_equal_to(lhs, rhs);
    }
};

template<>
struct hash<CACHE_KEY>
{
    size_t operator()(const CACHE_KEY& key) const
    {
        return cache_key_hash(key);
    }
};
}

std::string cache_key_to_string(const CACHE_KEY& key);

inline bool operator==(const CACHE_KEY& lhs, const CACHE_KEY& rhs)
{
    return cache_key_equal_to(lhs, rhs);
}

inline bool operator!=(const CACHE_KEY& lhs, const CACHE_KEY& rhs)
{
    return !(lhs == rhs);
}

class CacheKey : public CACHE_KEY
{
public:
    CacheKey()
    {
        data_hash = 0;
        full_hash = 0;
    }
};
