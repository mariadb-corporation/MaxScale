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

#include <maxscale/cppdefs.hh>
#include "cache_storage_api.h"

class Storage;

class StorageFactory
{
public:
    ~StorageFactory();

    static StorageFactory* Open(const char* zName);

    /**
     * The capabilities of storages created using this factory.
     * These capabilities may be a superset of those reported
     * by @c storage_capabilities.
     *
     * @return Bitmask of @c cache_storage_capabilities_t values.
     */
    uint32_t capabilities() const { return m_caps; }

    /**
     * The capabilities of storages loaded via this factory. These
     * capabilities may be a subset of those reported by @ capabilities.
     *
     * @return Bitmask of @c cache_storage_capabilities_t values.
     */
    uint32_t storage_capabilities() const { return m_storage_caps; }

    /**
     * Create storage instance.
     *
     * If some of the required functionality (max_count != 0 and/or
     * max_size != 0) is not provided by the underlying storage
     * implementation that will be provided on top of what is "natively"
     * provided.
     *
     * @param model      The needed thread model.
     * @param zName      The name of the storage.
     * @param ttl        Time to live. 0 means no limit.
     * @param max_count  The maximum number of items in the cache.
     *                   0 means no limit.
     * @param max_size   The maximum size of the cache. 0 means
     *                   no limit.
     * @argc             Number of items in argv.
     * @argv             Storage specific arguments.
     *
     * @return A storage instance or NULL in case of errors.
     */
    Storage* createStorage(cache_thread_model_t model,
                           const char* zName,
                           uint32_t ttl,
                           uint64_t max_count,
                           uint64_t max_size,
                           int argc, char* argv[]);

    /**
     * Create raw storage instance.
     *
     * The returned instance provides exactly the functionality the
     * underlying storage module is capable of providing. The provided
     * arguments (notably max_count and max_size) should be adjusted
     * accordingly.
     *
     * @param model      The needed thread model.
     * @param zName      The name of the storage.
     * @param ttl        Time to live. 0 means no limit.
     * @param max_count  The maximum number of items in the cache.
     *                   0 means no limit.
     * @param max_size   The maximum size of the cache. 0 means
     *                   no limit.
     * @argc             Number of items in argv.
     * @argv             Storage specific arguments.
     *
     * @return A storage instance or NULL in case of errors.
     */
    Storage* createRawStorage(cache_thread_model_t model,
                              const char* zName,
                              uint32_t ttl,
                              uint64_t max_count,
                              uint64_t max_size,
                              int argc, char* argv[]);

private:
    StorageFactory(void* handle, CACHE_STORAGE_API* pApi, uint32_t capabilities);

    StorageFactory(const StorageFactory&);
    StorageFactory& operator = (const StorageFactory&);

private:
    void*              m_handle;       /*< dl handle of storage. */
    CACHE_STORAGE_API* m_pApi;         /*< API of storage. */
    uint32_t           m_storage_caps; /*< Capabilities of underlying storage. */
    uint32_t           m_caps;         /*< Capabilities of storages of this factory. */
};
