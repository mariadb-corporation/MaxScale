#pragma once
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

#include <maxscale/cdefs.h>
#include <tr1/memory>
#include <vector>
#include "cache.h"

class CachePT : public Cache
{
public:
    ~CachePT();

    static CachePT* Create(const std::string& name, const CACHE_CONFIG* pConfig);
    static CachePT* Create(const std::string& name, StorageFactory* pFactory, const CACHE_CONFIG* pConfig);

    bool mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache);

    void refreshed(const CACHE_KEY& key, const SessionCache* pSessionCache);

    cache_result_t getKey(const char* zDefaultDb, const GWBUF* pQuery, CACHE_KEY* pKey);

    cache_result_t getValue(const CACHE_KEY& key, uint32_t flags, GWBUF** ppValue);

    cache_result_t putValue(const CACHE_KEY& key, const GWBUF* pValue);

    cache_result_t delValue(const CACHE_KEY& key);

private:
    typedef std::tr1::shared_ptr<Cache> SCache;
    typedef std::vector<SCache>         Caches;

    CachePT(const std::string&  name,
            const CACHE_CONFIG* pConfig,
            CACHE_RULES*        pRules,
            StorageFactory*     pFactory,
            const Caches&       caches);

    static CachePT* Create(const std::string&  name,
                           const CACHE_CONFIG* pConfig,
                           CACHE_RULES*        pRules,
                           StorageFactory*     pFactory);

    Cache& threadCache();

private:
    CachePT(const Cache&);
    CachePT& operator = (const CachePT&);

private:
    Caches m_caches;
};
