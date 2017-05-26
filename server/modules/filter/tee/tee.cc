/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file tee.cc  A filter that splits the processing pipeline in two
 */

#define MXS_MODULE_NAME "tee"

#include <maxscale/cppdefs.hh>

#include <regex.h>
#include <set>
#include <string>

#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>
#include <maxscale/service.h>
#include <maxscale/alloc.h>

#include "local_client.hh"

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
struct Tee
{
    SERVICE *service; /* The service to duplicate requests to */
    char *source; /* The source of the client connection */
    char *user; /* The user name to filter on */
    char *match; /* Optional text to match against */
    regex_t re; /* Compiled regex text */
    char *nomatch; /* Optional text to match against for exclusion */
    regex_t nore; /* Compiled regex nomatch text */
};

/**
 * The session structure for this TEE filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
struct TeeSession
{
    MXS_DOWNSTREAM down;    /**< The downstream filter */
    MXS_UPSTREAM   up;      /**< The upstream filter */
    bool           passive; /**< Whether to clone queries */
    LocalClient*   client;  /**< The client connection to the local service */
};

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE},
    {"case",       0},
    {"extended",   REG_EXTENDED},
    {NULL}
};

bool recursive_tee_usage(std::set<std::string>& services, SERVICE* service);

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
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    Tee *my_instance = new (std::nothrow) Tee;

    if (my_instance)
    {
        my_instance->service = config_get_service(params, "service");
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");
        my_instance->match = config_copy_string(params, "match");
        my_instance->nomatch = config_copy_string(params, "exclude");

        int cflags = config_get_enum(params, "options", option_values);

        if (my_instance->match && regcomp(&my_instance->re, my_instance->match, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s' for the match parameter.",
                      my_instance->match);
            MXS_FREE(my_instance->match);
            MXS_FREE(my_instance->nomatch);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user);
            delete my_instance;
            return NULL;
        }

        if (my_instance->nomatch && regcomp(&my_instance->nore, my_instance->nomatch, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s' for the nomatch paramter.",
                      my_instance->nomatch);
            if (my_instance->match)
            {
                regfree(&my_instance->re);
                MXS_FREE(my_instance->match);
            }
            MXS_FREE(my_instance->nomatch);
            MXS_FREE(my_instance->source);
            MXS_FREE(my_instance->user);
            delete my_instance;
            return NULL;
        }
    }

    return (MXS_FILTER*) my_instance;
}

/**
 * Create a filter new session
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 *
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    std::set<std::string> services;

    if (recursive_tee_usage(services, session->service))
    {
        MXS_ERROR("%s: Recursive use of tee filter in service.",
                  session->service->name);
        return NULL;
    }

    TeeSession* my_session = new (std::nothrow) TeeSession;

    if (my_session)
    {
        Tee *my_instance = reinterpret_cast<Tee*>(instance);
        const char* remote = session_get_remote(session);
        const char* user = session_get_user(session);

        if ((my_instance->source && remote && strcmp(remote, my_instance->source) != 0) ||
            (my_instance->user && user && strcmp(user, my_instance->user) != 0))
        {
            my_session->passive = true;
            my_session->client = NULL;
        }
        else
        {
            my_session->client = LocalClient::create(session, my_instance->service);
            my_session->passive = false;

            if (my_session->client == NULL)
            {
                delete my_session;
                my_session = NULL;
            }
        }
    }

    return reinterpret_cast<MXS_FILTER_SESSION*>(my_session);
}

/**
 * Close the filter session
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    TeeSession *my_session = reinterpret_cast<TeeSession*>(session);
    delete my_session->client;
    delete my_session;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param downstream    The downstream filter or router.
 */
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
{
    TeeSession *my_session = reinterpret_cast<TeeSession*>(session);
    my_session->down = *downstream;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param downstream    The downstream filter or router.
 */
static void setUpstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_UPSTREAM *upstream)
{
    TeeSession *my_session = reinterpret_cast<TeeSession*>(session);
    my_session->up = *upstream;
}

/**
 * Route a query
 *
 * @param instance  Filter instance
 * @param session   Filter session
 * @param queue     The query itself
 *
 * @retrn 1 on success, 0 on failure
 */
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    TeeSession *my_session = reinterpret_cast<TeeSession*>(session);

    int rval = my_session->down.routeQuery(my_session->down.instance,
                                           my_session->down.session,
                                           queue);

    my_session->client->queue_query(queue);

    return rval;
}

/**
 * The clientReply entry point. This is passed the response buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the upstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param reply     The response data
 */
static int
clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION *session, GWBUF *reply)
{
    TeeSession *my_session = reinterpret_cast<TeeSession*>(session);

    return my_session->up.clientReply(my_session->up.instance,
                                      my_session->up.session,
                                      reply);
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
    Tee *my_instance = reinterpret_cast<Tee*>(instance);

    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tLimit to connections from 		%s\n",
                   my_instance->source);
    }
    dcb_printf(dcb, "\t\tDuplicate statements to service		%s\n",
               my_instance->service->name);
    if (my_instance->user)
    {
        dcb_printf(dcb, "\t\tLimit to user			%s\n",
                   my_instance->user);
    }
    if (my_instance->match)
    {
        dcb_printf(dcb, "\t\tInclude queries that match		%s\n",
                   my_instance->match);
    }
    if (my_instance->nomatch)
    {
        dcb_printf(dcb, "\t\tExclude queries that match		%s\n",
                   my_instance->nomatch);
    }
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
 */
static json_t* diagnostic_json(const MXS_FILTER *instance, const MXS_FILTER_SESSION *fsession)
{
    const Tee *my_instance = reinterpret_cast<const Tee*>(instance);

    json_t* rval = json_object();

    if (my_instance->source)
    {
        json_object_set_new(rval, "source", json_string(my_instance->source));
    }

    json_object_set_new(rval, "service", json_string(my_instance->service->name));

    if (my_instance->user)
    {
        json_object_set_new(rval, "user", json_string(my_instance->user));
    }

    if (my_instance->match)
    {
        json_object_set_new(rval, "match", json_string(my_instance->match));
    }

    if (my_instance->nomatch)
    {
        json_object_set_new(rval, "exclude", json_string(my_instance->nomatch));
    }

    return rval;
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

/**
 * Detect loops in the filter chain.
 */
bool recursive_tee_usage(std::set<std::string>& services, SERVICE* service)
{
    if (!services.insert(service->name).second)
    {
        /** The service name was already in the set */
        return true;
    }

    for (int i = 0; i < service->n_filters; i++)
    {
        const char* module = filter_def_get_module_name(service->filters[i]);

        if (strcmp(module, "tee") == 0)
        {
            /*
             * Found a Tee filter, recurse down its path
             * if the service name isn't already in the hashtable.
             */
            Tee* inst = (Tee*)filter_def_get_instance(service->filters[i]);

            if (inst == NULL)
            {
                /**
                 * This tee instance hasn't been initialized yet and full
                 * resolution of recursion cannot be done now.
                 */
            }
            else if (recursive_tee_usage(services, inst->service))
            {
                return true;
            }
        }
    }

    return false;
}

MXS_BEGIN_DECLS

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
        setUpstream,
        routeQuery,
        clientReply,
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
        "A tee piece in the filter plumbing",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"service", MXS_MODULE_PARAM_SERVICE, NULL, MXS_MODULE_OPT_REQUIRED},
            {"match", MXS_MODULE_PARAM_STRING},
            {"exclude", MXS_MODULE_PARAM_STRING},
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

MXS_END_DECLS
