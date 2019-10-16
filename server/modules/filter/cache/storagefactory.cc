/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "storagefactory.hh"
#include <dlfcn.h>
#include <sys/param.h>
#include <new>
#include <maxbase/alloc.h>
#include <maxscale/paths.h>
#include "cachefilter.hh"
#include "lrustoragest.hh"
#include "lrustoragemt.hh"


namespace
{

bool open_storage_module(const char* zName,
                         void** pHandle,
                         StorageModule** ppModule,
                         uint32_t* pCapabilities)
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
            StorageModule* pModule = ((CacheGetStorageModuleFN)f)();

            if (pModule)
            {
                if (pModule->initialize(pCapabilities))
                {
                    *pHandle = handle;
                    *ppModule = pModule;

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
                      zName,
                      CACHE_STORAGE_ENTRY_POINT,
                      s ? s : "");
        }
    }
    else
    {
        const char* s = dlerror();
        MXS_ERROR("Could not load %s: %s", zName, s ? s : "");
    }

    return rv;
}


void close_cache_storage(void* handle, StorageModule* pModule)
{
    pModule->finalize();

    if (dlclose(handle) != 0)
    {
        const char* s = dlerror();
        MXS_ERROR("Could not close module %s: ", s ? s : "");
    }
}
}

StorageFactory::StorageFactory(void* handle,
                               StorageModule* pModule,
                               uint32_t capabilities)
    : m_handle(handle)
    , m_pModule(pModule)
    , m_storage_caps(capabilities)
    , m_caps(capabilities)
{
    mxb_assert(handle);
    mxb_assert(pModule);

    m_caps |= CACHE_STORAGE_CAP_LRU;
    m_caps |= CACHE_STORAGE_CAP_MAX_COUNT;
    m_caps |= CACHE_STORAGE_CAP_MAX_SIZE;
}

StorageFactory::~StorageFactory()
{
    close_cache_storage(m_handle, m_pModule);
    m_handle = 0;
    m_pModule = 0;
}

// static
StorageFactory* StorageFactory::open(const char* zName)
{
    StorageFactory* pFactory = nullptr;

    void* handle;
    StorageModule* pModule;
    uint32_t capabilities;

    if (open_storage_module(zName, &handle, &pModule, &capabilities))
    {
        pFactory = new StorageFactory(handle, pModule, capabilities);

        if (!pFactory)
        {
            close_cache_storage(handle, pModule);
        }
    }

    return pFactory;
}

Storage* StorageFactory::create_storage(const char* zName,
                                        const CACHE_STORAGE_CONFIG& config,
                                        int argc,
                                        char* argv[])
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);

    CacheStorageConfig used_config(config);

    uint32_t mask = CACHE_STORAGE_CAP_MAX_COUNT | CACHE_STORAGE_CAP_MAX_SIZE;

    if (!cache_storage_has_cap(m_storage_caps, mask))
    {
        // Since we will wrap the native storage with a LRUStorage, according
        // to the used threading model, the storage itself may be single
        // threaded. No point in locking twice.
        used_config.thread_model = CACHE_THREAD_MODEL_ST;
        used_config.max_count = 0;
        used_config.max_size = 0;
    }

    Storage* pStorage = create_raw_storage(zName, used_config, argc, argv);

    if (pStorage)
    {
        if (!cache_storage_has_cap(m_storage_caps, mask))
        {
            // Ok, so the cache cannot handle eviction. Let's decorate the
            // real storage with a storage than can.

            LRUStorage* pLruStorage = NULL;

            if (config.thread_model == CACHE_THREAD_MODEL_ST)
            {
                pLruStorage = LRUStorageST::create(config, pStorage);
            }
            else
            {
                mxb_assert(config.thread_model == CACHE_THREAD_MODEL_MT);

                pLruStorage = LRUStorageMT::create(config, pStorage);
            }

            if (pLruStorage)
            {
                pStorage = pLruStorage;
            }
            else
            {
                delete pStorage;
                pStorage = NULL;
            }
        }
    }

    return pStorage;
}


Storage* StorageFactory::create_raw_storage(const char* zName,
                                            const CACHE_STORAGE_CONFIG& config,
                                            int argc,
                                            char* argv[])
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);

    return m_pModule->create_storage(zName, &config, argc, argv);
}
