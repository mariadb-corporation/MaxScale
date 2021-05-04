/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachept.hh"

#include <maxbase/atomic.h>
#include <maxscale/config.hh>

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

CachePT::CachePT(const std::string& name,
                 const CacheConfig* pConfig,
                 const std::vector<SCacheRules>& rules,
                 SStorageFactory sFactory,
                 const Caches& caches)
    : Cache(name, pConfig, rules, sFactory)
    , m_caches(caches)
{
    MXS_NOTICE("Created cache per thread.");
}

CachePT::~CachePT()
{
}

// static
CachePT* CachePT::create(const std::string& name, const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CachePT* pCache = NULL;

    std::vector<SCacheRules> rules;
    StorageFactory* pFactory = NULL;

    if (Cache::get_storage_factory(*pConfig, &rules, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = create(name, pConfig, rules, sFactory);
    }

    return pCache;
}

bool CachePT::create_token(std::shared_ptr<Cache::Token>* psToken)
{
    return thread_cache().create_token(psToken);
}

bool CachePT::must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    return thread_cache().must_refresh(key, pSession);
}

void CachePT::refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    thread_cache().refreshed(key, pSession);
}

void CachePT::get_limits(Storage::Limits* pLimits) const
{
    // We return the limits of the first thread Cache. The limits will be the
    // same for all.
    m_caches.front()->get_limits(pLimits);
}

json_t* CachePT::get_info(uint32_t what) const
{
    json_t* pInfo = Cache::do_get_info(what);

    if (pInfo)
    {
        if (what & (INFO_PENDING | INFO_STORAGE))
        {
            what &= ~INFO_RULES;    // The rules are the same, we don't want them duplicated.

            for (size_t i = 0; i < m_caches.size(); ++i)
            {
                char key[20];   // Surely enough.
                sprintf(key, "thread-%u", (unsigned int)i + 1);

                SCache sCache = m_caches[i];

                json_t* pThreadInfo = sCache->get_info(what);

                if (pThreadInfo)
                {
                    json_object_set_new(pInfo, key, pThreadInfo);
                }
            }
        }
    }

    return pInfo;
}

cache_result_t CachePT::get_key(const std::string& user,
                                const std::string& host,
                                const char* zDefault_db,
                                const GWBUF* pQuery,
                                CacheKey* pKey) const
{
    return thread_cache().get_key(user, host, zDefault_db, pQuery, pKey);
}

cache_result_t CachePT::get_value(Token* pToken,
                                  const CacheKey& key,
                                  uint32_t flags,
                                  uint32_t soft_ttl,
                                  uint32_t hard_ttl,
                                  GWBUF** ppValue,
                                  const std::function<void (cache_result_t, GWBUF*)>& cb) const
{
    return thread_cache().get_value(pToken, key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t CachePT::put_value(Token* pToken,
                                  const CacheKey& key,
                                  const std::vector<std::string>& invalidation_words,
                                  const GWBUF* pValue,
                                  const std::function<void (cache_result_t)>& cb)
{
    return thread_cache().put_value(pToken, key, invalidation_words, pValue, cb);
}

cache_result_t CachePT::del_value(Token* pToken,
                                  const CacheKey& key,
                                  const std::function<void (cache_result_t)>& cb)
{
    return thread_cache().del_value(pToken, key, cb);
}

cache_result_t CachePT::invalidate(Token* pToken,
                                   const std::vector<std::string>& words,
                                   const std::function<void (cache_result_t)>& cb)
{
    return thread_cache().invalidate(pToken, words, cb);
}

cache_result_t CachePT::clear(Token* pToken)
{
    return thread_cache().clear(pToken);
}

// static
CachePT* CachePT::create(const std::string& name,
                         const CacheConfig* pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory sFactory)
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
            char suffix[12];    // Enough for 99999 threads
            sprintf(suffix, "%d", i);

            string namest(name + "-" + suffix);

            CacheST* pCacheST = 0;

            MXS_EXCEPTION_GUARD(pCacheST = CacheST::create(namest, rules, sFactory, pConfig));

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
    mxb_assert(i < (int)m_caches.size());
    return *m_caches[i].get();
}
