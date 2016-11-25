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

#define MXS_MODULE_NAME "cache"
#include "storagefactory.h"
#include <dlfcn.h>
#include <sys/param.h>
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>
#include "storage.h"


namespace
{

bool open_cache_storage(const char* zName, void** pHandle, CACHE_STORAGE_API** ppApi)
{
    bool rv = false;

    char path[MAXPATHLEN + 1];
    sprintf(path, "%s/lib%s.so", get_libdir(), zName);

    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (handle)
    {
        void* f = dlsym(handle, CACHE_STORAGE_ENTRY_POINT);

        if (f)
        {
            CACHE_STORAGE_API* pApi = ((CacheGetStorageAPIFN)f)();

            if (pApi)
            {
                if ((pApi->initialize)())
                {
                    *pHandle = handle;
                    *ppApi = pApi;

                    rv = true;
                }
                else
                {
                    MXS_ERROR("Initialization of %s failed.", path);

                    (void)dlclose(handle);
                }
            }
            else
            {
                MXS_ERROR("Could not obtain API object from %s.", zName);

                (void)dlclose(handle);
            }
        }
        else
        {
            const char* s = dlerror();
            MXS_ERROR("Could not look up symbol %s from %s: %s",
                      zName, CACHE_STORAGE_ENTRY_POINT, s ? s : "");
        }
    }
    else
    {
        const char* s = dlerror();
        MXS_ERROR("Could not load %s: %s", zName, s ? s : "");
    }

    return rv;
}


void close_cache_storage(void* handle, CACHE_STORAGE_API* pApi)
{
    // TODO: pApi->finalize();

    if (dlclose(handle) != 0)
    {
        const char *s = dlerror();
        MXS_ERROR("Could not close module %s: ", s ? s : "");
    }
}

}

StorageFactory::StorageFactory(void* handle, CACHE_STORAGE_API* pApi)
    : m_handle(handle)
    , m_pApi(pApi)
{
    ss_dassert(handle);
    ss_dassert(pApi);
}

StorageFactory::~StorageFactory()
{
    close_cache_storage(m_handle, m_pApi);
    m_handle = 0;
    m_pApi = 0;
}

//static
StorageFactory* StorageFactory::Open(const char* zName)
{
    StorageFactory* pFactory = 0;

    void* handle;
    CACHE_STORAGE_API* pApi;

    if (open_cache_storage(zName, &handle, &pApi))
    {
        pFactory = new (std::nothrow) StorageFactory(handle, pApi);

        if (!pFactory)
        {
            close_cache_storage(handle, pApi);
        }
    }

    return pFactory;
}

Storage* StorageFactory::createStorage(cache_thread_model_t model,
                                       const char* zName,
                                       uint32_t ttl,
                                       int argc, char* argv[])
{
    ss_dassert(m_handle);
    ss_dassert(m_pApi);

    Storage* pStorage = 0;
    CACHE_STORAGE* pRawStorage = m_pApi->createInstance(model, zName, ttl, argc, argv);

    if (pRawStorage)
    {
        pStorage = new (std::nothrow) Storage(m_pApi, pRawStorage);

        if (!pStorage)
        {
            m_pApi->freeInstance(pRawStorage);
        }
    }

    return pStorage;
}
