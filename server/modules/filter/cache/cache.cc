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

Cache::Cache(const char* zName,
             const CACHE_CONFIG* pConfig,
             CACHE_RULES* pRules,
             StorageFactory* pFactory)
    : m_zName(zName)
    , m_config(*pConfig)
    , m_pRules(pRules)
    , m_pFactory(pFactory)
{
}

Cache::~Cache()
{
    cache_rules_free(m_pRules);
    delete m_pFactory;
}

//static
bool Cache::Create(const CACHE_CONFIG& config,
                   CACHE_RULES**       ppRules)
{
    CACHE_RULES* pRules = NULL;

    if (config.rules)
    {
        pRules = cache_rules_load(config.rules, config.debug);
    }
    else
    {
        pRules = cache_rules_create(config.debug);
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
                   CACHE_RULES**       ppRules,
                   StorageFactory**    ppFactory)
{
    CACHE_RULES* pRules = NULL;
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
            cache_rules_free(pRules);
        }
    }

    return pFactory != NULL;
}

bool Cache::shouldStore(const char* zDefaultDb, const GWBUF* pQuery)
{
    return cache_rules_should_store(m_pRules, zDefaultDb, pQuery);
}

bool Cache::shouldUse(const SESSION* pSession)
{
    return cache_rules_should_use(m_pRules, pSession);
}


