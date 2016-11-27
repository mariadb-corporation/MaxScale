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

#include "cachest.h"
#include "storage.h"
#include "storagefactory.h"

CacheST::CacheST(const char* zName,
                 const CACHE_CONFIG* pConfig,
                 CACHE_RULES* pRules,
                 StorageFactory* pFactory,
                 HASHTABLE* pPending,
                 Storage* pStorage)
    : CacheSimple(zName, pConfig, pRules, pFactory, pPending, pStorage)
{
}

CacheST::~CacheST()
{
}

CacheST* CacheST::Create(const char* zName, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CacheST* pCache = NULL;

    CACHE_RULES* pRules = NULL;
    HASHTABLE* pPending = NULL;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pPending, &pFactory))
    {
        pCache = Create(zName, pConfig, pRules, pFactory, pPending);
    }

    return pCache;
}

// static
CacheST* CacheST::Create(const char* zName, StorageFactory* pFactory, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);
    ss_dassert(pFactory);

    CacheST* pCache = NULL;

    CACHE_RULES* pRules = NULL;
    HASHTABLE* pPending = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pPending))
    {
        pCache = Create(zName, pConfig, pRules, pFactory, pPending);
    }

    return pCache;
}

bool CacheST::mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    long k = hashOfKey(key);

    return CacheSimple::mustRefresh(k, pSessionCache);
}

void CacheST::refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache)
{
    long k = hashOfKey(key);

    CacheSimple::refreshed(k, pSessionCache);
}

// statis
CacheST* CacheST::Create(const char*         zName,
                         const CACHE_CONFIG* pConfig,
                         CACHE_RULES*        pRules,
                         StorageFactory*     pFactory,
                         HASHTABLE*          pPending)
{
    CacheST* pCache = NULL;

    uint32_t ttl = pConfig->ttl;
    int argc = pConfig->storage_argc;
    char** argv = pConfig->storage_argv;

    Storage* pStorage = pFactory->createStorage(CACHE_THREAD_MODEL_ST, zName, ttl, argc, argv);

    if (pStorage)
    {
        CPP_GUARD(pCache = new CacheST(zName,
                                       pConfig,
                                       pRules,
                                       pFactory,
                                       pPending,
                                       pStorage));

        if (!pCache)
        {
            delete pStorage;
            cache_rules_free(pRules);
            hashtable_free(pPending);
            delete pFactory;
        }
    }

    return pCache;
}
