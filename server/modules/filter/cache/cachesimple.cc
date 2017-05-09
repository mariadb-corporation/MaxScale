/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachesimple.hh"
#include "storage.hh"
#include "storagefactory.hh"

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

cache_result_t CacheSimple::get_value(const CACHE_KEY& key,
                                      uint32_t flags,
                                      GWBUF** ppValue) const
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

// protected:
json_t* CacheSimple::do_get_info(uint32_t what) const
{
    json_t* pInfo = Cache::do_get_info(what);

    if (what & INFO_PENDING)
    {
        // TODO: Include information about pending items.
    }

    if (what & INFO_STORAGE)
    {
        json_t* pStorageInfo;

        cache_result_t result = m_pStorage->get_info(Storage::INFO_ALL, &pStorageInfo);

        if (CACHE_RESULT_IS_OK(result))
        {
            json_object_set(pInfo, "storage", pStorageInfo);
            json_decref(pStorageInfo);
        }
    }

    return pInfo;
}

// protected
bool CacheSimple::do_must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession)
{
    bool rv = false;
    Pending::iterator i = m_pending.find(key);

    if (i == m_pending.end())
    {
        try
        {
            m_pending.insert(std::make_pair(key, pSession));
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
void CacheSimple::do_refreshed(const CACHE_KEY& key, const CacheFilterSession* pSession)
{
    Pending::iterator i = m_pending.find(key);
    ss_dassert(i != m_pending.end());
    ss_dassert(i->second == pSession);
    m_pending.erase(i);
}
