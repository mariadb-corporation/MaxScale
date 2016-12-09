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
#include "cache_storage_api.h"

class Storage;

class StorageFactory
{
public:
    ~StorageFactory();

    static StorageFactory* Open(const char* zName);

    Storage* createStorage(cache_thread_model_t model,
                           const char* zName,
                           uint32_t ttl,
                           uint32_t max_count,
                           uint64_t max_size,
                           int argc, char* argv[]);

private:
    StorageFactory(void* handle, CACHE_STORAGE_API* pApi, uint32_t capabilities);

    StorageFactory(const StorageFactory&);
    StorageFactory& operator = (const StorageFactory&);

private:
    void*              m_handle;       /*< dl handle of storage. */
    CACHE_STORAGE_API* m_pApi;         /*< API of storage. */
    uint32_t           m_capabilities; /*< Capabilities of storage. */
};
