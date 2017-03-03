/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <stdio.h>
#include <maxscale/filter.h>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/atomic.h>

/**
 * @file testfilter.c - a very simple test filter.
 * @verbatim
 *
 * This filter is a very simple example used to test the filter API,
 * it merely counts the number of statements that flow through the
 * filter pipeline.
 *
 * Reporting is done via the diagnostics print routine.
 * @endverbatim
 */


static  MXS_FILTER  *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params);
static  MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static  void    closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void    freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void    setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static  int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static  void    diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(MXS_FILTER* instance);
static void destroyInstance(MXS_FILTER *instance);




/**
 * A dummy instance structure
 */
typedef struct
{
    const char *name;
    int     sessions;
} TEST_INSTANCE;

/**
 * A dummy session structure for this test filter
 */
typedef struct
{
    MXS_DOWNSTREAM down;
    int count;
} TEST_SESSION;

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        NULL,  // No upstream requirement
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        getCapabilities,
        destroyInstance,
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_BETA_RELEASE,
        MXS_FILTER_VERSION,
        "A simple query counting filter",
        "V2.0.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static  MXS_FILTER  *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    TEST_INSTANCE   *my_instance;

    if ((my_instance = MXS_CALLOC(1, sizeof(TEST_INSTANCE))) != NULL)
    {
        my_instance->sessions = 0;
        my_instance->name = name;
    }
    return (MXS_FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    TEST_INSTANCE   *my_instance = (TEST_INSTANCE *)instance;
    TEST_SESSION    *my_session;

    if ((my_session = MXS_CALLOC(1, sizeof(TEST_SESSION))) != NULL)
    {
        atomic_add(&my_instance->sessions, 1);
        my_session->count = 0;
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static  void
closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    MXS_FREE(session);
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    TEST_SESSION    *my_session = (TEST_SESSION *)session;

    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query shoudl normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static  int
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    TEST_SESSION    *my_session = (TEST_SESSION *)session;

    if (modutil_is_SQL(queue))
    {
        my_session->count++;
    }
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static  void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    TEST_INSTANCE   *my_instance = (TEST_INSTANCE *)instance;
    TEST_SESSION    *my_session = (TEST_SESSION *)fsession;

    if (my_session)
        dcb_printf(dcb, "\t\tNo. of queries routed by filter: %d\n",
                   my_session->count);
    else
        dcb_printf(dcb, "\t\tNo. of sessions created: %d\n",
                   my_instance->sessions);
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}

/**
 * destroyInstance routine.
 *
 * @param The filter instance.
 */
static void destroyInstance(MXS_FILTER *instance)
{
    TEST_INSTANCE *cinstance = (TEST_INSTANCE *)instance;

    MXS_INFO("Destroying filter %s", cinstance->name);
}
