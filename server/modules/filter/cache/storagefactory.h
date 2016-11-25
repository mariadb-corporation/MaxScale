#pragma once
#ifndef _MAXSCALE_FILTER_CACHE_STORAGEFACTORY_H
#define _MAXSCALE_FILTER_CACHE_STORAGEFACTORY_H
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
                           int argc, char* argv[]);

private:
    StorageFactory(void* handle, CACHE_STORAGE_API* pApi);

    StorageFactory(const StorageFactory&);
    StorageFactory& operator = (const StorageFactory&);

private:
    void*              m_handle;
    CACHE_STORAGE_API* m_pApi;
};

#endif
