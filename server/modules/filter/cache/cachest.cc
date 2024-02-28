/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
#include "cachest.hh"
#include "storage.hh"
#include "storagefactory.hh"

using std::shared_ptr;

CacheST::CacheST(const std::string& name,
                 const CacheConfig* pConfig,
                 const CacheRules::SVector& sRules,
                 SStorageFactory sFactory,
                 Storage* pStorage)
    : CacheSimple(name, pConfig, sRules, sFactory, pStorage)
{
    MXB_NOTICE("Created single threaded cache.");
}

CacheST::~CacheST()
{
}

CacheST* CacheST::create(const std::string& name,
                         const CacheRules::SVector& sRules,
                         const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CacheST* pCache = NULL;

    StorageFactory* pFactory = NULL;

    if (CacheSimple::get_storage_factory(pConfig, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = create(name, pConfig, sRules, sFactory);
    }

    return pCache;
}

// static
CacheST* CacheST::create(const std::string& name,
                         const CacheRules::SVector& sRules,
                         SStorageFactory sFactory,
                         const CacheConfig* pConfig)
{
    mxb_assert(sFactory.get());
    mxb_assert(pConfig);

    return create(name, pConfig, sRules, sFactory);
}

json_t* CacheST::get_info(uint32_t flags) const
{
    return CacheSimple::do_get_info(flags);
}

bool CacheST::must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    return CacheSimple::do_must_refresh(key, pSession);
}

void CacheST::refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    CacheSimple::do_refreshed(key, pSession);
}

CacheRules::SVector CacheST::all_rules() const
{
    return m_sRules;
}

void CacheST::set_all_rules(const CacheRules::SVector& sRules)
{
    // Can't mxb_assert(mxs::MainWorker::is_current()), as this will be called
    // indirectly by CachePT in the routing worker of the CacheST.

    m_sRules = sRules;
}

// static
CacheST* CacheST::create(const std::string& name,
                         const CacheConfig* pConfig,
                         const CacheRules::SVector& sRules,
                         SStorageFactory sFactory)
{
    CacheST* pCache = NULL;

    Storage::Config storage_config(CACHE_THREAD_MODEL_ST,
                                   pConfig->hard_ttl.count(),
                                   pConfig->soft_ttl.count(),
                                   pConfig->max_count,
                                   pConfig->max_size,
                                   pConfig->invalidate,
                                   pConfig->timeout);

    const auto& storage_params = pConfig->storage_params;

    Storage* pStorage = sFactory->create_storage(name.c_str(), storage_config, storage_params);

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
