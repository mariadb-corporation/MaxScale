/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
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

    bool must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession);

    void refreshed(const CACHE_KEY& key, const CacheFilterSession* pSession);

    json_t* get_info(uint32_t what) const;

    cache_result_t get_key(const std::string& user,
                           const std::string& host,
                           const char* zDefault_db,
                           const GWBUF* pQuery,
                           CACHE_KEY* pKey) const override;

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue) const override;

    cache_result_t put_value(const CACHE_KEY& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue) override;

    cache_result_t del_value(const CACHE_KEY& key) override;

    cache_result_t invalidate(const std::vector<std::string>& words) override;

    cache_result_t clear() override;

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
