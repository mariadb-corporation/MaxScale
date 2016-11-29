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

#include "storage_rocksdb.h"
#include "../../cache_storage_api.h"
#include "rocksdbstorage.h"

namespace
{

bool initialize()
{
    return RocksDBStorage::Initialize();
}

CACHE_STORAGE* createInstance(cache_thread_model_t, // Ignored, RocksDB always MT safe.
                              const char* zName,
                              uint32_t ttl,
                              int argc, char* argv[])
{
    ss_dassert(zName);

    CACHE_STORAGE* pStorage = 0;

    try
    {
        pStorage = reinterpret_cast<CACHE_STORAGE*>(RocksDBStorage::Create(zName, ttl, argc, argv));
    }
    catch (const std::bad_alloc&)
    {
        MXS_OOM();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Standard exception caught: %s", x.what());
    }
    catch (...)
    {
        MXS_ERROR("Unknown exception caught.");
    }

    return pStorage;
}

void freeInstance(CACHE_STORAGE* pInstance)
{
    delete reinterpret_cast<RocksDBStorage*>(pInstance);
}

cache_result_t getKey(CACHE_STORAGE* pStorage,
                      const char* zDefaultDB,
                      const GWBUF* pQuery,
                      CACHE_KEY* pKey)
{
    ss_dassert(pStorage);
    // zDefaultDB may be NULL.
    ss_dassert(pQuery);
    ss_dassert(pKey);

    cache_result_t result = CACHE_RESULT_ERROR;

    try
    {
        result = reinterpret_cast<RocksDBStorage*>(pStorage)->getKey(zDefaultDB, pQuery, pKey);
    }
    catch (const std::bad_alloc&)
    {
        MXS_OOM();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Standard exception caught: %s", x.what());
    }
    catch (...)
    {
        MXS_ERROR("Unknown exception caught.");
    }

    return result;
}

cache_result_t getValue(CACHE_STORAGE* pStorage,
                        const CACHE_KEY* pKey,
                        uint32_t flags,
                        GWBUF** ppResult)
{
    ss_dassert(pStorage);
    ss_dassert(pKey);
    ss_dassert(ppResult);

    cache_result_t result = CACHE_RESULT_ERROR;

    try
    {
        result = reinterpret_cast<RocksDBStorage*>(pStorage)->getValue(pKey, flags, ppResult);
    }
    catch (const std::bad_alloc&)
    {
        MXS_OOM();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Standard exception caught: %s", x.what());
    }
    catch (...)
    {
        MXS_ERROR("Unknown exception caught.");
    }

    return result;
}

cache_result_t putValue(CACHE_STORAGE* pStorage,
                        const CACHE_KEY* pKey,
                        const GWBUF* pValue)
{
    ss_dassert(pStorage);
    ss_dassert(pKey);
    ss_dassert(pValue);

    cache_result_t result = CACHE_RESULT_ERROR;

    try
    {
        result = reinterpret_cast<RocksDBStorage*>(pStorage)->putValue(pKey, pValue);
    }
    catch (const std::bad_alloc&)
    {
        MXS_OOM();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Standard exception caught: %s", x.what());
    }
    catch (...)
    {
        MXS_ERROR("Unknown exception caught.");
    }

    return result;
}

cache_result_t delValue(CACHE_STORAGE* pStorage,
                        const CACHE_KEY* pKey)
{
    ss_dassert(pStorage);
    ss_dassert(pKey);

    cache_result_t result = CACHE_RESULT_ERROR;

    try
    {
        result = reinterpret_cast<RocksDBStorage*>(pStorage)->delValue(pKey);
    }
    catch (const std::bad_alloc&)
    {
        MXS_OOM();
    }
    catch (const std::exception& x)
    {
        MXS_ERROR("Standard exception caught: %s", x.what());
    }
    catch (...)
    {
        MXS_ERROR("Unknown exception caught.");
    }

    return result;
}

}

extern "C"
{

CACHE_STORAGE_API* CacheGetStorageAPI()
{
    static CACHE_STORAGE_API api =
        {
            initialize,
            createInstance,
            freeInstance,
            getKey,
            getValue,
            putValue,
            delValue,
        };

    return &api;
}

}
