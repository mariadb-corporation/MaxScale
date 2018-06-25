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

template<class StorageType>
class StorageModule
{
public:
    static bool initialize(uint32_t* pCapabilities)
    {
        return StorageType::Initialize(pCapabilities);
    }

    static CACHE_STORAGE* createInstance(const char* zName,
                                         const CACHE_STORAGE_CONFIG* pConfig,
                                         int argc, char* argv[])
    {
        ss_dassert(zName);
        ss_dassert(pConfig);

        StorageType* pStorage = NULL;

        MXS_EXCEPTION_GUARD(pStorage = StorageType::Create_instance(zName, *pConfig, argc, argv));

        return reinterpret_cast<CACHE_STORAGE*>(pStorage);
    }

    static void freeInstance(CACHE_STORAGE* pInstance)
    {
        MXS_EXCEPTION_GUARD(delete reinterpret_cast<StorageType*>(pInstance));
    }

    static void getConfig(CACHE_STORAGE* pCache_storage,
                          CACHE_STORAGE_CONFIG* pConfig)
    {
        ss_dassert(pCache_storage);
        ss_dassert(pConfig);

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(pStorage->get_config(pConfig));
    }

    static cache_result_t getInfo(CACHE_STORAGE* pCache_storage,
                                  uint32_t       what,
                                  json_t**       ppInfo)
    {
        ss_dassert(pCache_storage);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_info(what, ppInfo));

        return result;
    }

    static cache_result_t getValue(CACHE_STORAGE* pCache_storage,
                                   const CACHE_KEY* pKey,
                                   uint32_t flags,
                                   uint32_t soft_ttl,
                                   uint32_t hard_ttl,
                                   GWBUF** ppResult)
    {
        ss_dassert(pCache_storage);
        ss_dassert(pKey);
        ss_dassert(ppResult);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_value(*pKey, flags, soft_ttl, hard_ttl, ppResult));

        return result;
    }

    static cache_result_t putValue(CACHE_STORAGE* pCache_storage,
                                   const CACHE_KEY* pKey,
                                   const GWBUF* pValue)
    {
        ss_dassert(pCache_storage);
        ss_dassert(pKey);
        ss_dassert(pValue);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->put_value(*pKey, *pValue));

        return result;
    }

    static cache_result_t delValue(CACHE_STORAGE* pCache_storage, const CACHE_KEY* pKey)
    {
        ss_dassert(pCache_storage);
        ss_dassert(pKey);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->del_value(*pKey));

        return result;
    }

    static cache_result_t getHead(CACHE_STORAGE* pCache_storage,
                                  CACHE_KEY* pKey,
                                  GWBUF** ppHead)
    {
        ss_dassert(pCache_storage);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_head(pKey, ppHead));

        return result;
    }

    static cache_result_t getTail(CACHE_STORAGE* pCache_storage,
                                  CACHE_KEY* pKey,
                                  GWBUF** ppTail)
    {
        ss_dassert(pCache_storage);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_tail(pKey, ppTail));

        return result;
    }

    static cache_result_t getSize(CACHE_STORAGE* pCache_storage, uint64_t* pSize)
    {
        ss_dassert(pCache_storage);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_size(pSize));

        return result;
    }

    static cache_result_t getItems(CACHE_STORAGE* pCache_storage, uint64_t* pItems)
    {
        ss_dassert(pCache_storage);

        cache_result_t result = CACHE_RESULT_ERROR;

        StorageType* pStorage = reinterpret_cast<StorageType*>(pCache_storage);

        MXS_EXCEPTION_GUARD(result = pStorage->get_items(pItems));

        return result;
    }

    static CACHE_STORAGE_API s_api;
};

template<class StorageType>
CACHE_STORAGE_API StorageModule<StorageType>::s_api =
{
    &StorageModule<StorageType>::initialize,
    &StorageModule<StorageType>::createInstance,
    &StorageModule<StorageType>::freeInstance,
    &StorageModule<StorageType>::getConfig,
    &StorageModule<StorageType>::getInfo,
    &StorageModule<StorageType>::getValue,
    &StorageModule<StorageType>::putValue,
    &StorageModule<StorageType>::delValue,
    &StorageModule<StorageType>::getHead,
    &StorageModule<StorageType>::getTail,
    &StorageModule<StorageType>::getSize,
    &StorageModule<StorageType>::getItems
};
