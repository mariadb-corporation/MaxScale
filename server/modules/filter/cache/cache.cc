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

// Initial size of hashtable used for storing keys of queries that
// are being fetches.
#define CACHE_PENDING_ITEMS 50


static int hashfn(const void* address)
{
    // TODO: Hash the address; pointers are not evenly distributed.
    return (long)address;
}

static int hashcmp(const void* address1, const void* address2)
{
    return (long)address2 - (long)address1;
}


Cache::Cache(const char* zName,
             const CACHE_CONFIG& config,
             CACHE_RULES* pRules,
             StorageFactory* pFactory,
             Storage* pStorage,
             HASHTABLE* pPending)
    : m_zName(zName)
    , m_config(config)
    , m_pRules(pRules)
    , m_pFactory(pFactory)
    , m_pStorage(pStorage)
    , m_pPending(pPending)
{
}

Cache::~Cache()
{
    // TODO: Free everything.
    ss_dassert(false);
}

//static
Cache* Cache::Create(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams)
{
    Cache* pCache = NULL;

    CACHE_CONFIG config;
    memset(&config, 0, sizeof(config));

    if (process_params(pzOptions, ppParams, &config))
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
            HASHTABLE* pPending = hashtable_alloc(CACHE_PENDING_ITEMS, hashfn, hashcmp);

            if (pPending)
            {
                StorageFactory *pFactory = StorageFactory::Open(config.storage);

                if (pFactory)
                {
                    uint32_t ttl = config.ttl;
                    int argc = config.storage_argc;
                    char** argv = config.storage_argv;

                    Storage* pStorage = pFactory->createStorage(zName, ttl, argc, argv);

                    if (pStorage)
                    {
                        pCache = new (std::nothrow) Cache(zName,
                                                          config,
                                                          pRules,
                                                          pFactory,
                                                          pStorage,
                                                          pPending);
                    }
                    else
                    {
                        MXS_ERROR("Could not create storage instance for '%s'.", zName);
                    }
                }
                else
                {
                    MXS_ERROR("Could not open storage factory '%s'.", config.storage);
                }

                if (!pCache)
                {
                    delete pFactory;
                }
            }

            if (!pCache)
            {
                hashtable_free(pPending);
            }
        }

        if (!pCache)
        {
            cache_rules_free(pRules);
        }
    }

    return pCache;
}

bool Cache::shouldStore(const char* zDefaultDb, const GWBUF* pQuery)
{
    return cache_rules_should_store(m_pRules, zDefaultDb, pQuery);
}

bool Cache::shouldUse(const SESSION* pSession)
{
    return cache_rules_should_use(m_pRules, pSession);
}

bool Cache::mustRefresh(const char* pKey, const SessionCache* pSessionCache)
{
    long key = hash_of_key(pKey);

    spinlock_acquire(&m_lockPending);
    // TODO: Remove the internal locking of hashtable. The internal
    // TODO: locking is no good if you need transactional behaviour.
    // TODO: Now we lock twice.
    void *pValue = hashtable_fetch(m_pPending, (void*)pKey);
    if (!pValue)
    {
        // It's not being fetched, so we make a note that we are.
        hashtable_add(m_pPending, (void*)pKey, (void*)pSessionCache);
    }
    spinlock_release(&m_lockPending);

    return pValue == NULL;
}

void Cache::refreshed(const char* pKey,  const SessionCache* pSessionCache)
{
    long key = hash_of_key(pKey);

    spinlock_acquire(&m_lockPending);
    ss_dassert(hashtable_fetch(m_pPending, (void*)pKey) == pSessionCache);
    ss_debug(int n =) hashtable_delete(m_pPending, (void*)pKey);
    ss_dassert(n == 1);
    spinlock_release(&m_lockPending);
}

cache_result_t Cache::getKey(const char* zDefaultDb,
                             const GWBUF* pQuery,
                             char* pKey)
{
    return m_pStorage->getKey(zDefaultDb, pQuery, pKey);
}

cache_result_t Cache::getValue(const char* pKey,
                               uint32_t flags,
                               GWBUF** ppValue)
{
    return m_pStorage->getValue(pKey, flags, ppValue);
}

cache_result_t Cache::putValue(const char* pKey,
                               const GWBUF* pValue)
{
    return m_pStorage->putValue(pKey, pValue);
}

cache_result_t Cache::delValue(const char* pKey)
{
    return m_pStorage->delValue(pKey);
}

/**
 * Processes the cache params
 *
 * @param options Options as passed to the filter.
 * @param params  Parameters as passed to the filter.
 * @param config  Pointer to config instance where params will be stored.
 *
 * @return True if all parameters could be processed, false otherwise.
 */
bool Cache::process_params(char **pzOptions, FILTER_PARAMETER **ppParams, CACHE_CONFIG* pConfig)
{
    bool error = false;

    for (int i = 0; ppParams[i]; ++i)
    {
        const FILTER_PARAMETER *pParam = ppParams[i];

        if (strcmp(pParam->name, "max_resultset_rows") == 0)
        {
            int v = atoi(pParam->value);

            if (v > 0)
            {
                pConfig->max_resultset_rows = v;
            }
            else
            {
                pConfig->max_resultset_rows = CACHE_DEFAULT_MAX_RESULTSET_ROWS;
            }
        }
        else if (strcmp(pParam->name, "max_resultset_size") == 0)
        {
            int v = atoi(pParam->value);

            if (v > 0)
            {
                pConfig->max_resultset_size = v * 1024;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", pParam->name);
                error = true;
            }
        }
        else if (strcmp(pParam->name, "rules") == 0)
        {
            if (*pParam->value == '/')
            {
                pConfig->rules = MXS_STRDUP(pParam->value);
            }
            else
            {
                const char *datadir = get_datadir();
                size_t len = strlen(datadir) + 1 + strlen(pParam->value) + 1;

                char *rules = (char*)MXS_MALLOC(len);

                if (rules)
                {
                    sprintf(rules, "%s/%s", datadir, pParam->value);
                    pConfig->rules = rules;
                }
            }

            if (!pConfig->rules)
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "storage_options") == 0)
        {
            pConfig->storage_options = MXS_STRDUP(pParam->value);

            if (pConfig->storage_options)
            {
                int argc = 1;
                char *arg = pConfig->storage_options;

                while ((arg = strchr(pConfig->storage_options, ',')))
                {
                    ++argc;
                }

                pConfig->storage_argv = (char**) MXS_MALLOC((argc + 1) * sizeof(char*));

                if (pConfig->storage_argv)
                {
                    pConfig->storage_argc = argc;

                    int i = 0;
                    arg = pConfig->storage_options;
                    pConfig->storage_argv[i++] = arg;

                    while ((arg = strchr(pConfig->storage_options, ',')))
                    {
                        *arg = 0;
                        ++arg;
                        pConfig->storage_argv[i++] = arg;
                    }

                    pConfig->storage_argv[i] = NULL;
                }
                else
                {
                    MXS_FREE(pConfig->storage_options);
                    pConfig->storage_options = NULL;
                }
            }
            else
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "storage") == 0)
        {
            pConfig->storage = pParam->value;
        }
        else if (strcmp(pParam->name, "ttl") == 0)
        {
            int v = atoi(pParam->value);

            if (v > 0)
            {
                pConfig->ttl = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", pParam->name);
                error = true;
            }
        }
        else if (strcmp(pParam->name, "debug") == 0)
        {
            int v = atoi(pParam->value);

            if ((v >= CACHE_DEBUG_MIN) && (v <= CACHE_DEBUG_MAX))
            {
                pConfig->debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          pParam->name, CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
                error = true;
            }
        }
        else if (!filter_standard_parameter(pParam->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", pParam->name);
            error = true;
        }
    }

    return !error;
}
