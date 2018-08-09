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
#include <maxscale/spinlock.hh>
#include "cachesimple.hh"

class CacheMT : public CacheSimple
{
public:
    ~CacheMT();

    static CacheMT* Create(const std::string& name, const CACHE_CONFIG* pConfig);

    json_t* get_info(uint32_t what) const;

    bool must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession);

    void refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession);

private:
    CacheMT(const std::string&              name,
            const CACHE_CONFIG*             pConfig,
            const std::vector<SCacheRules>& rules,
            SStorageFactory                 sFactory,
            Storage*                        pStorage);

    static CacheMT* Create(const std::string&              name,
                           const CACHE_CONFIG*             pConfig,
                           const std::vector<SCacheRules>& rules,
                           SStorageFactory                 sFactory);

private:
    CacheMT(const CacheMT&);
    CacheMT& operator = (const CacheMT&);

private:
    mutable SPINLOCK m_lock_pending; // Lock used for protecting 'pending'.
};
