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
#include <maxscale/spinlock.h>
#include "cache.h"

class CacheMT : public Cache
{
public:
    ~CacheMT();

    static CacheMT* Create(const char* zName, CACHE_CONFIG& config);

    bool mustRefresh(const CACHE_KEY& key, const SessionCache* pSessionCache);

    void refreshed(const CACHE_KEY& key,  const SessionCache* pSessionCache);

private:
    CacheMT(const char* zName,
            CACHE_CONFIG& config,
            CACHE_RULES* pRules,
            StorageFactory* pFactory,
            Storage* pStorage,
            HASHTABLE* pPending);

private:
    CacheMT(const CacheMT&);
    CacheMT& operator = (const CacheMT&);

private:
    SPINLOCK m_lockPending; // Lock used for protecting 'pending'.
};
