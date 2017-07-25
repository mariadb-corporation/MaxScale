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

#include "readwritesplit.h"

#include <inttypes.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>
#include "rwsplit_internal.h"

#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/alloc.h>

/**
 * @file readwritesplit.c   The entry points for the read/write query splitting
 * router module.
 *
 * This file contains the entry points that comprise the API to the read write
 * query splitting router. It also contains functions that are directly called
 * by the entry point functions. Some of these are used by functions in other
 * modules of the read write split router, others are used only within this
 * module.
 *
 * @verbatim
 * Revision History
 *
 * Date          Who                 Description
 * 01/07/2013    Vilho Raatikka      Initial implementation
 * 15/07/2013    Massimiliano Pinto  Added clientReply from master only in case
 *                                   of session change
 * 17/07/2013    Massimiliano Pinto  clientReply is now used by mysql_backend
 *                                   for all reply situations
 * 18/07/2013    Massimiliano Pinto  routeQuery now handles COM_QUIT
 *                                   as QUERY_TYPE_SESSION_WRITE
 * 17/07/2014    Massimiliano Pinto  Server connection counter is updated in
 * closeSession
 * 09/09/2015    Martin Brampton     Modify error handler
 * 25/09/2015    Martin Brampton     Block callback processing when no router
 * session in the DCB
 * 03/08/2016    Martin Brampton     Extract the API functions, move the rest
 *
 * @endverbatim
 */

/** Maximum number of slaves */
#define MAX_SLAVE_COUNT "255"

/*
 * The functions that implement the router module API
 */

static MXS_ROUTER *createInstance(SERVICE *service, char **options);
static MXS_ROUTER_SESSION *newSession(MXS_ROUTER *instance, MXS_SESSION *session);
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static void freeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session);
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *session, GWBUF *queue);
static void diagnostics(MXS_ROUTER *instance, DCB *dcb);
static void clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue,
                        DCB *backend_dcb);
static void handleError(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session,
                        GWBUF *errmsgbuf, DCB *backend_dcb,
                        mxs_error_action_t action, bool *succp);
static uint64_t getCapabilities(MXS_ROUTER* instance);

/*
 * End of the API functions; now the module structure that links to them.
 * Note that the function names are chosen to exactly match the names used in
 * the definition of ROUTER_OBJECT. This is not obligatory, but is done to
 * make it easier to track the connection between calls and functions.
 */

/*
 * Declaration of functions that are used only within this module, and are
 * not part of the API.
 */

static void free_rwsplit_instance(ROUTER_INSTANCE *router);
static bool rwsplit_process_router_options(ROUTER_INSTANCE *router,
                                           char **options);
static void handle_error_reply_client(MXS_SESSION *ses, ROUTER_CLIENT_SES *rses,
                                      DCB *backend_dcb, GWBUF *errmsg);
static bool handle_error_new_connection(ROUTER_INSTANCE *inst,
                                        ROUTER_CLIENT_SES **rses,
                                        DCB *backend_dcb, GWBUF *errmsg);
static bool have_enough_servers(ROUTER_CLIENT_SES *rses, const int min_nsrv,
                                int router_nsrv, ROUTER_INSTANCE *router);
static bool create_backends(ROUTER_CLIENT_SES *rses, backend_ref_t** dest, int* n_backend);

/**
 * Enum values for router parameters
 */
static const MXS_ENUM_VALUE use_sql_variables_in_values[] =
{
    {"all",    TYPE_ALL},
    {"master", TYPE_MASTER},
    {NULL}
};

static const MXS_ENUM_VALUE slave_selection_criteria_values[] =
{
    {"LEAST_GLOBAL_CONNECTIONS", LEAST_GLOBAL_CONNECTIONS},
    {"LEAST_ROUTER_CONNECTIONS", LEAST_ROUTER_CONNECTIONS},
    {"LEAST_BEHIND_MASTER",      LEAST_BEHIND_MASTER},
    {"LEAST_CURRENT_OPERATIONS", LEAST_CURRENT_OPERATIONS},
    {NULL}
};

static const MXS_ENUM_VALUE master_failure_mode_values[] =
{
    {"fail_instantly", RW_FAIL_INSTANTLY},
    {"fail_on_write",  RW_FAIL_ON_WRITE},
    {"error_on_write", RW_ERROR_ON_WRITE},
    {NULL}
};

/**
 * The module entry point routine. It is this routine that
 * must return the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE *MXS_CREATE_MODULE()
{
    static MXS_ROUTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostics,
        clientReply,
        handleError,
        getCapabilities,
        NULL
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER, MXS_MODULE_GA, MXS_ROUTER_VERSION,
        "A Read/Write splitting router for enhancement read scalability",
        "V1.1.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "use_sql_variables_in",
                MXS_MODULE_PARAM_ENUM,
                "all",
                MXS_MODULE_OPT_NONE,
                use_sql_variables_in_values
            },
            {
                "slave_selection_criteria",
                MXS_MODULE_PARAM_ENUM,
                "LEAST_CURRENT_OPERATIONS",
                MXS_MODULE_OPT_NONE,
                slave_selection_criteria_values
            },
            {
                "master_failure_mode",
                MXS_MODULE_PARAM_ENUM,
                "fail_instantly",
                MXS_MODULE_OPT_NONE,
                master_failure_mode_values
            },
            {"max_slave_replication_lag", MXS_MODULE_PARAM_INT, "-1"},
            {"max_slave_connections", MXS_MODULE_PARAM_STRING, MAX_SLAVE_COUNT},
            {"retry_failed_reads", MXS_MODULE_PARAM_BOOL, "true"},
            {"disable_sescmd_history", MXS_MODULE_PARAM_BOOL, "true"},
            {"max_sescmd_history", MXS_MODULE_PARAM_COUNT, "0"},
            {"strict_multi_stmt",  MXS_MODULE_PARAM_BOOL, "true"},
            {"master_accept_reads", MXS_MODULE_PARAM_BOOL, "false"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}

// TODO: Don't process parameters in readwritesplit
static bool handle_max_slaves(ROUTER_INSTANCE *router, const char *str)
{
    bool rval = true;
    char *endptr;
    int val = strtol(str, &endptr, 10);

    if (*endptr == '%' && *(endptr + 1) == '\0')
    {
        router->rwsplit_config.rw_max_slave_conn_percent = val;
        router->rwsplit_config.max_slave_connections = 0;
    }
    else if (*endptr == '\0')
    {
        router->rwsplit_config.max_slave_connections = val;
        router->rwsplit_config.rw_max_slave_conn_percent = 0;
    }
    else
    {
        MXS_ERROR("Invalid value for 'max_slave_connections': %s", str);
        rval = false;
    }

    return rval;
}

/**
 * @brief Create an instance of the read/write router (API).
 *
 * Create an instance of read/write statement router within the MaxScale. One
 * instance of the router is required for each service that is defined in the
 * configuration as using this router. One instance of the router will handle
 * multiple connections (or router sessions).
 *
 * @param service   The service this router is being create for
 * @param options   The options for this query router
 * @return NULL in failure, pointer to router in success.
 */
static MXS_ROUTER *createInstance(SERVICE *service, char **options)
{
    ROUTER_INSTANCE *router;

    if ((router = MXS_CALLOC(1, sizeof(ROUTER_INSTANCE))) == NULL)
    {
        return NULL;
    }
    router->service = service;

    /*
     * Until we know otherwise assume we have some available slaves.
     */
    router->available_slaves = true;

    /** By default, the client connection is closed immediately when a master
     * failure is detected */
    router->rwsplit_config.master_failure_mode = RW_FAIL_INSTANTLY;

    MXS_CONFIG_PARAMETER *params = service->svc_config_param;

    router->rwsplit_config.use_sql_variables_in = config_get_enum(params, "use_sql_variables_in",
                                                                  use_sql_variables_in_values);

    router->rwsplit_config.slave_selection_criteria = config_get_enum(params, "slave_selection_criteria",
                                                                      slave_selection_criteria_values);

    router->rwsplit_config.master_failure_mode = config_get_enum(params, "master_failure_mode",
                                                                 master_failure_mode_values);

    router->rwsplit_config.max_slave_replication_lag = config_get_integer(params, "max_slave_replication_lag");
    router->rwsplit_config.retry_failed_reads = config_get_bool(params, "retry_failed_reads");
    router->rwsplit_config.strict_multi_stmt = config_get_bool(params, "strict_multi_stmt");
    router->rwsplit_config.disable_sescmd_history = config_get_bool(params, "disable_sescmd_history");
    router->rwsplit_config.max_sescmd_history = config_get_integer(params, "max_sescmd_history");
    router->rwsplit_config.master_accept_reads = config_get_bool(params, "master_accept_reads");

    if (!handle_max_slaves(router, config_get_string(params, "max_slave_connections")) ||
        (options && !rwsplit_process_router_options(router, options)))
    {
        free_rwsplit_instance(router);
        return NULL;
    }

    /** These options cancel each other out */
    if (router->rwsplit_config.disable_sescmd_history &&
        router->rwsplit_config.max_sescmd_history > 0)
    {
        router->rwsplit_config.max_sescmd_history = 0;
    }

    return (MXS_ROUTER *)router;
}

/**
 * @brief Associate a new session with this instance of the router (API).
 *
 * The session is used to store all the data required by the router for a
 * particular client connection. The instance of the router that relates to a
 * particular service is passed as the first parameter. The second parameter is
 * the session that has been created in response to the request from a client
 * for a connection. The passed session contains generic information; this
 * function creates the session structure that holds router specific data.
 * There is often a one to one relationship between sessions and router
 * sessions, although it is possible to create configurations where a
 * connection is handled by multiple routers, one after another.
 *
 * @param instance  The router instance data
 * @param session   The MaxScale session (generic connection data)
 * @return Session specific data for this session, i.e. a router session
 */
static MXS_ROUTER_SESSION *newSession(MXS_ROUTER *router_inst, MXS_SESSION *session)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)router_inst;
    ROUTER_CLIENT_SES *client_rses = (ROUTER_CLIENT_SES *)MXS_CALLOC(1, sizeof(ROUTER_CLIENT_SES));

    if (client_rses == NULL)
    {
        return NULL;
    }
#if defined(SS_DEBUG)
    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

    client_rses->router = router;
    client_rses->client_dcb = session->client_dcb;
    client_rses->have_tmp_tables = false;
    client_rses->forced_node = NULL;
    memcpy(&client_rses->rses_config, &router->rwsplit_config, sizeof(client_rses->rses_config));

    int router_nservers = router->service->n_dbref;
    const int min_nservers = 1; /*< hard-coded for now */

    if (!have_enough_servers(client_rses, min_nservers, router_nservers, router))
    {
        MXS_FREE(client_rses);
        return NULL;
    }

    /**
     * Create backend reference objects for this session.
     */
    backend_ref_t *backend_ref;

    if (!create_backends(client_rses, &backend_ref, &router_nservers))
    {
        MXS_FREE(client_rses);
        return NULL;
    }

    int max_nslaves = rses_get_max_slavecount(client_rses, router_nservers);
    int max_slave_rlag = rses_get_max_replication_lag(client_rses);

    client_rses->rses_backend_ref = backend_ref;
    client_rses->rses_nbackends = router_nservers; /*< # of backend servers */

    backend_ref_t *master_ref = NULL; /*< pointer to selected master */
    if (!select_connect_backend_servers(&master_ref, backend_ref, router_nservers,
                                        max_nslaves, max_slave_rlag,
                                        client_rses->rses_config.slave_selection_criteria,
                                        session, router, false))
    {
        /**
         * Master and at least <min_nslaves> slaves must be found if the router is
         * in the strict mode. If sessions without master are allowed, only
         * <min_nslaves> slaves must be found.
         */
        MXS_FREE(client_rses->rses_backend_ref);
        MXS_FREE(client_rses);
        return NULL;
    }

    /** Copy backend pointers to router session. */
    client_rses->rses_master_ref = master_ref;

    if (client_rses->rses_config.rw_max_slave_conn_percent)
    {
        int n_conn = 0;
        double pct = (double)client_rses->rses_config.rw_max_slave_conn_percent / 100.0;
        n_conn = MXS_MAX(floor((double)client_rses->rses_nbackends * pct), 1);
        client_rses->rses_config.max_slave_connections = n_conn;
    }

    router->stats.n_sessions += 1;

    return (void *)client_rses;
}

/**
 * @brief Close a router session (API).
 *
 * Close a session with the router, this is the mechanism by which a router
 * may cleanup data structure etc. The instance of the router that relates to
 * the relevant service is passed, along with the router session that is to
 * be closed. Typically the function is used in conjunction with freeSession
 * which will release the resources used by a router session (see below).
 *
 * @param instance  The router instance data
 * @param session   The router session being closed
 */
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session)
{
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    CHK_CLIENT_RSES(router_cli_ses);

    if (!router_cli_ses->rses_closed)
    {
        /**
         * Mark router session as closed. @c rses_closed is checked at the start
         * of every API function to quickly stop the processing of closed sessions.
         */
        router_cli_ses->rses_closed = true;

        for (int i = 0; i < router_cli_ses->rses_nbackends; i++)
        {
            backend_ref_t *bref = &router_cli_ses->rses_backend_ref[i];

            if (BREF_IS_IN_USE(bref))
            {
                /** This backend is in use and it needs to be closed */
                DCB *dcb = bref->bref_dcb;
                CHK_DCB(dcb);
                ss_dassert(dcb->session->state == SESSION_STATE_STOPPING);

                if (BREF_IS_WAITING_RESULT(bref))
                {
                    /** This backend was executing a query when the session was closed */
                    bref_clear_state(bref, BREF_WAITING_RESULT);
                }
                bref_clear_state(bref, BREF_IN_USE);
                bref_set_state(bref, BREF_CLOSED);

                RW_CHK_DCB(bref, dcb);

                /** MXS-956: This will prevent closed DCBs from being closed twice.
                 * It should not happen but for currently unknown reasons, a DCB
                 * gets closed twice; first in handleError and a second time here. */
                if (dcb && dcb->state == DCB_STATE_POLLING)
                {
                    dcb_close(dcb);
                }

                RW_CLOSE_BREF(bref);

                /** decrease server current connection counters */
                atomic_add(&bref->ref->connections, -1);
            }
            else
            {
                ss_dassert(!BREF_IS_WAITING_RESULT(bref));

                /** This should never be true unless a backend reference is taken
                 * out of use before clearing the BREF_WAITING_RESULT state */
                if (BREF_IS_WAITING_RESULT(bref))
                {
                    MXS_WARNING("A closed backend was expecting a result, this should not be possible. "
                                "Decrementing active operation counter for this backend.");
                    bref_clear_state(bref, BREF_WAITING_RESULT);
                }
            }
        }
    }
}

/**
 * @brief Free a router session (API).
 *
 * When a router session has been closed, freeSession can be called to free
 * allocated resources.
 *
 * @param router_instance   The router instance the session belongs to
 * @param router_client_session Client session
 *
 */
static void freeSession(MXS_ROUTER *router_instance, MXS_ROUTER_SESSION *router_client_session)
{
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_client_session;

    /**
     * For each property type, walk through the list, finalize properties
     * and free the allocated memory.
     */
    for (int i = RSES_PROP_TYPE_FIRST; i < RSES_PROP_TYPE_COUNT; i++)
    {
        rses_property_t *p = router_cli_ses->rses_properties[i];
        rses_property_t *q = p;

        while (p != NULL)
        {
            q = p->rses_prop_next;
            rses_property_done(p);
            p = q;
        }
    }

    MXS_FREE(router_cli_ses->rses_backend_ref);
    MXS_FREE(router_cli_ses);
    return;
}

/**
 * @brief Mark a backend reference as failed
 *
 * @param bref Backend reference to close
 * @param fatal Whether the failure was fatal
 */
void close_failed_bref(backend_ref_t *bref, bool fatal)
{
    if (BREF_IS_WAITING_RESULT(bref))
    {
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }

    bref_clear_state(bref, BREF_QUERY_ACTIVE);
    bref_clear_state(bref, BREF_IN_USE);
    bref_set_state(bref, BREF_CLOSED);

    if (fatal)
    {
        bref_set_state(bref, BREF_FATAL_FAILURE);
    }

    if (sescmd_cursor_is_active(&bref->bref_sescmd_cur))
    {
        sescmd_cursor_set_active(&bref->bref_sescmd_cur, false);
    }

    if (bref->bref_pending_cmd)
    {
        gwbuf_free(bref->bref_pending_cmd);
        bref->bref_pending_cmd = NULL;
    }
}

/**
 * @brief The main routing entry point for a query (API)
 *
 * The routeQuery function will make the routing decision based on the contents
 * of the instance, session and the query itself. The query always represents
 * a complete MariaDB/MySQL packet because we define the RCAP_TYPE_STMT_INPUT in
 * getCapabilities().
 *
 * @param instance       Router instance
 * @param router_session Router session associated with the client
 * @param querybuf       Buffer containing the query
 * @return 1 on success, 0 on error
 */
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *querybuf)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *rses = (ROUTER_CLIENT_SES *) router_session;
    int rval = 0;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        closed_session_reply(querybuf);
    }
    else
    {
        live_session_reply(&querybuf, rses);
        if (route_single_stmt(inst, rses, querybuf))
        {
            rval = 1;
        }
    }

    if (querybuf != NULL)
    {
        gwbuf_free(querybuf);
    }

    return rval;
}

/**
 * @brief Diagnostics routine (API)
 *
 * Print query router statistics to the DCB passed in
 *
 * @param   instance    The router instance
 * @param   dcb     The DCB for diagnostic output
 */
static void diagnostics(MXS_ROUTER *instance, DCB *dcb)
{
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE *)instance;
    char *weightby;
    double master_pct = 0.0, slave_pct = 0.0, all_pct = 0.0;

    dcb_printf(dcb, "\n");
    dcb_printf(dcb, "\tuse_sql_variables_in:      %s\n",
               mxs_target_to_str(router->rwsplit_config.use_sql_variables_in));
    dcb_printf(dcb, "\tslave_selection_criteria:  %s\n",
               select_criteria_to_str(router->rwsplit_config.slave_selection_criteria));
    dcb_printf(dcb, "\tmaster_failure_mode:       %s\n",
               failure_mode_to_str(router->rwsplit_config.master_failure_mode));
    dcb_printf(dcb, "\tmax_slave_replication_lag: %d\n",
               router->rwsplit_config.max_slave_replication_lag);
    dcb_printf(dcb, "\tretry_failed_reads:        %s\n",
               router->rwsplit_config.retry_failed_reads ? "true" : "false");
    dcb_printf(dcb, "\tstrict_multi_stmt:         %s\n",
               router->rwsplit_config.strict_multi_stmt ? "true" : "false");
    dcb_printf(dcb, "\tdisable_sescmd_history:    %s\n",
               router->rwsplit_config.disable_sescmd_history ? "true" : "false");
    dcb_printf(dcb, "\tmax_sescmd_history:        %d\n",
               router->rwsplit_config.max_sescmd_history);
    dcb_printf(dcb, "\tmaster_accept_reads:       %s\n",
               router->rwsplit_config.master_accept_reads ? "true" : "false");
    dcb_printf(dcb, "\n");

    if (router->stats.n_queries > 0)
    {
        master_pct = ((double)router->stats.n_master / (double)router->stats.n_queries) * 100.0;
        slave_pct = ((double)router->stats.n_slave / (double)router->stats.n_queries) * 100.0;
        all_pct = ((double)router->stats.n_all / (double)router->stats.n_queries) * 100.0;
    }

    dcb_printf(dcb, "\tNumber of router sessions:           	%" PRIu64 "\n",
               router->stats.n_sessions);
    dcb_printf(dcb, "\tCurrent no. of router sessions:      	%d\n",
               router->service->stats.n_current);
    dcb_printf(dcb, "\tNumber of queries forwarded:          	%" PRIu64 "\n",
               router->stats.n_queries);
    dcb_printf(dcb, "\tNumber of queries forwarded to master:	%" PRIu64 " (%.2f%%)\n",
               router->stats.n_master, master_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to slave: 	%" PRIu64 " (%.2f%%)\n",
               router->stats.n_slave, slave_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to all:   	%" PRIu64 " (%.2f%%)\n",
               router->stats.n_all, all_pct);

    if ((weightby = serviceGetWeightingParameter(router->service)) != NULL)
    {
        dcb_printf(dcb, "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb, "\t\tServer               Target %%    Connections  "
                   "Operations\n");
        dcb_printf(dcb, "\t\t                               Global  Router\n");
        for (SERVER_REF *ref = router->service->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb, "\t\t%-20s %3.1f%%     %-6d  %-6d  %d\n",
                       ref->server->unique_name, (float)ref->weight / 10,
                       ref->server->stats.n_current, ref->connections,
                       ref->server->stats.n_current_ops);
        }
    }
}

/**
 * @brief Client Reply routine (API)
 *
 * The routine will reply to client for session change with master server data
 *
 * @param   instance    The router instance
 * @param   router_session  The router session
 * @param   backend_dcb The backend DCB
 * @param   queue       The GWBUF with reply data
 */
static void clientReply(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *writebuf,
                        DCB *backend_dcb)
{
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
    ROUTER_INSTANCE *router_inst = (ROUTER_INSTANCE *)instance;
    DCB *client_dcb = backend_dcb->session->client_dcb;

    CHK_CLIENT_RSES(router_cli_ses);

    /**
     * Lock router client session for secure read of router session members.
     * Note that this could be done without lock by using version #
     */
    if (router_cli_ses->rses_closed)
    {
        gwbuf_free(writebuf);
        return;
    }

    /**
     * 1. Check if backend received reply to sescmd.
     * 2. Check sescmd's state whether OK_PACKET has been
     *    sent to client already and if not, lock property cursor,
     *    reply to client, and move property cursor forward. Finally
     *    release the lock.
     * 3. If reply for this sescmd is sent, lock property cursor
     *    and
     */

    backend_ref_t *bref = get_bref_from_dcb(router_cli_ses, backend_dcb);
    CHK_BACKEND_REF(bref);
    sescmd_cursor_t *scur = &bref->bref_sescmd_cur;

    /** Statement was successfully executed, free the stored statement */
    session_clear_stmt(backend_dcb->session);

    /**
     * Active cursor means that reply is from session command
     * execution.
     */
    if (sescmd_cursor_is_active(scur))
    {
        check_session_command_reply(writebuf, scur, bref);

        if (GWBUF_IS_TYPE_SESCMD_RESPONSE(writebuf))
        {
            /**
             * Discard all those responses that have already been sent to
             * the client. Return with buffer including response that
             * needs to be sent to client or NULL.
             */
            bool rconn = false;
            writebuf = sescmd_cursor_process_replies(writebuf, bref, &rconn);

            if (rconn && !router_inst->rwsplit_config.disable_sescmd_history)
            {
                select_connect_backend_servers(
                    &router_cli_ses->rses_master_ref, router_cli_ses->rses_backend_ref,
                    router_cli_ses->rses_nbackends,
                    router_cli_ses->rses_config.max_slave_connections,
                    router_cli_ses->rses_config.max_slave_replication_lag,
                    router_cli_ses->rses_config.slave_selection_criteria,
                    router_cli_ses->rses_master_ref->bref_dcb->session,
                    router_cli_ses->router,
                    true);
            }
        }
        /**
         * If response will be sent to client, decrease waiter count.
         * This applies to session commands only. Counter decrement
         * for other type of queries is done outside this block.
         */

        /** Set response status as replied */
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }
    /**
     * Clear BREF_QUERY_ACTIVE flag and decrease waiter counter.
     * This applies for queries  other than session commands.
     */
    else if (BREF_IS_QUERY_ACTIVE(bref))
    {
        bref_clear_state(bref, BREF_QUERY_ACTIVE);
        /** Set response status as replied */
        bref_clear_state(bref, BREF_WAITING_RESULT);
    }

    if (writebuf != NULL && client_dcb != NULL)
    {
        /** Write reply to client DCB */
        MXS_SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    }

    /** There is one pending session command to be executed. */
    if (sescmd_cursor_is_active(scur))
    {
        bool succp;

        MXS_INFO("Backend [%s]:%d processed reply and starts to execute active cursor.",
                 bref->ref->server->name, bref->ref->server->port);

        succp = execute_sescmd_in_backend(bref);

        if (!succp)
        {
            MXS_INFO("Backend [%s]:%d failed to execute session command.",
                     bref->ref->server->name, bref->ref->server->port);
        }
    }
    else if (bref->bref_pending_cmd != NULL) /*< non-sescmd is waiting to be routed */
    {
        int ret;

        CHK_GWBUF(bref->bref_pending_cmd);

        if ((ret = bref->bref_dcb->func.write(bref->bref_dcb,
                                              gwbuf_clone(bref->bref_pending_cmd))) == 1)
        {
            ROUTER_INSTANCE* inst = (ROUTER_INSTANCE *)instance;
            atomic_add_uint64(&inst->stats.n_queries, 1);
            /**
             * Add one query response waiter to backend reference
             */
            bref_set_state(bref, BREF_QUERY_ACTIVE);
            bref_set_state(bref, BREF_WAITING_RESULT);
        }
        else
        {
            char* sql = modutil_get_SQL(bref->bref_pending_cmd);

            if (sql)
            {
                MXS_ERROR("Routing query \"%s\" failed.", sql);
                MXS_FREE(sql);
            }
            else
            {
                MXS_ERROR("Failed to route query.");
            }
        }
        gwbuf_free(bref->bref_pending_cmd);
        bref->bref_pending_cmd = NULL;
    }
}


/**
 * @brief Get router capabilities (API)
 *
 * Return a bit map indicating the characteristics of this particular router.
 * In this case, the only bit set indicates that the router wants to receive
 * data for routing as whole SQL statements.
 *
 * @return RCAP_TYPE_STMT_INPUT.
 */
static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING;
}

/*
 * This is the end of the API functions, and the start of functions that are
 * used by the API functions and also used in other modules of the router
 * code. Their prototypes are included in rwsplit_internal.h since these
 * functions are not intended for use outside the read write split router.
 */

/*
 * @brief Clear one or more bits in the backend reference state
 *
 * The router session holds details of the backend servers that are
 * involved in the routing for this particular service. Each backend
 * server has a state bit string, and this function (along with
 * bref_set_state) is used to manage the state.
 *
 * @param bref The backend reference to be modified
 * @param state A bit string where the 1 bits indicate bits that should
 * be turned off in the bref state.
 */
void bref_clear_state(backend_ref_t *bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }

    if ((state & BREF_WAITING_RESULT) && (bref->bref_state & BREF_WAITING_RESULT))
    {
        int prev1;
        int prev2;

        /** Decrease waiter count */
        prev1 = atomic_add(&bref->bref_num_result_wait, -1);

        if (prev1 <= 0)
        {
            atomic_add(&bref->bref_num_result_wait, 1);
        }
        else
        {
            /** Decrease global operation count */
            prev2 = atomic_add(&bref->ref->server->stats.n_current_ops, -1);
            ss_dassert(prev2 > 0);
            if (prev2 <= 0)
            {
                MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                          __FUNCTION__, bref->ref->server->name,
                          bref->ref->server->port);
            }
        }
    }

    bref->bref_state &= ~state;
}

/*
 * @brief Set one or more bits in the backend reference state
 *
 * The router session holds details of the backend servers that are
 * involved in the routing for this particular service. Each backend
 * server has a state bit string, and this function (along with
 * bref_clear_state) is used to manage the state.
 *
 * @param bref The backend reference to be modified
 * @param state A bit string where the 1 bits indicate bits that should
 * be turned on in the bref state.
 */
void bref_set_state(backend_ref_t *bref, bref_state_t state)
{
    if (bref == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }

    if ((state & BREF_WAITING_RESULT) && (bref->bref_state & BREF_WAITING_RESULT) == 0)
    {
        int prev1;
        int prev2;

        /** Increase waiter count */
        prev1 = atomic_add(&bref->bref_num_result_wait, 1);
        ss_dassert(prev1 >= 0);
        if (prev1 < 0)
        {
            MXS_ERROR("[%s] Error: negative number of connections waiting for "
                      "results in backend %s:%u", __FUNCTION__,
                      bref->ref->server->name, bref->ref->server->port);
        }
        /** Increase global operation count */
        prev2 = atomic_add(&bref->ref->server->stats.n_current_ops, 1);
        ss_dassert(prev2 >= 0);
        if (prev2 < 0)
        {
            MXS_ERROR("[%s] Error: negative current operation count in backend %s:%u",
                      __FUNCTION__, bref->ref->server->name, bref->ref->server->port);
        }
    }

    bref->bref_state |= state;
}

/**
 * @brief Free resources belonging to a property
 *
 * Property is freed at the end of router client session.
 *
 * @param prop The property whose resources are to be released
 */
void rses_property_done(rses_property_t *prop)
{
    if (prop == NULL)
    {
        MXS_ERROR("[%s] Error: NULL parameter.", __FUNCTION__);
        return;
    }
    CHK_RSES_PROP(prop);

    switch (prop->rses_prop_type)
    {
    case RSES_PROP_TYPE_SESCMD:
        mysql_sescmd_done(&prop->rses_prop_data.sescmd);
        break;

    case RSES_PROP_TYPE_TMPTABLES:
        hashtable_free(prop->rses_prop_data.temp_tables);
        break;

    default:
        MXS_DEBUG("%lu [rses_property_done] Unknown property type %d "
                  "in property %p", pthread_self(), prop->rses_prop_type, prop);

        ss_dassert(false);
        break;
    }
    MXS_FREE(prop);
}

/**
 * @brief Get count of backend servers that are slaves.
 *
 * Find out the number of read backend servers.
 * Depending on the configuration value type, either copy direct count
 * of slave connections or calculate the count from percentage value.
 *
 * @param   rses Router client session
 * @param   router_nservers The number of backend servers in total
 */
int rses_get_max_slavecount(ROUTER_CLIENT_SES *rses,
                            int router_nservers)
{
    int conf_max_nslaves;
    int max_nslaves;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_config.max_slave_connections > 0)
    {
        conf_max_nslaves = rses->rses_config.max_slave_connections;
    }
    else
    {
        conf_max_nslaves = (router_nservers * rses->rses_config.rw_max_slave_conn_percent) / 100;
    }
    max_nslaves = MXS_MIN(router_nservers - 1, MXS_MAX(1, conf_max_nslaves));

    return max_nslaves;
}

/*
 * @brief Get the maximum replication lag for this router
 *
 * @param   rses    Router client session
 * @return  Replication lag from configuration or very large number
 */
int rses_get_max_replication_lag(ROUTER_CLIENT_SES *rses)
{
    int conf_max_rlag;

    CHK_CLIENT_RSES(rses);

    /** if there is no configured value, then longest possible int is used */
    if (rses->rses_config.max_slave_replication_lag > 0)
    {
        conf_max_rlag = rses->rses_config.max_slave_replication_lag;
    }
    else
    {
        conf_max_rlag = ~(1 << 31);
    }

    return conf_max_rlag;
}

/**
 * @brief Find a back end reference that matches the given DCB
 *
 * Finds out if there is a backend reference pointing at the DCB given as
 * parameter.
 *
 * @param rses  router client session
 * @param dcb   DCB
 *
 * @return backend reference pointer if succeed or NULL
 */
backend_ref_t *get_bref_from_dcb(ROUTER_CLIENT_SES *rses, DCB *dcb)
{
    ss_dassert(dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    CHK_DCB(dcb);
    CHK_CLIENT_RSES(rses);

    for (int i = 0; i < rses->rses_nbackends; i++)
    {
        if (rses->rses_backend_ref[i].bref_dcb == dcb)
        {
            return &rses->rses_backend_ref[i];
        }
    }

    /** We should always have a valid backend reference */
    ss_dassert(false);
    return NULL;
}

/**
 * @brief Call hang up function
 *
 * Calls hang-up function for DCB if it is not both running and in
 * master/slave/joined/ndb role. Called by DCB's callback routine.
 *
 * @param dcb       DCB relating to a backend server
 * @param reason    The reason for the state change
 * @param data      Data is a backend reference structure belonging to this router
 *
 * @return  1 for success, 0 for failure
 */
int router_handle_state_switch(DCB *dcb, DCB_REASON reason, void *data)
{
    backend_ref_t *bref;
    int rc = 1;
    SERVER *srv;
    CHK_DCB(dcb);

    if (NULL == dcb->session->router_session)
    {
        /*
         * The following processing will fail if there is no router session,
         * because the "data" parameter will not contain meaningful data,
         * so we have no choice but to stop here.
         */
        return 0;
    }
    bref = (backend_ref_t *)data;
    CHK_BACKEND_REF(bref);

    srv = bref->ref->server;

    if (SERVER_IS_RUNNING(srv) && SERVER_IS_IN_CLUSTER(srv))
    {
        goto return_rc;
    }

    MXS_DEBUG("%lu [router_handle_state_switch] %s [%s]:%d in state %s",
              pthread_self(), STRDCBREASON(reason), srv->name, srv->port,
              STRSRVSTATUS(srv));
    CHK_SESSION(((MXS_SESSION *)dcb->session));
    if (dcb->session->router_session)
    {
        CHK_CLIENT_RSES(((ROUTER_CLIENT_SES *)dcb->session->router_session));
    }

    switch (reason)
    {
    case DCB_REASON_NOT_RESPONDING:
        dcb->func.hangup(dcb);
        break;

    default:
        break;
    }

return_rc:
    return rc;
}

/*
 * The end of the functions used here and elsewhere in the router; start of
 * functions that are purely internal to this module, i.e. are called directly
 * or indirectly by the API functions and not used elsewhere.
 */

/**
 * @brief Process router options
 *
 * @param router Router instance
 * @param options Router options
 * @return True on success, false if a configuration error was found
 */
static bool rwsplit_process_router_options(ROUTER_INSTANCE *router,
                                           char **options)
{
    int i;
    char *value;
    select_criteria_t c;

    if (options == NULL)
    {
        return true;
    }

    MXS_WARNING("Router options for readwritesplit are deprecated.");

    bool success = true;

    for (i = 0; options[i]; i++)
    {
        if ((value = strchr(options[i], '=')) == NULL)
        {
            MXS_ERROR("Unsupported router option \"%s\" for readwritesplit router.", options[i]);
            success = false;
        }
        else
        {
            *value = 0;
            value++;
            if (strcmp(options[i], "slave_selection_criteria") == 0)
            {
                c = GET_SELECT_CRITERIA(value);
                ss_dassert(c == LEAST_GLOBAL_CONNECTIONS ||
                           c == LEAST_ROUTER_CONNECTIONS || c == LEAST_BEHIND_MASTER ||
                           c == LEAST_CURRENT_OPERATIONS || c == UNDEFINED_CRITERIA);

                if (c == UNDEFINED_CRITERIA)
                {
                    MXS_ERROR("Unknown slave selection criteria \"%s\". "
                              "Allowed values are LEAST_GLOBAL_CONNECTIONS, "
                              "LEAST_ROUTER_CONNECTIONS, LEAST_BEHIND_MASTER,"
                              "and LEAST_CURRENT_OPERATIONS.",
                              STRCRITERIA(router->rwsplit_config.slave_selection_criteria));
                    success = false;
                }
                else
                {
                    router->rwsplit_config.slave_selection_criteria = c;
                }
            }
            else if (strcmp(options[i], "max_sescmd_history") == 0)
            {
                router->rwsplit_config.max_sescmd_history = atoi(value);
            }
            else if (strcmp(options[i], "disable_sescmd_history") == 0)
            {
                router->rwsplit_config.disable_sescmd_history = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_accept_reads") == 0)
            {
                router->rwsplit_config.master_accept_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "strict_multi_stmt") == 0)
            {
                router->rwsplit_config.strict_multi_stmt = config_truth_value(value);
            }
            else if (strcmp(options[i], "retry_failed_reads") == 0)
            {
                router->rwsplit_config.retry_failed_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_failure_mode") == 0)
            {
                if (strcasecmp(value, "fail_instantly") == 0)
                {
                    router->rwsplit_config.master_failure_mode = RW_FAIL_INSTANTLY;
                }
                else if (strcasecmp(value, "fail_on_write") == 0)
                {
                    router->rwsplit_config.master_failure_mode = RW_FAIL_ON_WRITE;
                }
                else if (strcasecmp(value, "error_on_write") == 0)
                {
                    router->rwsplit_config.master_failure_mode = RW_ERROR_ON_WRITE;
                }
                else
                {
                    MXS_ERROR("Unknown value for 'master_failure_mode': %s", value);
                    success = false;
                }
            }
            else
            {
                MXS_ERROR("Unknown router option \"%s=%s\" for readwritesplit router.",
                          options[i], value);
                success = false;
            }
        }
    } /*< for */

    return success;
}

/**
 * @brief Router error handling routine (API)
 *
 * Error Handler routine to resolve _backend_ failures. If it succeeds then
 * there are enough operative backends available and connected. Otherwise it
 * fails, and session is terminated.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       errmsgbuf       The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action      The action: ERRACT_NEW_CONNECTION or
 * ERRACT_REPLY_CLIENT
 * @param   succp       Result of action: true iff router can continue
 *
 * Even if succp == true connecting to new slave may have failed. succp is to
 * tell whether router has enough master/slave connections to continue work.
 */
static void handleError(MXS_ROUTER *instance,
                        MXS_ROUTER_SESSION *router_session,
                        GWBUF *errmsgbuf,
                        DCB *problem_dcb,
                        mxs_error_action_t action,
                        bool *succp)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *)instance;
    ROUTER_CLIENT_SES *rses = (ROUTER_CLIENT_SES *)router_session;
    CHK_CLIENT_RSES(rses);
    CHK_DCB(problem_dcb);

    if (rses->rses_closed)
    {
        /** Session is already closed */
        problem_dcb->dcb_errhandle_called = true;
        *succp = false;
        return;
    }

    /** Don't handle same error twice on same DCB */
    if (problem_dcb->dcb_errhandle_called)
    {
        /** we optimistically assume that previous call succeed */
        /*
         * The return of true is potentially misleading, but appears to
         * be safe with the code as it stands on 9 Sept 2015 - MNB
         */
        *succp = true;
        return;
    }
    else
    {
        problem_dcb->dcb_errhandle_called = true;
    }

    MXS_SESSION *session = problem_dcb->session;
    ss_dassert(session);

    if (problem_dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        dcb_close(problem_dcb);
        *succp = false;
    }
    else
    {
        backend_ref_t *bref = get_bref_from_dcb(rses, problem_dcb);

        switch (action)
        {
        case ERRACT_NEW_CONNECTION:
            {
                /**
                 * If master has lost its Master status error can't be
                 * handled so that session could continue.
                 */
                if (rses->rses_master_ref && rses->rses_master_ref->bref_dcb == problem_dcb)
                {
                    SERVER *srv = rses->rses_master_ref->ref->server;
                    bool can_continue = false;

                    if (rses->rses_config.master_failure_mode != RW_FAIL_INSTANTLY &&
                        (bref == NULL || !BREF_IS_WAITING_RESULT(bref)))
                    {
                        /** The failure of a master is not considered a critical
                         * failure as partial functionality still remains. Reads
                         * are allowed as long as slave servers are available
                         * and writes will cause an error to be returned.
                         *
                         * If we were waiting for a response from the master, we
                         * can't be sure whether it was executed or not. In this
                         * case the safest thing to do is to close the client
                         * connection. */
                        can_continue = true;
                    }
                    else if (!SERVER_IS_MASTER(srv) && !srv->master_err_is_logged)
                    {
                        MXS_ERROR("Server [%s]:%d lost the master status. Readwritesplit "
                                  "service can't locate the master. Client sessions "
                                  "will be closed.", srv->name, srv->port);
                        srv->master_err_is_logged = true;
                    }

                    *succp = can_continue;

                    if (bref != NULL)
                    {
                        CHK_BACKEND_REF(bref);
                        RW_CHK_DCB(bref, problem_dcb);
                        dcb_close(problem_dcb);
                        RW_CLOSE_BREF(bref);
                        close_failed_bref(bref, true);
                    }
                    else
                    {
                        MXS_ERROR("Server [%s]:%d lost the master status but could not locate the "
                                  "corresponding backend ref.", srv->name, srv->port);
                    }
                }
                else if (bref)
                {
                    /** Check whether problem_dcb is same as dcb of rses->forced_node
                     * and within READ ONLY transaction:
                     * if true reset rses->forced_node and close session
                     */
                    if (rses->forced_node &&
                        (rses->forced_node->bref_dcb == problem_dcb &&
                         session_trx_is_read_only(problem_dcb->session)))
                    {
                        MXS_ERROR("forced_node SLAVE %s in opened READ ONLY transaction has failed:"
                                  " closing session",
                                  problem_dcb->server->unique_name);

                        rses->forced_node = NULL;
                        *succp = false;
                        break;
                    }

                    /** We should reconnect only if we find a backend for this
                     * DCB. If this DCB is an older DCB that has been closed,
                     * we can ignore it. */
                    *succp = handle_error_new_connection(inst, &rses, problem_dcb, errmsgbuf);
                }

                if (bref)
                {
                    /** This is a valid DCB for a backend ref */
                    if (BREF_IS_IN_USE(bref) && bref->bref_dcb == problem_dcb)
                    {
                        ss_dassert(false);
                        MXS_ERROR("Backend '%s' is still in use and points to the problem DCB.",
                                  bref->ref->server->unique_name);
                    }
                }
                else
                {
                    const char *remote = problem_dcb->state == DCB_STATE_POLLING &&
                                         problem_dcb->server ? problem_dcb->server->unique_name : "CLOSED";

                    MXS_ERROR("DCB connected to '%s' is not in use by the router "
                              "session, not closing it. DCB is in state '%s'",
                              remote, STRDCBSTATE(problem_dcb->state));
                }
                break;
            }

        case ERRACT_REPLY_CLIENT:
            {
                handle_error_reply_client(session, rses, problem_dcb, errmsgbuf);
                *succp = false; /*< no new backend servers were made available */
                break;
            }

        default:
            ss_dassert(!true);
            *succp = false;
            break;
        }
    }
}

/**
 * @brief Handle an error reply for a client
 *
 * @param ses           Session
 * @param rses          Router session
 * @param backend_dcb   DCB for the backend server that has failed
 * @param errmsg        GWBUF containing the error message
 */
static void handle_error_reply_client(MXS_SESSION *ses, ROUTER_CLIENT_SES *rses,
                                      DCB *backend_dcb, GWBUF *errmsg)
{
    mxs_session_state_t sesstate;
    DCB *client_dcb;
    backend_ref_t *bref;

    sesstate = ses->state;
    client_dcb = ses->client_dcb;

    if ((bref = get_bref_from_dcb(rses, backend_dcb)) != NULL)
    {
        CHK_BACKEND_REF(bref);

        if (BREF_IS_IN_USE(bref))
        {
            close_failed_bref(bref, false);
            RW_CHK_DCB(bref, backend_dcb);
            dcb_close(backend_dcb);
            RW_CLOSE_BREF(bref);
        }
    }
    else
    {
        // All dcbs should be associated with a backend reference.
        ss_dassert(!true);
    }

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        CHK_DCB(client_dcb);
        client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
    }
}

static bool reroute_stored_statement(ROUTER_CLIENT_SES *rses, backend_ref_t *old, GWBUF *stored)
{
    bool success = false;

    if (!session_trx_is_active(rses->client_dcb->session))
    {
        /**
         * Only try to retry the read if autocommit is enabled and we are
         * outside of a transaction
         */
        for (int i = 0; i < rses->rses_nbackends; i++)
        {
            backend_ref_t *bref = &rses->rses_backend_ref[i];

            if (BREF_IS_IN_USE(bref) && bref != old &&
                !SERVER_IS_MASTER(bref->ref->server) &&
                SERVER_IS_SLAVE(bref->ref->server))
            {
                /** Found a valid candidate; a non-master slave that's in use */
                if (bref->bref_dcb->func.write(bref->bref_dcb, stored))
                {
                    MXS_INFO("Retrying failed read at '%s'.", bref->ref->server->unique_name);
                    success = true;
                    break;
                }
            }
        }

        if (!success && rses->rses_master_ref && BREF_IS_IN_USE(rses->rses_master_ref))
        {
            /**
             * Either we failed to write to the slave or no valid slave was found.
             * Try to retry the read on the master.
             */
            backend_ref_t *bref = rses->rses_master_ref;

            if (bref->bref_dcb->func.write(bref->bref_dcb, stored))
            {
                MXS_INFO("Retrying failed read at '%s'.", bref->ref->server->unique_name);
                success = true;
            }
        }
    }

    return success;
}

/**
 * Check if there is backend reference pointing at failed DCB, and reset its
 * flags. Then clear DCB's callback and finally : try to find replacement(s)
 * for failed slave(s).
 *
 * This must be called with router lock.
 *
 * @param inst      router instance
 * @param rses      router client session
 * @param dcb       failed DCB
 * @param errmsg    error message which is sent to client if it is waiting
 *
 * @return true if there are enough backend connections to continue, false if
 * not
 */
static bool handle_error_new_connection(ROUTER_INSTANCE *inst,
                                        ROUTER_CLIENT_SES **rses,
                                        DCB *backend_dcb, GWBUF *errmsg)
{
    ROUTER_CLIENT_SES *myrses;
    MXS_SESSION *ses;
    int max_nslaves;
    int max_slave_rlag;
    backend_ref_t *bref;
    bool succp;

    myrses = *rses;

    ses = backend_dcb->session;
    CHK_SESSION(ses);

    /**
     * If bref == NULL it has been replaced already with another one.
     *
     * NOTE: This can never happen.
     */
    if ((bref = get_bref_from_dcb(myrses, backend_dcb)) == NULL)
    {
        return true;
    }
    CHK_BACKEND_REF(bref);

    /**
     * If query was sent through the bref and it is waiting for reply from
     * the backend server it is necessary to send an error to the client
     * because it is waiting for reply.
     */
    if (BREF_IS_WAITING_RESULT(bref))
    {
        GWBUF *stored = NULL;
        const SERVER *target = NULL;

        if (!session_take_stmt(backend_dcb->session, &stored, &target) ||
            target != bref->ref->server ||
            !reroute_stored_statement(*rses, bref, stored))
        {
            /**
             * We failed to route the stored statement or no statement was
             * stored for this server. Either way we can safely free the buffer.
             */
            gwbuf_free(stored);

            if (!sescmd_cursor_is_active(&bref->bref_sescmd_cur))
            {
                /** The client expects a response from this exact backend.
                 * We need to route an error to the client to let it know
                 * that the query failed. */
                DCB *client_dcb = ses->client_dcb;
                client_dcb->func.write(client_dcb, gwbuf_clone(errmsg));
            }
        }
    }

    RW_CHK_DCB(bref, backend_dcb);
    dcb_close(backend_dcb);
    RW_CLOSE_BREF(bref);
    close_failed_bref(bref, false);
    max_nslaves = rses_get_max_slavecount(myrses, myrses->rses_nbackends);
    max_slave_rlag = rses_get_max_replication_lag(myrses);
    /**
     * Try to get replacement slave or at least the minimum
     * number of slave connections for router session.
     */
    if (inst->rwsplit_config.disable_sescmd_history)
    {
        succp = have_enough_servers(myrses, 1, myrses->rses_nbackends, inst) ? true : false;
    }
    else
    {
        succp = select_connect_backend_servers(&myrses->rses_master_ref,
                                               myrses->rses_backend_ref,
                                               myrses->rses_nbackends,
                                               max_nslaves, max_slave_rlag,
                                               myrses->rses_config.slave_selection_criteria,
                                               ses, inst, true);
    }

    return succp;
}

/**
 * @brief Calculate whether we have enough servers to route a query
 *
 * @param p_rses        Router session
 * @param min_nsrv      Minimum number of servers that is sufficient
 * @param nsrv          Actual number of servers
 * @param router        Router instance
 *
 * @return bool - whether enough, side effect is error logging
 */
static bool have_enough_servers(ROUTER_CLIENT_SES *rses, const int min_nsrv,
                                int router_nsrv, ROUTER_INSTANCE *router)
{
    bool succp;

    /** With too few servers session is not created */
    if (router_nsrv < min_nsrv ||
        MXS_MAX((rses)->rses_config.max_slave_connections,
                (router_nsrv * (rses)->rses_config.rw_max_slave_conn_percent) /
                100) < min_nsrv)
    {
        if (router_nsrv < min_nsrv)
        {
            MXS_ERROR("Unable to start %s service. There are "
                      "too few backend servers available. Found %d "
                      "when %d is required.",
                      router->service->name, router_nsrv, min_nsrv);
        }
        else
        {
            int pct = (rses)->rses_config.rw_max_slave_conn_percent / 100;
            int nservers = router_nsrv * pct;

            if ((rses)->rses_config.max_slave_connections < min_nsrv)
            {
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d when %d is required.",
                          router->service->name,
                          (rses)->rses_config.max_slave_connections, min_nsrv);
            }
            if (nservers < min_nsrv)
            {
                double dbgpct = ((double)min_nsrv / (double)router_nsrv) * 100.0;
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d%% when at least %.0f%% "
                          "would be required.",
                          router->service->name,
                          (rses)->rses_config.rw_max_slave_conn_percent, dbgpct);
            }
        }
        succp = false;
    }
    else
    {
        succp = true;
    }
    return succp;
}

/*
 * @brief   Release resources when createInstance fails to complete
 *
 * Internal to createInstance
 *
 * @param   router  Router instance
 *
 */
static void free_rwsplit_instance(ROUTER_INSTANCE *router)
{
    if (router)
    {
        MXS_FREE(router);
    }
}

/**
 * @brief Create backend server references
 *
 * This creates a new set of backend references for the client session. Currently
 * this is only used on startup but it could be used to dynamically change the
 * set of used servers.
 *
 * @param rses Client router session
 * @param dest Destination where the array of backens is stored
 * @param n_backend Number of items in the array
 * @return True on success, false on error
 */
static bool create_backends(ROUTER_CLIENT_SES *rses, backend_ref_t** dest, int* n_backend)
{
    backend_ref_t *backend_ref = (backend_ref_t *)MXS_CALLOC(1, *n_backend * sizeof(backend_ref_t));

    if (backend_ref == NULL)
    {
        return false;
    }

    int i = 0;

    for (SERVER_REF *sref = rses->router->service->dbref; sref && i < *n_backend; sref = sref->next)
    {
        if (sref->active)
        {
#if defined(SS_DEBUG)
            backend_ref[i].bref_chk_top = CHK_NUM_BACKEND_REF;
            backend_ref[i].bref_chk_tail = CHK_NUM_BACKEND_REF;
            backend_ref[i].bref_sescmd_cur.scmd_cur_chk_top = CHK_NUM_SESCMD_CUR;
            backend_ref[i].bref_sescmd_cur.scmd_cur_chk_tail = CHK_NUM_SESCMD_CUR;
#endif
            backend_ref[i].bref_state = 0;
            backend_ref[i].ref = sref;
            /** store pointers to sescmd list to both cursors */
            backend_ref[i].bref_sescmd_cur.scmd_cur_rses = rses;
            backend_ref[i].bref_sescmd_cur.scmd_cur_active = false;
            backend_ref[i].bref_sescmd_cur.scmd_cur_ptr_property =
                &rses->rses_properties[RSES_PROP_TYPE_SESCMD];
            backend_ref[i].bref_sescmd_cur.scmd_cur_cmd = NULL;
            i++;
        }
    }

    if (i < *n_backend)
    {
        MXS_INFO("The service reported %d servers but only took %d into use.", *n_backend, i);
        *n_backend = i;
    }

    *dest = backend_ref;
    return true;
}
