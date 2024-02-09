/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
#include "cachemt.hh"
#include <maxscale/mainworker.hh>
#include "storage.hh"
#include "storagefactory.hh"

using std::shared_ptr;

CacheMT::CacheMT(const std::string& name,
                 const CacheConfig* pConfig,
                 const CacheRules::SVector& sRules,
                 SStorageFactory sFactory,
                 Storage* pStorage)
    : CacheSimple(name, pConfig, sRules, sFactory, pStorage)
{
    MXB_NOTICE("Created multi threaded cache.");
}

CacheMT::~CacheMT()
{
}

CacheMT* CacheMT::create(const std::string& name,
                         const CacheRules::SVector& sRules,
                         const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CacheMT* pCache = NULL;

    StorageFactory* pFactory = NULL;

    if (CacheSimple::get_storage_factory(pConfig, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = create(name, pConfig, sRules, sFactory);
    }

    return pCache;
}

json_t* CacheMT::get_info(uint32_t flags) const
{
    std::lock_guard<std::mutex> guard(m_lock_pending);

    return CacheSimple::do_get_info(flags);
}

bool CacheMT::must_refresh(const CacheKey& key, const CacheFilterSession* pSession)
{
    std::lock_guard<std::mutex> guard(m_lock_pending);

    return do_must_refresh(key, pSession);
}

void CacheMT::refreshed(const CacheKey& key, const CacheFilterSession* pSession)
{
    std::lock_guard<std::mutex> guard(m_lock_pending);

    do_refreshed(key, pSession);
}

CacheRules::SVector CacheMT::all_rules() const
{
    std::lock_guard<std::mutex> guard(m_lock_rules);
    return m_sRules;
}

void CacheMT::set_all_rules(const CacheRules::SVector& sRules)
{
    mxb_assert(mxs::MainWorker::is_current());

    std::lock_guard<std::mutex> guard(m_lock_rules);
    m_sRules = sRules;
}

// static
CacheMT* CacheMT::create(const std::string& name,
                         const CacheConfig* pConfig,
                         const CacheRules::SVector& sRules,
                         SStorageFactory sFactory)
{
    CacheMT* pCache = NULL;

    Storage::Config storage_config(CACHE_THREAD_MODEL_MT,
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
        MXS_EXCEPTION_GUARD(pCache = new CacheMT(name,
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
