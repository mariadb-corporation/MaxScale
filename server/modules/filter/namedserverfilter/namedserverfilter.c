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
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>
#include <string.h>
#include <regex.h>
#include <maxscale/hint.h>
#include <maxscale/alloc.h>

/**
 * @file namedserverfilter.c - a very simple regular expression based filter
 * that routes to a named server if a regular expression match is found.
 * @verbatim
 *
 * A simple regular expression based query routing filter.
 * Two parameters should be defined in the filter configuration
 *      match=<regular expression>
 *      server=<server to route statement to>
 * Two optional parameters
 *      source=<source address to limit filter>
 *      user=<username to limit filter>
 *
 * Date         Who             Description
 * 22/01/2015   Mark Riddoch    Written as example based on regex filter
 * @endverbatim
 */

static MXS_FILTER *createInstance(const char *name, char **options, CONFIG_PARAMETER *params);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DOWNSTREAM *downstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(void);

/**
 * Instance structure
 */
typedef struct
{
    char *source; /* Source address to restrict matches */
    char *user; /* User name to restrict matches */
    char *match; /* Regular expression to match */
    char *server; /* Server to route to */
    regex_t re; /* Compiled regex text */
} REGEXHINT_INSTANCE;

/**
 * The session structuee for this regex filter
 */
typedef struct
{
    DOWNSTREAM down; /* The downstream filter */
    int n_diverted; /* No. of statements diverted */
    int n_undiverted; /* No. of statements not diverted */
    int active; /* Is filter active */
} REGEXHINT_SESSION;

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE},
    {"case", 0},
    {"extended", REG_EXTENDED},
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
        NULL, // No Upstream requirement
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
        FILTER_VERSION,
        "A routing hint filter that uses regular expressions to direct queries",
        "V1.1.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"match", MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {"server", MXS_MODULE_PARAM_SERVER, NULL, MXS_MODULE_OPT_REQUIRED},
            {"source", MXS_MODULE_PARAM_STRING},
            {"user", MXS_MODULE_PARAM_STRING},
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
 * @param params    The array of name/value pair parameters for the filter
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, CONFIG_PARAMETER *params)
{
    REGEXHINT_INSTANCE *my_instance = (REGEXHINT_INSTANCE*)MXS_MALLOC(sizeof(REGEXHINT_INSTANCE));

    if (my_instance)
    {
        my_instance->match = MXS_STRDUP_A(config_get_string(params, "match"));
        my_instance->server = MXS_STRDUP_A(config_get_string(params, "server"));
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");
        bool error = false;

        int cflags = config_get_enum(params, "options", option_values);

        if (regcomp(&my_instance->re, my_instance->match, cflags))
        {
            MXS_ERROR("namedserverfilter: Invalid regular expression '%s'.", my_instance->match);
            MXS_FREE(my_instance->match);
            my_instance->match = NULL;
            error = true;
        }

        if (error)
        {
            if (my_instance->match)
            {
                regfree(&my_instance->re);
                MXS_FREE(my_instance->match);
            }
            MXS_FREE(my_instance->server);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user);
            MXS_FREE(my_instance);
            my_instance = NULL;
        }
    }

    return (MXS_FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, SESSION *session)
{
    REGEXHINT_INSTANCE *my_instance = (REGEXHINT_INSTANCE *) instance;
    REGEXHINT_SESSION *my_session;
    const char *remote, *user;

    if ((my_session = MXS_CALLOC(1, sizeof(REGEXHINT_SESSION))) != NULL)
    {
        my_session->n_diverted = 0;
        my_session->n_undiverted = 0;
        my_session->active = 1;
        if (my_instance->source
            && (remote = session_get_remote(session)) != NULL)
        {
            if (strcmp(remote, my_instance->source))
            {
                my_session->active = 0;
            }
        }

        if (my_instance->user && (user = session_get_user(session))
            && strcmp(user, my_instance->user))
        {
            my_session->active = 0;
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
static void
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
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, DOWNSTREAM *downstream)
{
    REGEXHINT_SESSION *my_session = (REGEXHINT_SESSION *) session;
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
    REGEXHINT_INSTANCE *my_instance = (REGEXHINT_INSTANCE *) instance;
    REGEXHINT_SESSION *my_session = (REGEXHINT_SESSION *) session;
    char *sql;

    if (modutil_is_SQL(queue) && my_session->active)
    {
        if ((sql = modutil_get_SQL(queue)) != NULL)
        {
            if (regexec(&my_instance->re, sql, 0, NULL, 0) == 0)
            {
                queue->hint = hint_create_route(queue->hint,
                                                HINT_ROUTE_TO_NAMED_SERVER,
                                                my_instance->server);
                my_session->n_diverted++;
            }
            else
            {
                my_session->n_undiverted++;
            }
            MXS_FREE(sql);
        }
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
static void
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
{
    REGEXHINT_INSTANCE *my_instance = (REGEXHINT_INSTANCE *) instance;
    REGEXHINT_SESSION *my_session = (REGEXHINT_SESSION *) fsession;

    dcb_printf(dcb, "\t\tMatch and route:           /%s/ -> %s\n",
               my_instance->match, my_instance->server);
    if (my_session)
    {
        dcb_printf(dcb, "\t\tNo. of queries diverted by filter: %d\n",
                   my_session->n_diverted);
        dcb_printf(dcb, "\t\tNo. of queries not diverted by filter:     %d\n",
                   my_session->n_undiverted);
    }
    if (my_instance->source)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limited to connections from     %s\n",
                   my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limit to user           %s\n",
                   my_instance->user);
    }
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}
