/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "cachesimple.hh"

class CacheST : public CacheSimple
{
public:
    ~CacheST();

    static CacheST* create(const std::string& name,
                           const CacheRules::SVector& sRules,
                           const CacheConfig* pConfig);
    static CacheST* create(const std::string& name,
                           const CacheRules::SVector& sRules,
                           SStorageFactory sFactory,
                           const CacheConfig* pConfig);

    json_t* get_info(uint32_t what) const override final;

    bool must_refresh(const CacheKey& key, const CacheFilterSession* pSession) override final;

    void refreshed(const CacheKey& key, const CacheFilterSession* pSession) override final;

    CacheRules::SVector all_rules() const override final;

    void set_all_rules(const CacheRules::SVector& sRules) override final;

private:
    CacheST(const std::string& name,
            const CacheConfig* pConfig,
            const CacheRules::SVector& sRules,
            SStorageFactory sFactory,
            Storage* pStorage);

    static CacheST* create(const std::string& name,
                           const CacheConfig* pConfig,
                           const CacheRules::SVector& sRules,
                           SStorageFactory sFactory);
private:
    CacheST(const CacheST&);
    CacheST& operator=(const CacheST&);
};
