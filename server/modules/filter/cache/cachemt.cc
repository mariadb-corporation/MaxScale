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

#include "cachemt.h"
#include "storage.h"
#include "storagefactory.h"

CacheMT::CacheMT(const std::string& name,
                 const CACHE_CONFIG* pConfig,
                 CACHE_RULES* pRules,
                 StorageFactory* pFactory,
                 HASHTABLE* pPending,
                 Storage* pStorage)
    : CacheSimple(name, pConfig, pRules, pFactory, pPending, pStorage)
{
    spinlock_init(&m_lockPending);
}

CacheMT::~CacheMT()
{
}

CacheMT* CacheMT::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CacheMT* pCache = NULL;

    CACHE_RULES* pRules = NULL;
    HASHTABLE* pPending = NULL;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pPending, &pFactory))
    {
        pCache = Create(name, pConfig, pRules, pFactory, pPending);
    }

    return pCache;
}

// static
CacheMT* CacheMT::Create(const std::string& name, StorageFactory* pFactory, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);
    ss_dassert(pFactory);

    CacheMT* pCache = NULL;

    CACHE_RULES* pRules = NULL;
    HASHTABLE* pPending = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pPending))
    {
        pCache = Create(name, pConfig, pRules, pFactory, pPending);
    }

    return pCache;
}

bool CacheMT::mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    long k = hashOfKey(key);

    spinlock_acquire(&m_lockPending);
    bool rv = CacheSimple::mustRefresh(k, pSessionCache);
    spinlock_release(&m_lockPending);

    return rv;
}

void CacheMT::refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache)
{
    long k = hashOfKey(key);

    spinlock_acquire(&m_lockPending);
    CacheSimple::refreshed(k, pSessionCache);
    spinlock_release(&m_lockPending);
}

// static
CacheMT* CacheMT::Create(const std::string&  name,
                         const CACHE_CONFIG* pConfig,
                         CACHE_RULES*        pRules,
                         StorageFactory*     pFactory,
                         HASHTABLE*          pPending)
{
    CacheMT* pCache = NULL;

    uint32_t ttl = pConfig->ttl;
    int argc = pConfig->storage_argc;
    char** argv = pConfig->storage_argv;

    Storage* pStorage = pFactory->createStorage(CACHE_THREAD_MODEL_MT, name.c_str(), ttl, argc, argv);

    if (pStorage)
    {
        CPP_GUARD(pCache = new CacheMT(name,
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
