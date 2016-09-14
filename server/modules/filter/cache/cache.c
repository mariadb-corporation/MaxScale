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
#include <maxscale/alloc.h>
#include <filter.h>
#include <log_manager.h>
#include <modinfo.h>
#include <modutil.h>
#include <query_classifier.h>
#include "storage.h"

static char VERSION_STRING[] = "V1.0.0";

typedef enum cache_references
{
    CACHE_REFERENCES_ANY,
    CACHE_REFERENCES_QUALIFIED
} cache_references_t;

#define DEFAULT_ALLOWED_REFERENCES CACHE_REFERENCES_QUALIFIED
// Bytes
#define DEFAULT_MAX_RESULTSET_SIZE 64 * 1024 * 1024
// Seconds
#define DEFAULT_TTL                10

static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **);
static void   *newSession(FILTER *instance, SESSION *session);
static void    closeSession(FILTER *instance, void *sdata);
static void    freeSession(FILTER *instance, void *sdata);
static void    setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *downstream);
static void    setUpstream(FILTER *instance, void *sdata, UPSTREAM *upstream);
static int     routeQuery(FILTER *instance, void *sdata, GWBUF *queue);
static int     clientReply(FILTER *instance, void *sdata, GWBUF *queue);
static void    diagnostics(FILTER *instance, void *sdata, DCB *dcb);

#define C_DEBUG(format, ...) MXS_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)

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

char *version()
{
    return VERSION_STRING;
}

/**
 * The module initialization functions, called when the module has
 * been loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
FILTER_OBJECT *GetModuleObject()
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
        };

    return &object;
};

//
// Implementation
//

typedef struct cache_config
{
    cache_references_t allowed_references;
    uint32_t           max_resultset_size;
    const char        *storage;
    const char        *storage_args;
    uint32_t           ttl;
} CACHE_CONFIG;

typedef struct cache_instance
{
    const char            *name;
    CACHE_CONFIG           config;
    CACHE_STORAGE_MODULE  *module;
    CACHE_STORAGE         *storage;
} CACHE_INSTANCE;

static const CACHE_CONFIG DEFAULT_CONFIG =
{
    DEFAULT_ALLOWED_REFERENCES,
    DEFAULT_MAX_RESULTSET_SIZE,
    NULL,
    NULL,
    DEFAULT_TTL
};

typedef struct cache_session_data
{
    CACHE_STORAGE_API *api;     /**< The storage API to be used. */
    CACHE_STORAGE     *storage; /**< The storage to be used with this session data. */
    DOWNSTREAM         down;    /**< The previous filter or equivalent. */
    UPSTREAM           up;      /**< The next filter or equivalent. */
    GWBUF             *packets; /**< A possible incomplete packet. */
    SESSION           *session; /**< The session this data is associated with. */
    char               key[CACHE_KEY_MAXLEN]; /**< Key storage. */
    char              *used_key; /**< A key if one is ued. */
} CACHE_SESSION_DATA;

static bool route_using_cache(CACHE_INSTANCE *instance,
                              CACHE_SESSION_DATA *sdata,
                              const GWBUF *key,
                              GWBUF **value);

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
    CACHE_CONFIG config = DEFAULT_CONFIG;

    bool error = false;

    for (int i = 0; params[i]; ++i)
    {
        const FILTER_PARAMETER *param = params[i];

        if (strcmp(param->name, "allowed_references") == 0)
        {
            if (strcmp(param->value, "qualified") == 0)
            {
                config.allowed_references = CACHE_REFERENCES_QUALIFIED;
            }
            else if (strcmp(param->value, "any") == 0)
            {
                config.allowed_references = CACHE_REFERENCES_ANY;
            }
            else
            {
                MXS_ERROR("Unknown value '%s' for parameter '%s'.", param->value, param->name);
                error = true;
            }
        }
        else if (strcmp(param->name, "max_resultset_size") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config.max_resultset_size = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", param->name);
                error = true;
            }
        }
        else if (strcmp(param->name, "storage_args") == 0)
        {
            config.storage_args = param->value;
        }
        else if (strcmp(param->name, "storage") == 0)
        {
            config.storage = param->value;
        }
        else if (strcmp(param->name, "ttl") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config.ttl = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", param->name);
                error = true;
            }
        }
        else if (!filter_standard_parameter(params[i]->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", param->name);
            error = true;
        }
    }

    CACHE_INSTANCE *cinstance = NULL;

    if (!error)
    {
        if ((cinstance = MXS_CALLOC(1, sizeof(CACHE_INSTANCE))) != NULL)
        {
            CACHE_STORAGE_MODULE *module = cache_storage_open(config.storage);

            if (module)
            {
                CACHE_STORAGE *storage = module->api->createInstance(name, config.ttl, 0, NULL);

                if (storage)
                {
                    cinstance->name = name;
                    cinstance->config = config;
                    cinstance->module = module;
                    cinstance->storage = storage;

                    MXS_NOTICE("Cache storage %s opened and initialized.", config.storage);
                }
                else
                {
                    MXS_ERROR("Could not create storage instance for %s.", name);
                    cache_storage_close(module);
                    MXS_FREE(cinstance);
                    cinstance = NULL;
                }
            }
            else
            {
                MXS_ERROR("Could not load cache storage module %s.", name);
                MXS_FREE(cinstance);
                cinstance = NULL;
            }
        }
    }
    else
    {
        cinstance = NULL;
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)MXS_CALLOC(1, sizeof(CACHE_SESSION_DATA));

    if (csdata)
    {
        csdata->api = cinstance->module->api;
        csdata->storage = cinstance->storage;
        csdata->session = session;
    }

    return csdata;
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    MXS_FREE(csdata);
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    csdata->down = *down;
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    csdata->up = *up;
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param packets   The query data
 */
static int routeQuery(FILTER *instance, void *sdata, GWBUF *packets)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    if (csdata->packets)
    {
        C_DEBUG("Old packets exist.");
        gwbuf_append(csdata->packets, packets);
    }
    else
    {
        C_DEBUG("NO old packets exist.");
        csdata->packets = packets;
    }

    packets = modutil_get_complete_packets(&csdata->packets);

    int rv;

    if (packets)
    {
        C_DEBUG("At least one complete packet exist.");
        GWBUF *packet;

        // TODO: Is it really possible to get more that one packet
        // TODO: is this loop? If so, can those packets be sent
        // TODO: after one and other, or do we need to wait for
        // TODO: a replies? If there are more complete packets
        // TODO: than one, then either CACHE_SESSION_DATA::key
        // TODO: needs to be a queue

        // TODO: modutil_get_next_MySQL_packet *copies* the data.
        while ((packet = modutil_get_next_MySQL_packet(&packets)))
        {
            C_DEBUG("Processing packet.");
            bool use_default = true;

            if (modutil_is_SQL(packet))
            {
                C_DEBUG("Is SQL.");
                // We do not care whether the query was fully parsed or not.
                // If a query cannot be fully parsed, the worst thing that can
                // happen is that caching is not used, even though it would be
                // possible.

                if (qc_get_operation(packet) == QUERY_OP_SELECT)
                {
                    C_DEBUG("Is a SELECT");

                    GWBUF *result;
                    use_default = !route_using_cache(cinstance, csdata, packet, &result);

                    if (!use_default)
                    {
                        C_DEBUG("Using data from cache.");
                        gwbuf_free(packet);
                        DCB *dcb = csdata->session->client_dcb;

                        // TODO: This is not ok. Any filters before this filter, will not
                        // TODO: see this data.
                        rv = dcb->func.write(dcb, result);
                    }
                }
                else
                {
                    C_DEBUG("Is NOT a SELECT");
                }
            }
            else
            {
                C_DEBUG("Is NOT SQL.");
            }

            if (use_default)
            {
                C_DEBUG("Using default processing.");
                rv = csdata->down.routeQuery(csdata->down.instance, csdata->down.session, packet);
            }
        }
    }
    else
    {
        C_DEBUG("Not even one complete packet exist; more data needed.");
        // Ok, we need more data before we can do something.
        rv = 1;
    }

    return rv;
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param queue     The query data
 */
static int clientReply(FILTER *instance, void *sdata, GWBUF *queue)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    // TODO: queue can be put to the cache only if it is a complete
    // TODO: response. If it isn't, then we need to stash it and wait
    // TODO: we get a complete response.
    // TODO: Since we will know from the first queue how big the
    // TODO: entire response will be, this is also where we can decide
    // TODO: that something is too large to cache. If it is, an existing
    // TODO: item must be deleted.

    if (csdata->used_key)
    {
        C_DEBUG("Key available, storing result.");

        cache_result_t result = csdata->api->putValue(csdata->storage, csdata->used_key, queue);
        csdata->used_key = NULL;

        if (result != CACHE_RESULT_OK)
        {
            MXS_ERROR("Could not store cache item.");
        }
    }

    return csdata->up.clientReply(csdata->up.instance, csdata->up.session, queue);
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
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    dcb_printf(dcb, "Hello World from Cache!\n");
}

//
// API END
//

/**
 * Route a query via the cache.
 *
 * @param instance The filter instance.
 * @param sdata Session data
 * @param key A SELECT packet.
 * @param value The result.
 * @return True if the query was satisfied from the query.
 */
static bool route_using_cache(CACHE_INSTANCE *instance,
                              CACHE_SESSION_DATA *csdata,
                              const GWBUF *query,
                              GWBUF **value)
{
    // TODO: This works *only* if only one request/response is handled at a time.
    // TODO: Is that the case, or is it not?

    cache_result_t result = csdata->api->getKey(csdata->storage, query, csdata->key);

    if (result == CACHE_RESULT_OK)
    {
        result = csdata->api->getValue(csdata->storage, csdata->key, value);

        switch (result)
        {
        case CACHE_RESULT_OK:
            csdata->used_key = NULL;
            break;

        default:
            MXS_ERROR("Could not get value from cache storage.");
        case CACHE_RESULT_NOT_FOUND:
            csdata->used_key = csdata->key;
            break;
        }
    }
    else
    {
        MXS_ERROR("Could not create cache key.");
        csdata->used_key = NULL;
    }

    return result == CACHE_RESULT_OK;
}
