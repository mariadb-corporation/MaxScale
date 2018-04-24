/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cachemt.hh"
#include "storage.hh"
#include "storagefactory.hh"

using maxscale::SpinLockGuard;
using std::tr1::shared_ptr;

CacheMT::CacheMT(const std::string&              name,
                 const CACHE_CONFIG*             pConfig,
                 const std::vector<SCacheRules>& rules,
                 SStorageFactory                 sFactory,
                 Storage*                        pStorage)
    : CacheSimple(name, pConfig, rules, sFactory, pStorage)
{
    spinlock_init(&m_lock_pending);

    MXS_NOTICE("Created multi threaded cache.");
}

CacheMT::~CacheMT()
{
}

CacheMT* CacheMT::Create(const std::string& name, const CACHE_CONFIG* pConfig)
{
    ss_dassert(pConfig);

    CacheMT* pCache = NULL;

    std::vector<SCacheRules> rules;
    StorageFactory* pFactory = NULL;

    if (CacheSimple::Create(*pConfig, &rules, &pFactory))
    {
        shared_ptr<StorageFactory> sFactory(pFactory);

        pCache = Create(name, pConfig, rules, sFactory);
    }

    return pCache;
}

json_t* CacheMT::get_info(uint32_t flags) const
{
    SpinLockGuard guard(m_lock_pending);

    return CacheSimple::do_get_info(flags);
}

bool CacheMT::must_refresh(const CACHE_KEY& key, const CacheFilterSession* pSession)
{
    SpinLockGuard guard(m_lock_pending);

    return do_must_refresh(key, pSession);
}

void CacheMT::refreshed(const CACHE_KEY& key,  const CacheFilterSession* pSession)
{
    SpinLockGuard guard(m_lock_pending);

    do_refreshed(key, pSession);
}

// static
CacheMT* CacheMT::Create(const std::string&              name,
                         const CACHE_CONFIG*             pConfig,
                         const std::vector<SCacheRules>& rules,
                         SStorageFactory                 sFactory)
{
    CacheMT* pCache = NULL;

    CacheStorageConfig storage_config(CACHE_THREAD_MODEL_MT,
                                      pConfig->hard_ttl,
                                      pConfig->soft_ttl,
                                      pConfig->max_count,
                                      pConfig->max_size);

    int argc = pConfig->storage_argc;
    char** argv = pConfig->storage_argv;

    Storage* pStorage = sFactory->createStorage(name.c_str(), storage_config, argc, argv);

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
