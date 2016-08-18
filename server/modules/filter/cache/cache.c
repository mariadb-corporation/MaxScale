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

#include <filter.h>
#include <modinfo.h>
#include <maxscale/alloc.h>

static char VERSION_STRING[] = "V1.0.0";

static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void   *newSession(FILTER *instance, SESSION *session);
static void    closeSession(FILTER *instance, void *sdata);
static void    freeSession(FILTER *instance, void *sdata);
static void    setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *downstream);
static void    setUpstream(FILTER *instance, void *sdata, UPSTREAM *upstream);
static int     routeQuery(FILTER *instance, void *sdata, GWBUF *queue);
static int     clientReply(FILTER *instance, void *sdata, GWBUF *queue);
static void    diagnostics(FILTER *instance, void *sdata, DCB *dcb);

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

typedef struct cache_instance
{
} CACHE_INSTANCE;

typedef struct cache_session_data
{
    DOWNSTREAM down;
    UPSTREAM up;
} CACHE_SESSION_DATA;

/**
 * Create an instance of the cache filter for a particular service
 * within MaxScale.
 *
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **params)
{
    CACHE_INSTANCE *cinstance;

    if ((cinstance = MXS_CALLOC(1, sizeof(CACHE_INSTANCE))) != NULL)
    {
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
 * @param queue     The query data
 */
static int routeQuery(FILTER *instance, void *sdata, GWBUF *queue)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    return csdata->down.routeQuery(csdata->down.instance, csdata->down.session, queue);
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
