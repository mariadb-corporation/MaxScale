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

#include <maxscale/cppdefs.hh>
#include "cache_storage_api.h"

class Storage
{
public:
    enum what_info_t
    {
        INFO_ALL = CACHE_STORAGE_INFO_ALL
    };

    virtual ~Storage();

    /**
     * Returns the configuration the storage was created with.
     *
     * @param pConfig  Pointer to object that will be updated.
     */
    virtual void get_config(CACHE_STORAGE_CONFIG* pConfig) = 0;

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
                                     GWBUF** ppValue) const = 0;

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             GWBUF** ppValue) const
    {
        return get_value(key, flags, CACHE_USE_CONFIG_TTL, CACHE_USE_CONFIG_TTL, ppValue);
    }

    /**
     * Put a value to the cache.
     *
     * @param key     A key generated with get_key.
     * @param pValue  Pointer to GWBUF containing the value to be stored.
     *                Must be one contiguous buffer.
     * @return CACHE_RESULT_OK if item was successfully put,
     *         CACHE_RESULT_OUT_OF_RESOURCES if item could not be put, due to
     *         some resource having become exhausted, or some other error code.
     */
    virtual cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue) = 0;

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
    virtual cache_result_t get_head(CACHE_KEY* pKey, GWBUF** ppHead) const = 0;

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
    virtual cache_result_t get_tail(CACHE_KEY* pKey, GWBUF** ppTail) const = 0;

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
    Storage();

    Storage(const Storage&);
    Storage& operator = (const Storage&);
};
