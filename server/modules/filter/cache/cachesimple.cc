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

#define MXS_MODULE_NAME "cache"
#include "cachesimple.h"
#include "storage.h"
#include "storagefactory.h"

CacheSimple::CacheSimple(const std::string&  name,
                         const CACHE_CONFIG* pConfig,
                         SCacheRules         sRules,
                         SStorageFactory     sFactory,
                         Storage*            pStorage)
    : Cache(name, pConfig, sRules, sFactory)
    , m_pStorage(pStorage)
{
}

CacheSimple::~CacheSimple()
{
    delete m_pStorage;
}

// static
bool CacheSimple::Create(const CACHE_CONFIG& config,
                         CacheRules**        ppRules,
                         StorageFactory**    ppFactory)
{
    int rv = false;

    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (Cache::Create(config, &pRules, &pFactory))
    {
        *ppRules = pRules;
        *ppFactory = pFactory;
    }

    return pRules != NULL;
}

cache_result_t CacheSimple::get_key(const char* zDefaultDb,
                                    const GWBUF* pQuery,
                                    CACHE_KEY* pKey)
{
    return m_pStorage->get_key(zDefaultDb, pQuery, pKey);
}

cache_result_t CacheSimple::get_value(const CACHE_KEY& key,
                                      uint32_t flags,
                                      GWBUF** ppValue)
{
    return m_pStorage->get_value(key, flags, ppValue);
}

cache_result_t CacheSimple::put_value(const CACHE_KEY& key,
                                      const GWBUF* pValue)
{
    return m_pStorage->put_value(key, pValue);
}

cache_result_t CacheSimple::del_value(const CACHE_KEY& key)
{
    return m_pStorage->del_value(key);
}

// protected
bool CacheSimple::do_must_refresh(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    bool rv = false;
    Pending::iterator i = m_pending.find(key);

    if (i == m_pending.end())
    {
        try
        {
            m_pending.insert(std::make_pair(key, pSessionCache));
            rv = true;
        }
        catch (const std::exception& x)
        {
            rv = false;
        }
    }

    return rv;
}

// protected
void CacheSimple::do_refreshed(const CACHE_KEY& key, const SessionCache* pSessionCache)
{
    Pending::iterator i = m_pending.find(key);
    ss_dassert(i != m_pending.end());
    ss_dassert(i->second == pSessionCache);
    m_pending.erase(i);
}
