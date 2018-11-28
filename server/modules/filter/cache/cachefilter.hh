/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>
#include <limits.h>
#include <maxscale/filter.hh>
#include "rules.hh"
#include "cache.hh"
#include "cachefiltersession.hh"


class CacheFilter : public maxscale::Filter<CacheFilter, CacheFilterSession>
{
public:
    ~CacheFilter();

    static CacheFilter* create(const char* zName, MXS_CONFIG_PARAMETER* ppParams);

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

    CacheFilterSession* newSession(MXS_SESSION* pSession);

    void    diagnostics(DCB* pDcb);
    json_t* diagnostics_json() const;

    uint64_t getCapabilities();

private:
    CacheFilter();

    CacheFilter(const CacheFilter&);
    CacheFilter& operator=(const CacheFilter&);

    static bool process_params(MXS_CONFIG_PARAMETER* ppParams, CACHE_CONFIG& config);

private:
    CACHE_CONFIG         m_config;
    std::auto_ptr<Cache> m_sCache;
};
