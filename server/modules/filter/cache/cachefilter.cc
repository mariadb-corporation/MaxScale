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
#include "cachefilter.h"
#include <exception>
#include <new>
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/gwdirs.h>
#include "cachemt.h"
#include "cachept.h"
#include "sessioncache.h"

static char VERSION_STRING[] = "V1.0.0";

static const CACHE_CONFIG DEFAULT_CONFIG =
{
    CACHE_DEFAULT_MAX_RESULTSET_ROWS,
    CACHE_DEFAULT_MAX_RESULTSET_SIZE,
    NULL,
    NULL,
    NULL,
    NULL,
    0,
    CACHE_DEFAULT_TTL,
    CACHE_DEFAULT_DEBUG,
    CACHE_DEFAULT_THREAD_MODEL,
};

typedef struct cache_filter
{
    cache_filter()
        : config(DEFAULT_CONFIG)
        , pCache(NULL)
    {
    }

    ~cache_filter()
    {
        delete pCache;
        cache_config_finish(config);
    }

    CACHE_CONFIG config;
    Cache*       pCache;

private:
    cache_filter(const cache_filter&);
    cache_filter& operator = (const cache_filter&);
} CACHE_FILTER;

static FILTER*  createInstance(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams);
static void*    newSession(FILTER* pInstance, SESSION* pSession);
static void     closeSession(FILTER* pInstance, void* pSessionData);
static void     freeSession(FILTER* pInstance, void* pSessionData);
static void     setDownstream(FILTER* pInstance, void* pSessionData, DOWNSTREAM* pDownstream);
static void     setUpstream(FILTER* pInstance, void* pSessionData, UPSTREAM* pUpstream);
static int      routeQuery(FILTER* pInstance, void* pSessionData, GWBUF* pPacket);
static int      clientReply(FILTER* pInstance, void* pSessionData, GWBUF* pPacket);
static void     diagnostics(FILTER* pInstance, void* pSessionData, DCB* pDcb);
static uint64_t getCapabilities(void);

static bool process_params(char **pzOptions, FILTER_PARAMETER **ppParams, CACHE_CONFIG& config);

//
// Global symbols of the Module
//

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_IN_DEVELOPMENT,
    FILTER_VERSION,
    "A caching filter that is capable of caching and returning cached data."
};

extern "C" char *version()
{
    return VERSION_STRING;
}

/**
 * The module initialization functions, called when the module has
 * been loaded.
 */
extern "C" void ModuleInit()
{
}

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
extern "C" FILTER_OBJECT *GetModuleObject()
{
    static FILTER_OBJECT object =
        {
            createInstance,
            newSession,
            closeSession,
            freeSession,
            setDownstream,
            setUpstream,
            routeQuery,
            clientReply,
            diagnostics,
            getCapabilities,
            NULL, // destroyInstance
        };

    return &object;
};

//
// API Implementation BEGIN
//

/**
 * Create an instance of the cache filter for a particular service
 * within MaxScale.
 *
 * @param zName      The name of the instance (as defined in the config file).
 * @param pzOptions  The options for this filter
 * @param ppparams   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *createInstance(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams)
{
    CACHE_FILTER* pFilter = new (std::nothrow) CACHE_FILTER;

    if (pFilter)
    {
        if (process_params(pzOptions, ppParams, pFilter->config))
        {
            switch (pFilter->config.thread_model)
            {
            case CACHE_THREAD_MODEL_MT:
                MXS_NOTICE("Creating shared cache.");
                CPP_GUARD(pFilter->pCache = CacheMT::Create(zName, &pFilter->config));
                break;

            case CACHE_THREAD_MODEL_ST:
                MXS_NOTICE("Creating thread specific cache.");
                CPP_GUARD(pFilter->pCache = CachePT::Create(zName, &pFilter->config));
                break;

            default:
                ss_dassert(!true);
            }
        }

        if (!pFilter->pCache)
        {
            cache_config_finish(pFilter->config);
            delete pFilter;
            pFilter = NULL;
        }
    }

    return reinterpret_cast<FILTER*>(pFilter);
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param pInstance  The cache instance data
 * @param pSession   The session itself
 *
 * @return Session specific data for this session
 */
static void *newSession(FILTER* pInstance, SESSION* pSession)
{
    CACHE_FILTER *pFilter = reinterpret_cast<CACHE_FILTER*>(pInstance);
    Cache* pCache = pFilter->pCache;

    SessionCache* pSessionCache = NULL;
    CPP_GUARD(pSessionCache = SessionCache::Create(pCache, pSession));

    return pSessionCache;
}

/**
 * A session has been closed.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session being closed
 */
static void closeSession(FILTER* pInstance, void* pSessionData)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    CPP_GUARD(pSessionCache->close());
}

/**
 * Free the session data.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session being closed
 */
static void freeSession(FILTER* pInstance, void* pSessionData)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    delete pSessionCache;
}

/**
 * Set the downstream component for this filter.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session
 * @param pDownstream   The downstream filter or router
 */
static void setDownstream(FILTER* pInstance, void* pSessionData, DOWNSTREAM* pDownstream)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    CPP_GUARD(pSessionCache->setDownstream(pDownstream));
}

/**
 * Set the upstream component for this filter.
 *
 * @param pInstance     The cache instance data
 * @param pSessionData  The session data of the session
 * @param pUpstream     The upstream filter or router
 */
static void setUpstream(FILTER* pInstance, void* pSessionData, UPSTREAM* pUpstream)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    CPP_GUARD(pSessionCache->setUpstream(pUpstream));
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param pInstance     The filter instance data
 * @param pSessionData  The filter session data
 * @param pPacket       Buffer containing an MySQL protocol packet.
 */
static int routeQuery(FILTER* pInstance, void* pSessionData, GWBUF* pPacket)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    int rv = 0;
    CPP_GUARD(rv = pSessionCache->routeQuery(pPacket));

    return rv;
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param pInstance     The filter instance data
 * @param pSessionData  The filter session data
 * @param pPacket       The response data
 */
static int clientReply(FILTER* pInstance, void* pSessionData, GWBUF* pPacket)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    int rv = 0;
    CPP_GUARD(rv = pSessionCache->clientReply(pPacket));

    return rv;
}

/**
 * Diagnostics routine
 *
 * If cpSessionData is NULL then print diagnostics on the instance as a whole,
 * otherwise print diagnostics for the particular session.
 *
 * @param pInstance     The filter instance
 * @param pSessionData  Filter session, may be NULL
 * @param pDcb          The DCB for diagnostic output
 */
static void diagnostics(FILTER* pInstance, void* pSessionData, DCB* pDcb)
{
    SessionCache* pSessionCache = static_cast<SessionCache*>(pSessionData);

    CPP_GUARD(pSessionCache->diagnostics(pDcb));
}


/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_TRANSACTION_TRACKING;
}

//
// API Implementation END
//

/**
 * Processes the cache params
 *
 * @param options Options as passed to the filter.
 * @param params  Parameters as passed to the filter.
 * @param config  Reference to config instance where params will be stored.
 *
 * @return True if all parameters could be processed, false otherwise.
 */
static bool process_params(char **pzOptions, FILTER_PARAMETER **ppParams, CACHE_CONFIG& config)
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
                config.max_resultset_rows = v;
            }
            else
            {
                config.max_resultset_rows = CACHE_DEFAULT_MAX_RESULTSET_ROWS;
            }
        }
        else if (strcmp(pParam->name, "max_resultset_size") == 0)
        {
            int v = atoi(pParam->value);

            if (v > 0)
            {
                config.max_resultset_size = v * 1024;
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
                config.rules = MXS_STRDUP(pParam->value);
            }
            else
            {
                const char* datadir = get_datadir();
                size_t len = strlen(datadir) + 1 + strlen(pParam->value) + 1;

                char *rules = (char*)MXS_MALLOC(len);

                if (rules)
                {
                    sprintf(rules, "%s/%s", datadir, pParam->value);
                    config.rules = rules;
                }
            }

            if (!config.rules)
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "storage_options") == 0)
        {
            config.storage_options = MXS_STRDUP(pParam->value);

            if (config.storage_options)
            {
                int argc = 1;
                char *arg = config.storage_options;

                while ((arg = strchr(config.storage_options, ',')))
                {
                    ++argc;
                }

                config.storage_argv = (char**) MXS_MALLOC((argc + 1) * sizeof(char*));

                if (config.storage_argv)
                {
                    config.storage_argc = argc;

                    int i = 0;
                    arg = config.storage_options;
                    config.storage_argv[i++] = arg;

                    while ((arg = strchr(config.storage_options, ',')))
                    {
                        *arg = 0;
                        ++arg;
                        config.storage_argv[i++] = arg;
                    }

                    config.storage_argv[i] = NULL;
                }
                else
                {
                    MXS_FREE(config.storage_options);
                    config.storage_options = NULL;
                }
            }
            else
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "storage") == 0)
        {
            config.storage = MXS_STRDUP(pParam->value);

            if (!config.storage)
            {
                error = true;
            }
        }
        else if (strcmp(pParam->name, "ttl") == 0)
        {
            int v = atoi(pParam->value);

            if (v > 0)
            {
                config.ttl = v;
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
                config.debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          pParam->name, CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
                error = true;
            }
        }
        else if (strcmp(pParam->name, "cached_data") == 0)
        {
            if (strcmp(pParam->value, "shared") == 0)
            {
                config.thread_model = CACHE_THREAD_MODEL_MT;
            }
            else if (strcmp(pParam->value, "thread_specific") == 0)
            {
                config.thread_model = CACHE_THREAD_MODEL_ST;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be either 'shared' or 'thread_specific'.", pParam->name);
                error = true;
            }
        }
        else if (!filter_standard_parameter(pParam->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", pParam->name);
            error = true;
        }
    }

    if (error)
    {
        cache_config_finish(config);
    }

    return !error;
}

/**
 * Frees all data of a config object, but not the object itself
 *
 * @param pConfig  Pointer to a config object.
 */
void cache_config_finish(CACHE_CONFIG& config)
{
    MXS_FREE(config.rules);
    MXS_FREE(config.storage);
    MXS_FREE(config.storage_options);
    MXS_FREE(config.storage_argv); // The items need not be freed, they point into storage_options.

    config.max_resultset_rows = 0;
    config.max_resultset_size = 0;
    config.rules = NULL;
    config.storage = NULL;
    config.storage_options = NULL;
    config.storage_argc = 0;
    config.storage_argv = NULL;
    config.ttl = 0;
    config.debug = 0;
}

/**
 * Frees all data of a config object, and the object itself
 *
 * @param pConfig  Pointer to a config object.
 */
void cache_config_free(CACHE_CONFIG* pConfig)
{
    if (pConfig)
    {
        cache_config_finish(*pConfig);
        MXS_FREE(pConfig);
    }
}

/**
 * Resets the data without freeing anything.
 *
 * @param config  Reference to a config object.
 */
void cache_config_reset(CACHE_CONFIG& config)
{
    memset(&config, 0, sizeof(config));
}
