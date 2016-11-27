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

#include "cachesimple.h"
#include "storage.h"
#include "storagefactory.h"

namespace
{

// Initial size of hashtable used for storing keys of queries that
// are being fetches.
const size_t CACHE_PENDING_ITEMS = 50;

/**
 * Hashes a cache key to an integer.
 *
 * @param key Pointer to cache key.
 *
 * @returns Corresponding integer hash.
 */
int hash_of_key(const CACHE_KEY& key)
{
    int hash = 0;

    const char* i   = key.data;
    const char* end = i + CACHE_KEY_MAXLEN;

    while (i < end)
    {
        int c = *i;
        hash = c + (hash << 6) + (hash << 16) - hash;
        ++i;
    }

    return hash;
}

int hashfn(const void* address)
{
    // TODO: Hash the address; pointers are not evenly distributed.
    return (long)address;
}

int hashcmp(const void* address1, const void* address2)
{
    return (long)address2 - (long)address1;
}

}


CacheSimple::CacheSimple(const char*         zName,
                         const CACHE_CONFIG* pConfig,
                         CACHE_RULES*        pRules,
                         StorageFactory*     pFactory,
                         HASHTABLE*          pPending,
                         Storage*            pStorage)
    : Cache(zName, pConfig, pRules, pFactory)
    , m_pPending(pPending)
    , m_pStorage(pStorage)
{
}

CacheSimple::~CacheSimple()
{
    delete m_pStorage;
    hashtable_free(m_pPending);
}


// static
bool CacheSimple::Create(const CACHE_CONFIG& config,
                         CACHE_RULES**       ppRules,
                         HASHTABLE**         ppPending)
{
    int rv = false;

    CACHE_RULES* pRules = NULL;
    HASHTABLE* pPending = NULL;

    if (Cache::Create(config, &pRules) && Create(&pPending))
    {
        *ppRules = pRules;
        *ppPending = pPending;
    }
    else
    {
        cache_rules_free(pRules);
    }

    return pPending != NULL;;
}

// static
bool CacheSimple::Create(const CACHE_CONFIG& config,
                         CACHE_RULES**       ppRules,
                         HASHTABLE**         ppPending,
                         StorageFactory**    ppFactory)
{
    int rv = false;

    CACHE_RULES* pRules = NULL;
    StorageFactory* pFactory = NULL;
    HASHTABLE* pPending = NULL;

    if (Cache::Create(config, &pRules, &pFactory) && Create(&pPending))
    {
        *ppRules = pRules;
        *ppPending = pPending;
        *ppFactory = pFactory;
    }
    else
    {
        cache_rules_free(pRules);
        delete pFactory;
    }

    return pPending != NULL;
}

cache_result_t CacheSimple::getKey(const char* zDefaultDb,
                                   const GWBUF* pQuery,
                                   CACHE_KEY* pKey)
{
    return m_pStorage->getKey(zDefaultDb, pQuery, pKey);
}

cache_result_t CacheSimple::getValue(const CACHE_KEY& key,
                                     uint32_t flags,
                                     GWBUF** ppValue)
{
    return m_pStorage->getValue(key, flags, ppValue);
}

cache_result_t CacheSimple::putValue(const CACHE_KEY& key,
                                     const GWBUF* pValue)
{
    return m_pStorage->putValue(key, pValue);
}

cache_result_t CacheSimple::delValue(const CACHE_KEY& key)
{
    return m_pStorage->delValue(key);
}

// protected
long CacheSimple::hashOfKey(const CACHE_KEY& key)
{
    return hash_of_key(key);
}

// protected
bool CacheSimple::mustRefresh(long key, const SessionCache* pSessionCache)
{
    void *pValue = hashtable_fetch(m_pPending, (void*)key);
    if (!pValue)
    {
        // It's not being fetched, so we make a note that we are.
        hashtable_add(m_pPending, (void*)key, (void*)pSessionCache);
    }

    return !pValue;
}

// protected
void CacheSimple::refreshed(long key, const SessionCache* pSessionCache)
{
    ss_dassert(hashtable_fetch(m_pPending, (void*)key) == pSessionCache);
    ss_debug(int n =) hashtable_delete(m_pPending, (void*)key);
    ss_dassert(n == 1);
}

// static
bool CacheSimple::Create(HASHTABLE** ppPending)
{
    HASHTABLE* pPending = hashtable_alloc(CACHE_PENDING_ITEMS, hashfn, hashcmp);

    if (pPending)
    {
        *ppPending = pPending;
    }

    return pPending != NULL;
}
