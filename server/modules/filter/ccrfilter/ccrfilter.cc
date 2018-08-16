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

#define MXS_MODULE_NAME "ccrfilter"

#include <maxscale/cdefs.h>

#include <stdio.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/hint.h>
#include <maxscale/log.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/pcre2.h>
#include <maxscale/query_classifier.h>

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

static  MXS_FILTER *createInstance(const char *name, MXS_CONFIG_PARAMETER *params);
static  MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static  void   closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void   freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static  void   setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static  int    routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static  void   diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static  json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession);
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
    pcre2_code* re;        /* Compiled regex text of match */
    pcre2_code* nore;      /* Compiled regex text of ignore */
    uint32_t ovector_size; /* PCRE2 match data ovector size */
} CCR_INSTANCE;

/**
 * The session structure for this filter
 */
typedef struct
{
    MXS_DOWNSTREAM down;              /*< The downstream filter */
    int            hints_left;        /*< Number of hints left to add to queries*/
    time_t         last_modification; /*< Time of the last data modifying operation */
    pcre2_match_data* md;             /*< PCRE2 match data */
} CCR_SESSION;

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0},
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

static const char PARAM_MATCH[] = "match";
static const char PARAM_IGNORE[] = "ignore";

typedef enum ccr_hint_value_t
{
    CCR_HINT_NONE,
    CCR_HINT_MATCH,
    CCR_HINT_IGNORE
} CCR_HINT_VALUE;

static CCR_HINT_VALUE search_ccr_hint(GWBUF* buffer);

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
        NULL,               // No Upstream requirement
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        diagnostic_json,
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
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"count", MXS_MODULE_PARAM_COUNT, "0"},
            {"time", MXS_MODULE_PARAM_COUNT, CCR_DEFAULT_TIME},
            {PARAM_MATCH, MXS_MODULE_PARAM_REGEX},
            {PARAM_IGNORE, MXS_MODULE_PARAM_REGEX},
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
createInstance(const char *name, MXS_CONFIG_PARAMETER *params)
{
    CCR_INSTANCE *my_instance = static_cast<CCR_INSTANCE*>(MXS_CALLOC(1, sizeof(CCR_INSTANCE)));

    if (my_instance)
    {
        my_instance->count = config_get_integer(params, "count");
        my_instance->time = config_get_integer(params, "time");
        my_instance->stats.n_add_count = 0;
        my_instance->stats.n_add_time = 0;
        my_instance->stats.n_modified = 0;
        my_instance->ovector_size = 0;
        my_instance->re = NULL;
        my_instance->nore = NULL;

        int cflags = config_get_enum(params, "options", option_values);
        my_instance->match = config_copy_string(params, PARAM_MATCH);
        my_instance->nomatch = config_copy_string(params, PARAM_IGNORE);
        const char* keys[] = {PARAM_MATCH, PARAM_IGNORE};
        pcre2_code** code_arr[] = {&my_instance->re, &my_instance->nore};

        if (!config_get_compiled_regexes(params, keys, sizeof(keys) / sizeof(char*),
                                         cflags, &my_instance->ovector_size,
                                         code_arr))
        {
            MXS_FREE(my_instance->match);
            MXS_FREE(my_instance->nomatch);
            pcre2_code_free(my_instance->re);
            pcre2_code_free(my_instance->nore);
            MXS_FREE(my_instance);
            my_instance = NULL;
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
    CCR_INSTANCE *my_instance = (CCR_INSTANCE *)instance;
    CCR_SESSION  *my_session = static_cast<CCR_SESSION*>(MXS_MALLOC(sizeof(CCR_SESSION)));

    if (my_session)
    {
        bool error = false;
        my_session->hints_left = 0;
        my_session->last_modification = 0;
        if (my_instance->ovector_size)
        {
            my_session->md = pcre2_match_data_create(my_instance->ovector_size, NULL);
            if (!my_session->md)
            {
                MXS_FREE(my_session);
                my_session = NULL;
            }
        }
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
    int length;
    time_t now = time(NULL);

    if (modutil_is_SQL(queue))
    {
        /**
         * Not a simple SELECT statement, possibly modifies data. If we're processing a statement
         * with unknown query type, the safest thing to do is to treat it as a data modifying statement.
         */
        if (qc_query_is_type(qc_get_type_mask(queue), QUERY_TYPE_WRITE))
        {
            if (modutil_extract_SQL(queue, &sql, &length))
            {
                bool trigger_ccr = true;
                bool decided = false; // Set by hints to take precedence.
                CCR_HINT_VALUE ccr_hint_val = search_ccr_hint(queue);
                if (ccr_hint_val == CCR_HINT_IGNORE)
                {
                    trigger_ccr = false;
                    decided = true;
                }
                else if (ccr_hint_val == CCR_HINT_MATCH)
                {
                    decided = true;
                }
                if (!decided)
                {
                    trigger_ccr =
                        mxs_pcre2_check_match_exclude(my_instance->re, my_instance->nore,
                                                      my_session->md, sql, length,
                                                      MXS_MODULE_NAME);
                }
                if (trigger_ccr)
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
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession)
{
    CCR_INSTANCE *my_instance = (CCR_INSTANCE *)instance;
    json_t* rval = json_object();

    json_object_set_new(rval, "count", json_integer(my_instance->count));
    json_object_set_new(rval, "time", json_integer(my_instance->time));

    if (my_instance->match)
    {
        json_object_set_new(rval, PARAM_MATCH, json_string(my_instance->match));
    }

    if (my_instance->nomatch)
    {
        json_object_set_new(rval, "nomatch", json_string(my_instance->nomatch));
    }

    json_object_set_new(rval, "data_modifications", json_integer(my_instance->stats.n_modified));
    json_object_set_new(rval, "hints_added_count", json_integer(my_instance->stats.n_add_count));
    json_object_set_new(rval, "hints_added_time", json_integer(my_instance->stats.n_add_time));

    return rval;
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
 * Find the first CCR filter hint. The hint is removed from the buffer and the
 * contents returned.
 *
 * @param buffer Input buffer
 * @return The found ccr hint value
 */
static CCR_HINT_VALUE search_ccr_hint(GWBUF* buffer)
{
    const char CCR[] = "ccr";
    CCR_HINT_VALUE rval = CCR_HINT_NONE;
    bool found_ccr = false;
    HINT** prev_ptr = &buffer->hint;
    HINT* hint = buffer->hint;

    while (hint && !found_ccr)
    {
        if (hint->type == HINT_PARAMETER && strcasecmp(static_cast<char*>(hint->data), CCR) == 0)
        {
            found_ccr = true;
            if (strcasecmp(static_cast<char*>(hint->value), "match") == 0)
            {
                rval = CCR_HINT_MATCH;
            }
            else if (strcasecmp(static_cast<char*>(hint->value), "ignore") == 0)
            {
                rval = CCR_HINT_IGNORE;
            }
            else
            {
                MXS_ERROR("Unknown value for hint parameter %s: '%s'.",
                          CCR, (char*)hint->value);
            }
        }
        else
        {
            prev_ptr = &hint->next;
            hint = hint->next;
        }
    }
    // Remove the ccr-hint from the hint chain. Otherwise rwsplit will complain.
    if (found_ccr)
    {
        *prev_ptr = hint->next;
        hint_free(hint);
    }
    return rval;
}
