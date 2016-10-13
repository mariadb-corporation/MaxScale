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
#include <stdio.h>
#include <maxscale/filter.h>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include "mysqlhint.h"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_ALPHA_RELEASE,
    FILTER_VERSION,
    "A hint parsing filter"
};

static char *version_str = "V1.0.0";

static FILTER *createInstance(const char* name, char **options, FILTER_PARAMETER **params);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL, // No upstream requirement
    routeQuery,
    NULL,
    diagnostic,
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 * @see function load_module in load_utils.c for explanation of lint
 */

/*lint -e14 */
void
ModuleInit()
{
}
/*lint +e14 */

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT *
GetModuleObject()
{
    return &MyObject;
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
static FILTER *
createInstance(const char *name, char **options, FILTER_PARAMETER **params)
{
    HINT_INSTANCE *my_instance;

    if ((my_instance = MXS_CALLOC(1, sizeof(HINT_INSTANCE))) != NULL)
    {
        my_instance->sessions = 0;
    }
    return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    HINT_INSTANCE *my_instance = (HINT_INSTANCE *)instance;
    HINT_SESSION *my_session;

    if ((my_session = MXS_CALLOC(1, sizeof(HINT_SESSION))) != NULL)
    {
        my_session->query_len = 0;
        my_session->request = NULL;
        my_session->stack = NULL;
        my_session->named_hints = NULL;
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(FILTER *instance, void *session)
{
    HINT_SESSION *my_session = (HINT_SESSION *)session;
    NAMEDHINTS* named_hints;
    HINTSTACK* hint_stack;

    if (my_session->request)
    {
        gwbuf_free(my_session->request);
    }


    /** Free named hints */
    named_hints = my_session->named_hints;

    while ((named_hints = free_named_hint(named_hints)) != NULL)
        ;
    /** Free stacked hints */
    hint_stack = my_session->stack;

    while ((hint_stack = free_hint_stack(hint_stack)) != NULL)
        ;
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
freeSession(FILTER *instance, void *session)
{
    MXS_FREE(session);
    return;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    HINT_SESSION *my_session = (HINT_SESSION *)session;

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
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    HINT_SESSION *my_session = (HINT_SESSION *)session;
    char *ptr;
    int rval, len, residual;
    HINT *hint;

    if (my_session->request == NULL)
    {
        /*
         * No stored buffer, so this must be the first
         * buffer of a new request.
         */
        if (modutil_MySQL_Query(queue, &ptr, &len, &residual) == 0)
        {
            return my_session->down.routeQuery(
                       my_session->down.instance,
                       my_session->down.session, queue);
        }
        my_session->request = queue;
        my_session->query_len = len;
    }
    else
    {
        gwbuf_append(my_session->request, queue);
    }

    if (gwbuf_length(my_session->request) < my_session->query_len)
    {
        /*
         * We have not got the entire SQL text, buffer and wait for
         * the remainder.
         */
        return 1;
    }
    /* We have the entire SQL text, parse for hints and attach to the
     * buffer at the head of the queue.
     */
    queue = my_session->request;
    my_session->request = NULL;
    my_session->query_len = 0;
    hint = hint_parser(my_session, queue);
    queue->hint = hint;

    /* Now process the request */
    rval = my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
    return rval;
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
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    HINT_INSTANCE *my_instance = (HINT_INSTANCE *)instance;
    HINT_SESSION *my_session = (HINT_SESSION *)fsession;

}
