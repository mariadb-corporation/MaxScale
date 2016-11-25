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
#include "cachesimple.h"

class CacheST : public CacheSimple
{
public:
    ~CacheST();

    static CacheST* Create(const char* zName, CACHE_CONFIG& config);

    bool mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache);

    void refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache);

private:
    CacheST(const char* zName,
            CACHE_CONFIG& config,
            CACHE_RULES* pRules,
            StorageFactory* pFactory,
            Storage* pStorage,
            HASHTABLE* pPending);

private:
    CacheST(const CacheST&);
    CacheST& operator = (const CacheST&);
};
