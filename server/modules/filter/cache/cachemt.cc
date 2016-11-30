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
#include "cachemt.h"
#include "storage.h"
#include "storagefactory.h"

CacheMT::CacheMT(const std::string&  name,
                 const CACHE_CONFIG* pConfig,
                 CacheRules*         pRules,
                 StorageFactory*     pFactory,
                 Storage*            pStorage)
    : CacheSimple(name, pConfig, pRules, pFactory, pStorage)
{
    spinlock_init(&m_lockPending);

    MXS_NOTICE("Created multi threaded cache.");
}

CacheMT::~CacheMT()
{
}

CacheMT* CacheMT::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CacheMT* pCache = NULL;

    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pFactory))
    {
        pCache = Create(name, pConfig, pRules, pFactory);
    }

    return pCache;
}

// static
CacheMT* CacheMT::Create(const std::string& name, StorageFactory* pFactory, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);
    ss_dassert(pFactory);

    CacheMT* pCache = NULL;

    CacheRules* pRules = NULL;

    if (CacheSimple::Create(*pConfig, &pRules))
    {
        pCache = Create(name, pConfig, pRules, pFactory);
    }

    return pCache;
}

bool CacheMT::must_refresh(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    LockGuard guard(&m_lockPending);

    return do_must_refresh(key, pSessionCache);
}

void CacheMT::refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache)
{
    LockGuard guard(&m_lockPending);

    do_refreshed(key, pSessionCache);
}

// static
CacheMT* CacheMT::Create(const std::string&  name,
                         const CACHE_CONFIG* pConfig,
                         CacheRules*         pRules,
                         StorageFactory*     pFactory)
{
    CacheMT* pCache = NULL;

    uint32_t ttl = pConfig->ttl;
    uint32_t maxCount = pConfig->max_count;
    uint32_t maxSize = pConfig->max_size;

    int argc = pConfig->storage_argc;
    char** argv = pConfig->storage_argv;

    Storage* pStorage = pFactory->createStorage(CACHE_THREAD_MODEL_MT, name.c_str(),
                                                ttl, maxCount, maxSize,
                                                argc, argv);

    if (pStorage)
    {
        CPP_GUARD(pCache = new CacheMT(name,
                                       pConfig,
                                       pRules,
                                       pFactory,
                                       pStorage));

        if (!pCache)
        {
            delete pStorage;
            delete pRules;
            delete pFactory;
        }
    }

    return pCache;
}
