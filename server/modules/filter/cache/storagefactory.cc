/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
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
#include <maxscale/paths.hh>
#include "cachefilter.hh"
#include "lrustoragest.hh"
#include "lrustoragemt.hh"


namespace
{

bool open_storage_module(const char* zName,
                         void** pHandle,
                         StorageModule** ppModule,
                         cache_storage_kind_t* pKind,
                         uint32_t* pCapabilities)
{
    bool rv = false;

    char path[MAXPATHLEN + 1];
    sprintf(path, "%s/lib%s.so", mxs::libdir(), zName);

    void* handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);

    if (handle)
    {
        void* f = dlsym(handle, CACHE_STORAGE_ENTRY_POINT);

        if (f)
        {
            StorageModule* pModule = ((CacheGetStorageModuleFN)f)();

            if (pModule)
            {
                if (pModule->initialize(pKind, pCapabilities))
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
                               cache_storage_kind_t kind,
                               uint32_t capabilities)
    : m_handle(handle)
    , m_pModule(pModule)
    , m_kind(kind)
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
    cache_storage_kind_t kind;
    uint32_t capabilities;

    if (open_storage_module(zName, &handle, &pModule, &kind, &capabilities))
    {
        pFactory = new StorageFactory(handle, pModule, kind, capabilities);

        if (!pFactory)
        {
            close_cache_storage(handle, pModule);
        }
    }

    return pFactory;
}

Storage* StorageFactory::create_storage(const char* zName,
                                        const Storage::Config& config,
                                        const std::string& arguments)
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);

    switch (m_kind)
    {
    case CACHE_STORAGE_PRIVATE:
        return create_private_storage(zName, config, arguments);

    case CACHE_STORAGE_SHARED:
        return create_shared_storage(zName, config, arguments);
    }

    mxb_assert(!true);
    return nullptr;
}

Storage* StorageFactory::create_raw_storage(const char* zName,
                                            const Storage::Config& config,
                                            const std::string& arguments)
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);

    return m_pModule->create_storage(zName, config, arguments);
}

Storage* StorageFactory::create_private_storage(const char* zName,
                                                const Storage::Config& config,
                                                const std::string& arguments)
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);
    mxb_assert(m_kind == CACHE_STORAGE_PRIVATE);

    Storage::Config storage_config(config);

    uint32_t mask = CACHE_STORAGE_CAP_MAX_COUNT | CACHE_STORAGE_CAP_MAX_SIZE;

    if (!cache_storage_has_cap(m_storage_caps, mask))
    {
        // Ok, so the storage implementation does not support eviction, which
        // means we will have to wrap it. As the wrapper will handle all necessary
        // locking according to the threading model, the storage itself may be
        // single threaded. No point in locking twice.
        storage_config.thread_model = CACHE_THREAD_MODEL_ST;
        storage_config.max_count = 0;
        storage_config.max_size = 0;
    }

    if (!cache_storage_has_cap(m_storage_caps, CACHE_STORAGE_CAP_INVALIDATION))
    {
        // Ok, so the storage implementation does not support invalidation.
        // We can't request it.
        storage_config.invalidate = CACHE_INVALIDATE_NEVER;

        if (config.invalidate != CACHE_INVALIDATE_NEVER)
        {
            // But invalidation is needed so we will wrap the raw storage with
            // a storage that handles both eviction and invalidation. So no need
            // to request eviction from the raw storage.
            storage_config.max_count = 0;
            storage_config.max_size = 0;
        }
    }

    Storage* pStorage = create_raw_storage(zName, storage_config, arguments);

    if (pStorage)
    {
        if (config.invalidate != CACHE_INVALIDATE_NEVER)
        {
            mask |= CACHE_STORAGE_CAP_INVALIDATION;
        }

        if (!cache_storage_has_cap(m_storage_caps, mask))
        {
            // Ok, so the cache cannot handle eviction (LRU) and/or invalidation.
            // Let's decorate the raw storage with a storage than can.

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

Storage* StorageFactory::create_shared_storage(const char* zName,
                                               const Storage::Config& config,
                                               const std::string& arguments)
{
    mxb_assert(m_handle);
    mxb_assert(m_pModule);
    mxb_assert(m_kind == CACHE_STORAGE_SHARED);

    if (config.invalidate != CACHE_INVALIDATE_NEVER
        && !cache_storage_has_cap(m_storage_caps, CACHE_STORAGE_CAP_INVALIDATION))
    {
        MXS_ERROR("Invalidation is requested, but not natively supported by the "
                  "storage %s. As the storage is shared the invalidation cannot be "
                  "provided by the cache filter itself.", zName);
        return nullptr;
    }

    Storage::Config storage_config(config);

    if (storage_config.max_count != 0
        && !cache_storage_has_cap(m_storage_caps, CACHE_STORAGE_CAP_MAX_COUNT))
    {
        MXS_WARNING("The storage %s is shared and the maximum number of items cannot "
                    "be specified locally; the 'max_count' setting is ignored.",
                    zName);
        storage_config.max_count = 0;
    }

    if (storage_config.max_size != 0
        && !cache_storage_has_cap(m_storage_caps, CACHE_STORAGE_CAP_MAX_SIZE))
    {
        MXS_WARNING("The storage %s is shared and the maximum size of the cache "
                    "cannot be specified locally; the 'max_size' setting is ignored.",
                    zName);
        storage_config.max_size = 0;
    }

    return create_raw_storage(zName, storage_config, arguments);
}
