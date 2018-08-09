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

#include <maxscale/ccdefs.hh>
#include <memory>
#include <vector>
#include "cache.hh"

class CachePT : public Cache
{
public:
    ~CachePT();

    static CachePT* Create(const std::string& name, const CACHE_CONFIG* pConfig);

    bool must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession);

    void refreshed(const CACHE_KEY& key, const CacheFilterSession* pSession);

    json_t* get_info(uint32_t what) const;

    cache_result_t get_key(const char* zDefault_db, const GWBUF* pQuery, CACHE_KEY* pKey) const;

    cache_result_t get_value(const CACHE_KEY& key,
                             uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                             GWBUF** ppValue) const;

    cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue);

    cache_result_t del_value(const CACHE_KEY& key);

private:
    typedef std::shared_ptr<Cache>      SCache;
    typedef std::vector<SCache>         Caches;

    CachePT(const std::string&              name,
            const CACHE_CONFIG*             pConfig,
            const std::vector<SCacheRules>& rules,
            SStorageFactory                 sFactory,
            const Caches&                   caches);

    static CachePT* Create(const std::string&              name,
                           const CACHE_CONFIG*             pConfig,
                           const std::vector<SCacheRules>& rules,
                           SStorageFactory                 sFactory);

    Cache& thread_cache();

    const Cache& thread_cache() const
    {
        return const_cast<CachePT*>(this)->thread_cache();
    }

private:
    CachePT(const Cache&);
    CachePT& operator = (const CachePT&);

private:
    Caches m_caches;
};
