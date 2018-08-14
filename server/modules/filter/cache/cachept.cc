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

#define MXS_MODULE_NAME "cache"
#include "cachept.hh"

#include <maxbase/atomic.h>
#include <maxscale/config.h>
#include <maxscale/platform.h>

#include "cachest.hh"
#include "storagefactory.hh"

using std::shared_ptr;
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

CachePT::CachePT(const std::string&              name,
                 const CACHE_CONFIG*             pConfig,
                 const std::vector<SCacheRules>& rules,
                 SStorageFactory                 sFactory,
                 const Caches&                   caches)
    : Cache(name, pConfig, rules, sFactory)
    , m_caches(caches)
{
    MXS_NOTICE("Created cache per thread.");
}

CachePT::~CachePT()
{
}

// static
CachePT* CachePT::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CachePT* pCache = NULL;

    std::vector<SCacheRules> rules;
    StorageFactory* pFactory = NULL;

    if (Cache::Create(*pConfig, &rules, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = Create(name, pConfig, rules, sFactory);
    }

    return pCache;
}

bool CachePT::must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession)
{
    return thread_cache().must_refresh(key, pSession);
}

void CachePT::refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession)
{
    thread_cache().refreshed(key, pSession);
}

json_t* CachePT::get_info(uint32_t what) const
{
    json_t* pInfo = Cache::do_get_info(what);

    if (pInfo)
    {
        if (what & (INFO_PENDING | INFO_STORAGE))
        {
            what &= ~INFO_RULES; // The rules are the same, we don't want them duplicated.

            for (size_t i = 0; i < m_caches.size(); ++i)
            {
                char key[20]; // Surely enough.
                sprintf(key, "thread-%u", (unsigned int)i + 1);

                SCache sCache = m_caches[i];

                json_t* pThreadInfo = sCache->get_info(what);

                if (pThreadInfo)
                {
                    json_object_set(pInfo, key, pThreadInfo);
                    json_decref(pThreadInfo);
                }
            }
        }
    }

    return pInfo;
}

cache_result_t CachePT::get_key(const char* zDefault_db, const GWBUF* pQuery, CACHE_KEY* pKey) const
{
    return thread_cache().get_key(zDefault_db, pQuery, pKey);
}

cache_result_t CachePT::get_value(const CACHE_KEY& key,
                                  uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                                  GWBUF** ppValue) const
{
    return thread_cache().get_value(key, flags, soft_ttl, hard_ttl, ppValue);
}

cache_result_t CachePT::put_value(const CACHE_KEY& key, const GWBUF* pValue)
{
    return thread_cache().put_value(key, pValue);
}

cache_result_t CachePT::del_value(const CACHE_KEY& key)
{
    return thread_cache().del_value(key);
}

// static
CachePT* CachePT::Create(const std::string&              name,
                         const CACHE_CONFIG*             pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory                 sFactory)
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
            char suffix[12]; // Enough for 99999 threads
            sprintf(suffix, "%d", i);

            string namest(name + "-" + suffix);

            CacheST* pCacheST = 0;

            MXS_EXCEPTION_GUARD(pCacheST = CacheST::Create(namest, rules, sFactory, pConfig));

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
            pCache = new CachePT(name, pConfig, rules, sFactory, caches);
        }
    }
    catch (const std::exception&)
    {
    }

    return pCache;
}

Cache& CachePT::thread_cache()
{
    int i = thread_index();
    ss_dassert(i < (int)m_caches.size());
    return *m_caches[i].get();
}
