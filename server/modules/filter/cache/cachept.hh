/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <vector>
#include "cache.hh"

class CachePT : public Cache
{
public:
    ~CachePT();

    static CachePT* create(const std::string& name, const CacheConfig* pConfig);

    bool create_token(std::shared_ptr<Token>* psToken) override;

    bool must_refresh(const CacheKey& key, const CacheFilterSession* pSession);

    void refreshed(const CacheKey& key, const CacheFilterSession* pSession);

    void get_limits(Storage::Limits* pLimits) const override final;

    json_t* get_info(uint32_t what) const override final;

    cache_result_t get_key(const std::string& user,
                           const std::string& host,
                           const char* zDefault_db,
                           const GWBUF* pQuery,
                           CacheKey* pKey) const override final;

    cache_result_t get_value(Token* pToken,
                             const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
                             const std::function<void (cache_result_t, GWBUF*)>& cb) const override final;

    cache_result_t put_value(Token* pToken,
                             const CacheKey& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             const std::function<void (cache_result_t)>& cb) override final;

    cache_result_t del_value(Token* pToken,
                             const CacheKey& key,
                             const std::function<void (cache_result_t)>& cb) override final;

    cache_result_t invalidate(Token* pToken,
                              const std::vector<std::string>& words,
                              const std::function<void (cache_result_t)>& cb) override final;

    cache_result_t clear(Token* pToken) override final;

private:
    typedef std::shared_ptr<Cache> SCache;
    typedef std::vector<SCache>    Caches;

    CachePT(const std::string& name,
            const CacheConfig* pConfig,
            const std::vector<SCacheRules>& rules,
            SStorageFactory sFactory,
            const Caches& caches);

    static CachePT* create(const std::string& name,
                           const CacheConfig* pConfig,
                           const std::vector<SCacheRules>& rules,
                           SStorageFactory sFactory);

    Cache& thread_cache();

    const Cache& thread_cache() const
    {
        return const_cast<CachePT*>(this)->thread_cache();
    }

private:
    CachePT(const Cache&);
    CachePT& operator=(const CachePT&);

private:
    Caches m_caches;
};
