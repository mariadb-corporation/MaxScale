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

#define MXS_MODULE_NAME "storage_rocksdb"
#include <maxscale/cppdefs.hh>
#include <inttypes.h>
#include "../../cache_storage_api.h"
#include "rocksdbstorage.hh"

using std::unique_ptr;

namespace
{

bool initialize(uint32_t* pCapabilities)
{
    *pCapabilities = CACHE_STORAGE_CAP_MT;

    return RocksDBStorage::Initialize();
}

CACHE_STORAGE* createInstance(cache_thread_model_t, // Ignored, RocksDB always MT safe.
                              const char* zName,
                              uint32_t ttl,
                              uint32_t maxCount,
                              uint64_t maxSize,
                              int argc, char* argv[])
{
    ss_dassert(zName);

    if (maxCount != 0)
    {
        MXS_WARNING("A maximum item count of %u specifed, although 'storage_rocksdb' "
                    "does not enforce such a limit.", (unsigned int)maxCount);
    }

    if (maxSize != 0)
    {
        MXS_WARNING("A maximum size of %lu specified, although 'storage_rocksdb' "
                    "does not enforce such a limit.", (unsigned long)maxSize);
    }

    unique_ptr<RocksDBStorage> sStorage;

    MXS_EXCEPTION_GUARD(sStorage = RocksDBStorage::Create(zName, ttl, argc, argv));

    if (sStorage)
    {
        MXS_NOTICE("Storage module created.");
    }

    return reinterpret_cast<CACHE_STORAGE*>(sStorage.release());
}

void freeInstance(CACHE_STORAGE* pInstance)
{
    MXS_EXCEPTION_GUARD(delete reinterpret_cast<RocksDBStorage*>(pInstance));
}

cache_result_t getInfo(CACHE_STORAGE* pStorage,
                       uint32_t       what,
                       json_t**       ppInfo)
{
    ss_dassert(pStorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pStorage)->getInfo(what, ppInfo));

    return result;
}

cache_result_t getKey(const char* zDefaultDB,
                      const GWBUF* pQuery,
                      CACHE_KEY* pKey)
{
    // zDefaultDB may be NULL.
    ss_dassert(pQuery);
    ss_dassert(pKey);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = RocksDBStorage::GetKey(zDefaultDB, pQuery, pKey));

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

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pStorage)->getValue(pKey,
                                                                                       flags,
                                                                                       ppResult));

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

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pStorage)->putValue(pKey, pValue));

    return result;
}

cache_result_t delValue(CACHE_STORAGE* pStorage,
                        const CACHE_KEY* pKey)
{
    ss_dassert(pStorage);
    ss_dassert(pKey);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pStorage)->delValue(pKey));

    return result;
}

cache_result_t getHead(CACHE_STORAGE* pstorage,
                       CACHE_KEY* pkey,
                       GWBUF** pphead)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pstorage)->getHead(pkey, pphead));

    return result;
}

cache_result_t getTail(CACHE_STORAGE* pstorage,
                       CACHE_KEY* pkey,
                       GWBUF** pptail)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pstorage)->getTail(pkey, pptail));

    return result;
}

cache_result_t getSize(CACHE_STORAGE* pstorage,
                       uint64_t* psize)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pstorage)->getSize(psize));

    return result;
}


cache_result_t getItems(CACHE_STORAGE* pstorage,
                        uint64_t* pitems)
{
    ss_dassert(pstorage);

    cache_result_t result = CACHE_RESULT_ERROR;

    MXS_EXCEPTION_GUARD(result = reinterpret_cast<RocksDBStorage*>(pstorage)->getItems(pitems));

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
            getKey,
            freeInstance,
            getInfo,
            getValue,
            putValue,
            delValue,
            getHead,
            getTail,
            getSize,
            getItems
        };

    return &api;
}

}
