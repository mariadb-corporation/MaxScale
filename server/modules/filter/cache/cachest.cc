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
#include "cachest.hh"
#include "storage.hh"
#include "storagefactory.hh"

using std::tr1::shared_ptr;

CacheST::CacheST(const std::string&  name,
                 const CACHE_CONFIG* pConfig,
                 SCacheRules         sRules,
                 SStorageFactory     sFactory,
                 Storage*            pStorage)
    : CacheSimple(name, pConfig, sRules, sFactory, pStorage)
{
    MXS_NOTICE("Created single threaded cache.");
}

CacheST::~CacheST()
{
}

CacheST* CacheST::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CacheST* pCache = NULL;

    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::Create(*pConfig, &pRules, &pFactory))
    {
        shared_ptr<CacheRules> sRules(pRules);
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = Create(name, pConfig, sRules, sFactory);
    }

    return pCache;
}

// static
CacheST* CacheST::Create(const std::string&  name,
                         SCacheRules         sRules,
                         SStorageFactory     sFactory,
                         const CACHE_CONFIG* pConfig)
{
    ss_dassert(sRules.get());
    ss_dassert(sFactory.get());
    ss_dassert(pConfig);

    return Create(name, pConfig, sRules, sFactory);
}

json_t* CacheST::get_info(uint32_t flags) const
{
    return CacheSimple::do_get_info(flags);
}

bool CacheST::must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession)
{
    return CacheSimple::do_must_refresh(key, pSession);
}

void CacheST::refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession)
{
    CacheSimple::do_refreshed(key, pSession);
}

// static
CacheST* CacheST::Create(const std::string&  name,
                         const CACHE_CONFIG* pConfig,
                         SCacheRules         sRules,
                         SStorageFactory     sFactory)
{
    CacheST* pCache = NULL;

    CacheStorageConfig storage_config(CACHE_THREAD_MODEL_ST,
                                      pConfig->hard_ttl,
                                      pConfig->soft_ttl,
                                      pConfig->max_count,
                                      pConfig->max_size);

    int argc = pConfig->storage_argc;
    char** argv = pConfig->storage_argv;

    Storage* pStorage = sFactory->createStorage(name.c_str(), storage_config, argc, argv);

    if (pStorage)
    {
        MXS_EXCEPTION_GUARD(pCache = new CacheST(name,
                                                 pConfig,
                                                 sRules,
                                                 sFactory,
                                                 pStorage));

        if (!pCache)
        {
            delete pStorage;
        }
    }

    return pCache;
}
