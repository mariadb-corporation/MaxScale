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
#include <maxscale/hashtable.h>
#include "cache.h"

class Storage;

class CacheSimple : public Cache
{
public:
    ~CacheSimple();

    cache_result_t get_key(const char* zDefaultDb, const GWBUF* pQuery, CACHE_KEY* pKey);

    cache_result_t get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppValue);

    cache_result_t put_value(const CACHE_KEY& key, const GWBUF* pValue);

    cache_result_t del_value(const CACHE_KEY& key);

protected:
    CacheSimple(const std::string&  name,
                const CACHE_CONFIG* pConfig,
                CACHE_RULES*        pRules,
                StorageFactory*     pFactory,
                HASHTABLE*          pPending,
                Storage*            pStorage);

    static bool Create(const CACHE_CONFIG& config,
                       CACHE_RULES**       ppRules,
                       HASHTABLE**         ppPending);

    static bool Create(const CACHE_CONFIG& config,
                       CACHE_RULES**       ppRules,
                       HASHTABLE**         ppPending,
                       StorageFactory**    ppFactory);


    long hash_of_key(const CACHE_KEY& key);

    bool must_refresh(long key, const SessionCache* pSessionCache);

    void refreshed(long key, const SessionCache* pSessionCache);

private:
    CacheSimple(const Cache&);
    CacheSimple& operator = (const CacheSimple&);

    static bool Create(HASHTABLE** ppPending);

protected:
    HASHTABLE* m_pPending;  // Pending items; being fetched from the backend.
    Storage*   m_pStorage;  // The storage instance to use.
};
