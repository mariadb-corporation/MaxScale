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

#include "cachept.h"
#include <maxscale/atomic.h>
#include <maxscale/platform.h>
#include "cachest.h"
#include "storagefactory.h"

using std::tr1::shared_ptr;
using std::string;

namespace
{

int u_current_thread_id = 0;
thread_local int u_thread_id = -1;

/**
 * Get the thread index of the current thread.
 *
 * @return The index of the current thread.
 */
inline int thread_index()
{
    // A value of -1 indicates that the value has not been initialized,
    if (u_thread_id == -1)
    {
        u_thread_id = atomic_add(&u_current_thread_id, 1);
    }

    return u_thread_id;
}

}

CachePT::CachePT(const std::string&  name,
                 const CACHE_CONFIG* pConfig,
                 CACHE_RULES*        pRules,
                 StorageFactory*     pFactory,
                 const Caches&       caches)
    : Cache(name, pConfig, pRules, pFactory)
    , m_caches(caches)
{
}

CachePT::~CachePT()
{
}

// static
CachePT* CachePT::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CachePT* pCache = NULL;

    CACHE_RULES* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (Cache::Create(*pConfig, &pRules, &pFactory))
    {
        pCache = Create(name, pConfig, pRules, pFactory);
    }

    return pCache;
}

// static
CachePT* CachePT::Create(const std::string& name,
                         StorageFactory* pFactory,
                         const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CachePT* pCache = NULL;

    CACHE_RULES* pRules = NULL;

    if (Cache::Create(*pConfig, &pRules))
    {
        pCache = Create(name, pConfig, pRules, pFactory);
    }

    return pCache;
}

bool CachePT::mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    return threadCache().mustRefresh(key, pSessionCache);
}

void CachePT::refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache)
{
    threadCache().refreshed(key, pSessionCache);
}

cache_result_t CachePT::getKey(const char* zDefaultDb, const GWBUF* pQuery, CACHE_KEY* pKey)
{
    return threadCache().getKey(zDefaultDb, pQuery, pKey);
}

cache_result_t CachePT::getValue(const CACHE_KEY& key, uint32_t flags, GWBUF** ppValue)
{
    return threadCache().getValue(key, flags, ppValue);
}

cache_result_t CachePT::putValue(const CACHE_KEY& key, const GWBUF* pValue)
{
    return threadCache().putValue(key, pValue);
}

cache_result_t CachePT::delValue(const CACHE_KEY& key)
{
    return threadCache().delValue(key);
}

// static
CachePT* CachePT::Create(const std::string&  name,
                         const CACHE_CONFIG* pConfig,
                         CACHE_RULES*        pRules,
                         StorageFactory*     pFactory)
{
    CachePT* pCache = NULL;

    try
    {
        int n_threads = config_threadcount();

        Caches caches;

        bool error = false;
        int i = 0;

        while (!error && (i < n_threads))
        {
            char suffix[6]; // Enough for 99999 threads
            sprintf(suffix, "%d", i);

            string namest(name + "-" + suffix);

            CacheST* pCacheST = 0;

            CPP_GUARD(pCacheST = CacheST::Create(namest, pFactory, pConfig));

            if (pCacheST)
            {
                shared_ptr<Cache> sCache(pCacheST);

                caches.push_back(sCache);
            }
            else
            {
                error = true;
            }

            ++i;
        }

        if (!error)
        {
            pCache = new CachePT(name, pConfig, pRules, pFactory, caches);
        }
    }
    catch (const std::exception&)
    {
        cache_rules_free(pRules);
        delete pFactory;
    }

    return pCache;
}

Cache& CachePT::threadCache()
{
    int i = thread_index();
    ss_dassert(i < (int)m_caches.size());
    return *m_caches[i].get();
}
