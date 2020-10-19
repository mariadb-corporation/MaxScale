/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachemt.hh"
#include "storage.hh"
#include "storagefactory.hh"

using std::shared_ptr;

CacheMT::CacheMT(const std::string& name,
                 const CacheConfig* pConfig,
                 const std::vector<SCacheRules>& rules,
                 SStorageFactory sFactory,
                 Storage* pStorage)
    : CacheSimple(name, pConfig, rules, sFactory, pStorage)
{
    MXS_NOTICE("Created multi threaded cache.");
}

CacheMT::~CacheMT()
{
}

CacheMT* CacheMT::create(const std::string& name, const CacheConfig* pConfig)
{
    mxb_assert(pConfig);

    CacheMT* pCache = NULL;

    std::vector<SCacheRules> rules;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::get_storage_factory(*pConfig, &rules, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = create(name, pConfig, rules, sFactory);
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

// static
CacheMT* CacheMT::create(const std::string& name,
                         const CacheConfig* pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory sFactory)
{
    CacheMT* pCache = NULL;

    Storage::Config storage_config(CACHE_THREAD_MODEL_MT,
                                   pConfig->hard_ttl.count(),
                                   pConfig->soft_ttl.count(),
                                   pConfig->max_count,
                                   pConfig->max_size,
                                   pConfig->invalidate);

    const auto& storage_arguments = pConfig->storage_options;

    Storage* pStorage = sFactory->create_storage(name.c_str(), storage_config, storage_arguments);

    if (pStorage)
    {
        MXS_EXCEPTION_GUARD(pCache = new CacheMT(name,
                                                 pConfig,
                                                 rules,
                                                 sFactory,
                                                 pStorage));

        if (!pCache)
        {
            delete pStorage;
        }
    }

    return pCache;
}
