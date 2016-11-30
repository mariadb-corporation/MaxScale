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
#include "cache.h"
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include "storagefactory.h"
#include "storage.h"

Cache::Cache(const std::string&  name,
             const CACHE_CONFIG* pConfig,
             SCacheRules         sRules,
             SStorageFactory     sFactory)
    : m_name(name)
    , m_config(*pConfig)
    , m_sRules(sRules)
    , m_sFactory(sFactory)
{
}

Cache::~Cache()
{
}

//static
bool Cache::Create(const CACHE_CONFIG& config,
                   CacheRules**        ppRules)
{
    CacheRules* pRules = NULL;

    if (config.rules)
    {
        pRules = CacheRules::load(config.rules, config.debug);
    }
    else
    {
        pRules = CacheRules::create(config.debug);
    }

    if (pRules)
    {
        *ppRules = pRules;
    }
    else
    {
        MXS_ERROR("Could not create rules.");
    }

    return pRules != NULL;
}

//static
bool Cache::Create(const CACHE_CONFIG& config,
                   CacheRules**        ppRules,
                   StorageFactory**    ppFactory)
{
    CacheRules* pRules = NULL;
    StorageFactory* pFactory = NULL;

    if (Create(config, &pRules))
    {
        pFactory = StorageFactory::Open(config.storage);

        if (pFactory)
        {
            *ppFactory = pFactory;
            *ppRules = pRules;
        }
        else
        {
            MXS_ERROR("Could not open storage factory '%s'.", config.storage);
            delete pRules;
        }
    }

    return pFactory != NULL;
}

bool Cache::should_store(const char* zDefaultDb, const GWBUF* pQuery)
{
    return m_sRules->should_store(zDefaultDb, pQuery);
}

bool Cache::should_use(const SESSION* pSession)
{
    return m_sRules->should_use(pSession);
}
