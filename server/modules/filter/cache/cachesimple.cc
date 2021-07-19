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
#include "cachesimple.hh"
#include "storage.hh"
#include "storagefactory.hh"

CacheSimple::CacheSimple(const std::string& name,
                         const CacheConfig* pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory sFactory,
                         Storage* pStorage)
    : Cache(name, pConfig, rules, sFactory)
    , m_pStorage(pStorage)
{
}

CacheSimple::~CacheSimple()
{
    delete m_pStorage;
}

bool CacheSimple::create_token(std::shared_ptr<Cache::Token>* psToken)
{
    return m_pStorage->create_token(psToken);
}

void CacheSimple::get_limits(Storage::Limits* pLimits) const
{
    m_pStorage->get_limits(pLimits);
}

cache_result_t CacheSimple::get_value(Token* pToken,
                                      const CacheKey& key,
                                      uint32_t flags,
                                      uint32_t soft_ttl,
                                      uint32_t hard_ttl,
                                      GWBUF** ppValue,
                                      const std::function<void (cache_result_t, GWBUF*)>& cb) const
{
    return m_pStorage->get_value(pToken, key, flags, soft_ttl, hard_ttl, ppValue, cb);
}

cache_result_t CacheSimple::put_value(Token* pToken,
                                      const CacheKey& key,
                                      const std::vector<std::string>& invalidation_words,
                                      const GWBUF* pValue,
                                      const std::function<void (cache_result_t)>& cb)
{
    return m_pStorage->put_value(pToken, key, invalidation_words, pValue, cb);
}

cache_result_t CacheSimple::del_value(Token* pToken,
                                      const CacheKey& key,
                                      const std::function<void (cache_result_t)>& cb)
{
    return m_pStorage->del_value(pToken, key, cb);
}

cache_result_t CacheSimple::invalidate(Token* pToken,
                                       const std::vector<std::string>& words,
                                       const std::function<void (cache_result_t)>& cb)
{
    return m_pStorage->invalidate(pToken, words, cb);
}

cache_result_t CacheSimple::clear(Token* pToken)
{
    return m_pStorage->clear(pToken);
}

// protected:
json_t* CacheSimple::do_get_info(uint32_t what) const
{
    json_t* pInfo = Cache::do_get_info(what);

    if (what & INFO_PENDING)
    {
        // TODO: Include information about pending items.
    }

    if (what & INFO_STORAGE)
    {
        json_t* pStorageInfo;

        cache_result_t result = m_pStorage->get_info(Storage::INFO_ALL, &pStorageInfo);

        if (CACHE_RESULT_IS_OK(result))
        {
            json_object_set_new(pInfo, "storage", pStorageInfo);
        }
    }

    return pInfo;
}

// protected
bool CacheSimple::do_must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    bool rv = false;
    Pending::iterator i = m_pending.find(key);

    if (i == m_pending.end())
    {
        try
        {
            m_pending.insert(std::make_pair(key, pSession));
            rv = true;
        }
        catch (const std::exception& x)
        {
            rv = false;
        }
    }

    return rv;
}

// protected
void CacheSimple::do_refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    Pending::iterator i = m_pending.find(key);
    mxb_assert(i != m_pending.end());
    mxb_assert(i->second == pSession);
    m_pending.erase(i);
}
