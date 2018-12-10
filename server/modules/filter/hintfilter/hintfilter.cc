/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "hintfilter"

#include <stdio.h>
#include <maxscale/filter.hh>
#include <maxscale/alloc.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include "mysqlhint.hh"

/**
 * hintfilter.c - a filter to parse the MaxScale hint syntax and attach those
 * hints to the buffers that carry the requests.
 *
 */

static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER* params);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                setDownstream(MXS_FILTER* instance,
                                         MXS_FILTER_SESSION* fsession,
                                         MXS_DOWNSTREAM* downstream);
static int      routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

extern "C"
{

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
            NULL,   // No upstream requirement
            routeQuery,
            NULL,   // No clientReply
            diagnostic,
            diagnostic_json,
            getCapabilities,
            NULL,   // No destroyInstance
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_ALPHA_RELEASE,
            MXS_FILTER_VERSION,
            "A hint parsing filter",
            "V1.0.0",
            RCAP_TYPE_CONTIGUOUS_INPUT,
            &MyObject,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
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
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    HINT_INSTANCE* my_instance;

    if ((my_instance = static_cast<HINT_INSTANCE*>(MXS_CALLOC(1, sizeof(HINT_INSTANCE)))) != NULL)
    {
        my_instance->sessions = 0;
    }
    return (MXS_FILTER*)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    HINT_INSTANCE* my_instance = (HINT_INSTANCE*)instance;
    HINT_SESSION* my_session;

    if ((my_session = static_cast<HINT_SESSION*>(MXS_CALLOC(1, sizeof(HINT_SESSION)))) != NULL)
    {
        my_session->query_len = 0;
        my_session->request = NULL;
        my_session->stack = NULL;
        my_session->named_hints = NULL;
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
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;
    NAMEDHINTS* named_hints;
    HINTSTACK* hint_stack;

    if (my_session->request)
    {
        gwbuf_free(my_session->request);
    }


    /** Free named hints */
    named_hints = my_session->named_hints;

    while ((named_hints = free_named_hint(named_hints)) != NULL)
    {
    }
    /** Free stacked hints */
    hint_stack = my_session->stack;

    while ((hint_stack = free_hint_stack(hint_stack)) != NULL)
    {
    }
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
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
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;

    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    HINT_SESSION* my_session = (HINT_SESSION*)session;

    if (modutil_is_SQL(queue))
    {
        my_session->request = NULL;
        my_session->query_len = 0;
        HINT* new_hint = hint_parser(my_session, queue);
        if (new_hint)
        {
            queue->hint = hint_splice(queue->hint, new_hint);
        }
    }

    /* Now process the request */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session,
                                       queue);
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
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    HINT_INSTANCE* my_instance = (HINT_INSTANCE*)instance;
    HINT_SESSION* my_session = (HINT_SESSION*)fsession;
}

/**
 * Diagnostics routine
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    return NULL;
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
