#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include "cachesimple.hh"

class CacheST : public CacheSimple
{
public:
    ~CacheST();

    static CacheST* Create(const std::string& name, const CACHE_CONFIG* pConfig);
    static CacheST* Create(const std::string& name,
                           const std::vector<SCacheRules>& rules,
                           SStorageFactory sFactory,
                           const CACHE_CONFIG* pConfig);

    json_t* get_info(uint32_t what) const;

    bool must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession);

    void refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession);

private:
    CacheST(const std::string&              name,
            const CACHE_CONFIG*             pConfig,
            const std::vector<SCacheRules>& rules,
            SStorageFactory                 sFactory,
            Storage*                        pStorage);

    static CacheST* Create(const std::string&              name,
                           const CACHE_CONFIG*             pConfig,
                           const std::vector<SCacheRules>& rules,
                           SStorageFactory                 sFactory);
private:
    CacheST(const CacheST&);
    CacheST& operator = (const CacheST&);
};
