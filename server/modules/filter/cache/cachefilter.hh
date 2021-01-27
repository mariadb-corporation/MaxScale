/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <limits.h>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include "rules.hh"
#include "cache.hh"
#include "cachefiltersession.hh"
#include "cacheconfig.hh"


class CacheFilter : public mxs::Filter
{
public:
    ~CacheFilter();

    static CacheFilter* create(const char* zName, mxs::ConfigParameters* ppParams);

    Cache& cache()
    {
        mxb_assert(m_sCache.get());
        return *m_sCache.get();
    }
    const Cache& cache() const
    {
        mxb_assert(m_sCache.get());
        return *m_sCache.get();
    }

    CacheFilterSession* newSession(MXS_SESSION* pSession, SERVICE* pService);

    json_t* diagnostics() const;

    uint64_t getCapabilities() const;

    mxs::config::Configuration* getConfiguration()
    {
        return m_sConfig.get();
    }

private:
    CacheFilter(std::unique_ptr<CacheConfig> sConfig, std::unique_ptr<Cache> sCache);

    CacheFilter(const CacheFilter&);
    CacheFilter& operator=(const CacheFilter&);

private:
    std::unique_ptr<CacheConfig> m_sConfig;
    std::unique_ptr<Cache>       m_sCache;
};
