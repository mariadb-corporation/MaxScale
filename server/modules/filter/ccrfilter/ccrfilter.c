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

#define MXS_MODULE_NAME "ccrfilter"

#include <stdio.h>
#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>
#include <string.h>
#include <maxscale/hint.h>
#include <maxscale/query_classifier.h>
#include <regex.h>
#include <maxscale/alloc.h>

/**
 * @file ccrfilter.c - a very simple filter designed to send queries to the
 * master server after data modification has occurred. This is done to prevent
 * replication lag affecting the outcome of a select query.
 *
 * @verbatim
 *
 * Two optional parameters that define the behavior after a data modifying query
 * is executed:
 *
 *      count=<number of queries>   Queries to route to master after data modification.
 *      time=<time period>          Seconds to wait before queries are routed to slaves.
 *      match=<regex>               Regex for matching
 *      ignore=<regex>              Regex for ignoring
 *
 * The filter also has two options:
 *     @c case, which makes the regex case-sensitive, and
 *     @c ignorecase, which does the opposite.
 *
 * Date         Who             Description
 * 03/03/2015   Markus Mäkelä   Written for demonstrative purposes
 * 10/08/2016   Markus Mäkelä   Cleaned up code and renamed to ccrfilter
 * @endverbatim
 */

static  MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params);
static  MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static  void   closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void   freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void   setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static  int    routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static  void   diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(MXS_FILTER* instance);

#define CCR_DEFAULT_TIME "60"

typedef struct lagstats
{
    int n_add_count;  /*< No. of statements diverted based on count */
    int n_add_time;   /*< No. of statements diverted based on time */
    int n_modified;   /*< No. of statements not diverted */
} LAGSTATS;

/**
 * Instance structure
 */
typedef struct
{
    char *match;     /* Regular expression to match */
    char *nomatch;   /* Regular expression to ignore */
    int  time;       /*< The number of seconds to wait before routing queries
                      * to slave servers after a data modification operation
                      * is done. */
    int count;       /*< Number of hints to add after each operation
                     * that modifies data. */
    LAGSTATS stats;
    regex_t re;      /* Compiled regex text of match */
    regex_t nore;    /* Compiled regex text of ignore */
} CCR_INSTANCE;

/**
 * The session structure for this filter
 */
typedef struct
{
    MXS_DOWNSTREAM down;              /*< The downstream filter */
    int            hints_left;        /*< Number of hints left to add to queries*/
    time_t         last_modification; /*< Time of the last data modifying operation */
} CCR_SESSION;

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE},
    {"case",       0},
    {"extended",   REG_EXTENDED},
    {NULL}
};

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
        NULL,               // No Upstream requirement
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        getCapabilities,
        NULL, // No destroyInstance
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A routing hint filter that send queries to the master after data modification",
        "V1.1.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"count", MXS_MODULE_PARAM_COUNT, "0"},
            {"time", MXS_MODULE_PARAM_COUNT, CCR_DEFAULT_TIME},
            {"match", MXS_MODULE_PARAM_STRING},
            {"ignore", MXS_MODULE_PARAM_STRING},
            {
             "options",
             MXS_MODULE_PARAM_ENUM,
             "ignorecase",
             MXS_MODULE_OPT_NONE,
             option_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name     The name of the instance (as defined in the config file).
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    CCR_INSTANCE *my_instance = MXS_CALLOC(1, sizeof(CCR_INSTANCE));

    if (my_instance)
    {
        my_instance->count = config_get_integer(params, "count");
        my_instance->time = config_get_integer(params, "time");
        my_instance->stats.n_add_count = 0;
        my_instance->stats.n_add_time = 0;
        my_instance->stats.n_modified = 0;

        int cflags = config_get_enum(params, "options", option_values);

        if ((my_instance->match = config_copy_string(params, "match")))
        {
            if (regcomp(&my_instance->re, my_instance->match, cflags))
            {
                MXS_ERROR("Failed to compile regex '%s'.", my_instance->match);
            }
        }

        if ((my_instance->nomatch = config_copy_string(params, "ignore")))
        {
            if (regcomp(&my_instance->nore, my_instance->nomatch, cflags))
            {
                MXS_ERROR("Failed to compile regex '%s'.", my_instance->nomatch);
            }
        }
    }

    return (MXS_FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 *
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    CCR_SESSION  *my_session = MXS_MALLOC(sizeof(CCR_SESSION));

    if (my_session)
    {
        my_session->hints_left = 0;
        my_session->last_modification = 0;
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
 * @param instance    The filter instance data
 * @param session     The session being closed
 * @param downstream  The downstream filter or router
 */
static void
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    CCR_SESSION *my_session = (CCR_SESSION *)session;

    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * If the regular expressed configured in the match parameter of the
 * filter definition matches the SQL text then add the hint
 * "Route to named server" with the name defined in the server parameter
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    CCR_INSTANCE *my_instance = (CCR_INSTANCE *)instance;
    CCR_SESSION  *my_session = (CCR_SESSION *)session;
    char *sql;
    regmatch_t limits[] = {{0, 0}};
    time_t now = time(NULL);

    if (modutil_is_SQL(queue))
    {
        /**
         * Not a simple SELECT statement, possibly modifies data. If we're processing a statement
         * with unknown query type, the safest thing to do is to treat it as a data modifying statement.
         */
        if (qc_query_is_type(qc_get_type_mask(queue), QUERY_TYPE_WRITE))
        {
            if (modutil_extract_SQL(queue, &sql, &limits[0].rm_eo))
            {
                if (my_instance->nomatch == NULL ||
                    (my_instance->nomatch && regexec(&my_instance->nore, sql, 0, limits, REG_STARTEND) != 0))
                {
                    if (my_instance->match == NULL ||
                        (my_instance->match && regexec(&my_instance->re, sql, 0, limits, REG_STARTEND) == 0))
                    {
                        if (my_instance->count)
                        {
                            my_session->hints_left = my_instance->count;
                            MXS_INFO("Write operation detected, next %d queries routed to master", my_instance->count);
                        }

                        if (my_instance->time)
                        {
                            my_session->last_modification = now;
                            MXS_INFO("Write operation detected, queries routed to master for %d seconds", my_instance->time);
                        }

                        my_instance->stats.n_modified++;
                    }
                }
            }
        }
        else if (my_session->hints_left > 0)
        {
            queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
            my_session->hints_left--;
            my_instance->stats.n_add_count++;
            MXS_INFO("%d queries left", my_instance->time);
        }
        else if (my_instance->time)
        {
            double dt = difftime(now, my_session->last_modification);

            if (dt < my_instance->time)
            {
                queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
                my_instance->stats.n_add_time++;
                MXS_INFO("%.0f seconds left", dt);
            }
        }
    }

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
 * @param instance  The filter instance
 * @param fsession  Filter session, may be NULL
 * @param dcb       The DCB for diagnostic output
 */
static void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    CCR_INSTANCE *my_instance = (CCR_INSTANCE *)instance;

    dcb_printf(dcb, "Configuration:\n\tCount: %d\n", my_instance->count);
    dcb_printf(dcb, "\tTime: %d seconds\n", my_instance->time);

    if (my_instance->match)
    {
        dcb_printf(dcb, "\tMatch regex: %s\n", my_instance->match);
    }

    if (my_instance->nomatch)
    {
        dcb_printf(dcb, "\tExclude regex: %s\n", my_instance->nomatch);
    }

    dcb_printf(dcb, "\nStatistics:\n");
    dcb_printf(dcb, "\tNo. of data modifications: %d\n", my_instance->stats.n_modified);
    dcb_printf(dcb, "\tNo. of hints added based on count: %d\n", my_instance->stats.n_add_count);
    dcb_printf(dcb, "\tNo. of hints added based on time: %d\n",  my_instance->stats.n_add_time);
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}
