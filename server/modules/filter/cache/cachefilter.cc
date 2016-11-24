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
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>
#include "rules.h"
#include "sessioncache.h"
#include "storage.h"
#include "storagefactory.h"

static char VERSION_STRING[] = "V1.0.0";

static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **);
static void   *newSession(FILTER *instance, SESSION *session);
static void    closeSession(FILTER *instance, void *sdata);
static void    freeSession(FILTER *instance, void *sdata);
static void    setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *downstream);
static void    setUpstream(FILTER *instance, void *sdata, UPSTREAM *upstream);
static int     routeQuery(FILTER *instance, void *sdata, GWBUF *queue);
static int     clientReply(FILTER *instance, void *sdata, GWBUF *queue);
static void    diagnostics(FILTER *instance, void *sdata, DCB *dcb);
static uint64_t getCapabilities(void);

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
// Implementation
//

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
    CACHE_DEFAULT_DEBUG
};

static bool process_params(char **options, FILTER_PARAMETER **params, CACHE_CONFIG* config);

/**
 * Hashes a cache key to an integer.
 *
 * @param key Pointer to cache key.
 *
 * @returns Corresponding integer hash.
 */
int hash_of_key(const void* key)
{
    int hash = 0;

    const char* i   = (const char*)key;
    const char* end = i + CACHE_KEY_MAXLEN;

    while (i < end)
    {
        int c = *i;
        hash = c + (hash << 6) + (hash << 16) - hash;
        ++i;
    }

    return hash;
}

static int hashfn(const void* address)
{
    // TODO: Hash the address; pointers are not evenly distributed.
    return (long)address;
}

static int hashcmp(const void* address1, const void* address2)
{
    return (long)address2 - (long)address1;
}

// Initial size of hashtable used for storing keys of queries that
// are being fetches.
#define CACHE_PENDING_ITEMS 50

//
// API BEGIN
//

/**
 * Create an instance of the cache filter for a particular service
 * within MaxScale.
 *
 * @param name     The name of the instance (as defined in the config file).
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **params)
{
    CACHE_INSTANCE *cinstance = NULL;
    CACHE_CONFIG config = DEFAULT_CONFIG;

    if (process_params(options, params, &config))
    {
        CACHE_RULES *rules = NULL;

        if (config.rules)
        {
            rules = cache_rules_load(config.rules, config.debug);
        }
        else
        {
            rules = cache_rules_create(config.debug);
        }

        if (rules)
        {
            cinstance = (CACHE_INSTANCE*)MXS_CALLOC(1, sizeof(CACHE_INSTANCE));
            HASHTABLE* pending = hashtable_alloc(CACHE_PENDING_ITEMS, hashfn, hashcmp);

            if (cinstance && pending)
            {
                StorageFactory *factory = StorageFactory::Open(config.storage);

                if (factory)
                {
                    uint32_t ttl = config.ttl;
                    int argc = config.storage_argc;
                    char** argv = config.storage_argv;

                    Storage *storage = factory->createStorage(name, ttl, argc, argv);

                    if (storage)
                    {
                        cinstance->name = name;
                        cinstance->config = config;
                        cinstance->rules = rules;
                        cinstance->factory = factory;
                        cinstance->storage = storage;
                        cinstance->pending = pending;

                        MXS_NOTICE("Cache storage %s opened and initialized.", config.storage);
                    }
                    else
                    {
                        MXS_ERROR("Could not create storage instance for '%s'.", name);
                        cache_rules_free(rules);
                        delete factory;
                        MXS_FREE(cinstance);
                        hashtable_free(pending);
                        cinstance = NULL;
                    }
                }
                else
                {
                    MXS_ERROR("Could not load cache storage module '%s'.", name);
                    cache_rules_free(rules);
                    MXS_FREE(cinstance);
                    cinstance = NULL;
                }
            }
            else
            {
                MXS_FREE(cinstance);
                if (pending)
                {
                    hashtable_free(pending);
                }
            }
        }
    }

    return (FILTER*)cinstance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The cache instance data
 * @param session   The session itself
 *
 * @return Session specific data for this session
 */
static void *newSession(FILTER *instance, SESSION *session)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache *pSessionCache = SessionCache::Create(cinstance, session);

    return pSessionCache;
}

/**
 * A session has been closed.
 *
 * @param instance  The cache instance data
 * @param sdata     The session data of the session being closed
 */
static void closeSession(FILTER *instance, void *sdata)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    pSessionCache->close();
}

/**
 * Free the session data.
 *
 * @param instance  The cache instance data
 * @param sdata     The session data of the session being closed
 */
static void freeSession(FILTER *instance, void *sdata)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    delete pSessionCache;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance    The cache instance data
 * @param sdata       The session data of the session
 * @param down        The downstream filter or router
 */
static void setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *down)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    pSessionCache->setDownstream(down);
}

/**
 * Set the upstream component for this filter.
 *
 * @param instance    The cache instance data
 * @param sdata       The session data of the session
 * @param up          The upstream filter or router
 */
static void setUpstream(FILTER *instance, void *sdata, UPSTREAM *up)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    pSessionCache->setUpstream(up);
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param buffer    Buffer containing an MySQL protocol packet.
 */
static int routeQuery(FILTER *instance, void *sdata, GWBUF *packet)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    return pSessionCache->routeQuery(packet);
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param queue     The query data
 */
static int clientReply(FILTER *instance, void *sdata, GWBUF *data)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    return pSessionCache->clientReply(data);
}

/**
 * Diagnostics routine
 *
 * If csdata is NULL then print diagnostics on the instance as a whole,
 * otherwise print diagnostics for the particular session.
 *
 * @param instance  The filter instance
 * @param fsession  Filter session, may be NULL
 * @param dcb       The DCB for diagnostic output
 */
static void diagnostics(FILTER *instance, void *sdata, DCB *dcb)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;

    SessionCache* pSessionCache = static_cast<SessionCache*>(sdata);

    pSessionCache->diagnostics(dcb);
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
// API END
//

/**
 * Processes the cache params
 *
 * @param options Options as passed to the filter.
 * @param params  Parameters as passed to the filter.
 * @param config  Pointer to config instance where params will be stored.
 *
 * @return True if all parameters could be processed, false otherwise.
 */
static bool process_params(char **options, FILTER_PARAMETER **params, CACHE_CONFIG* config)
{
    bool error = false;

    for (int i = 0; params[i]; ++i)
    {
        const FILTER_PARAMETER *param = params[i];

        if (strcmp(param->name, "max_resultset_rows") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->max_resultset_rows = v;
            }
            else
            {
                config->max_resultset_rows = CACHE_DEFAULT_MAX_RESULTSET_ROWS;
            }
        }
        else if (strcmp(param->name, "max_resultset_size") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->max_resultset_size = v * 1024;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", param->name);
                error = true;
            }
        }
        else if (strcmp(param->name, "rules") == 0)
        {
            if (*param->value == '/')
            {
                config->rules = MXS_STRDUP(param->value);
            }
            else
            {
                const char *datadir = get_datadir();
                size_t len = strlen(datadir) + 1 + strlen(param->value) + 1;

                char *rules = (char*)MXS_MALLOC(len);

                if (rules)
                {
                    sprintf(rules, "%s/%s", datadir, param->value);
                    config->rules = rules;
                }
            }

            if (!config->rules)
            {
                error = true;
            }
        }
        else if (strcmp(param->name, "storage_options") == 0)
        {
            config->storage_options = MXS_STRDUP(param->value);

            if (config->storage_options)
            {
                int argc = 1;
                char *arg = config->storage_options;

                while ((arg = strchr(config->storage_options, ',')))
                {
                    ++argc;
                }

                config->storage_argv = (char**) MXS_MALLOC((argc + 1) * sizeof(char*));

                if (config->storage_argv)
                {
                    config->storage_argc = argc;

                    int i = 0;
                    arg = config->storage_options;
                    config->storage_argv[i++] = arg;

                    while ((arg = strchr(config->storage_options, ',')))
                    {
                        *arg = 0;
                        ++arg;
                        config->storage_argv[i++] = arg;
                    }

                    config->storage_argv[i] = NULL;
                }
                else
                {
                    MXS_FREE(config->storage_options);
                    config->storage_options = NULL;
                }
            }
            else
            {
                error = true;
            }
        }
        else if (strcmp(param->name, "storage") == 0)
        {
            config->storage = param->value;
        }
        else if (strcmp(param->name, "ttl") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->ttl = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", param->name);
                error = true;
            }
        }
        else if (strcmp(param->name, "debug") == 0)
        {
            int v = atoi(param->value);

            if ((v >= CACHE_DEBUG_MIN) && (v <= CACHE_DEBUG_MAX))
            {
                config->debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          param->name, CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
                error = true;
            }
        }
        else if (!filter_standard_parameter(params[i]->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", param->name);
            error = true;
        }
    }

    return !error;
}
