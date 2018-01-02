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

/**
 * @file readconnroute.c - Read Connection Load Balancing Query Router
 *
 * This is the implementation of a simple query router that balances
 * read connections. It assumes the service is configured with a set
 * of slaves and that the application clients already split read and write
 * queries. It offers a service to balance the client read connections
 * over this set of slave servers. It does this once only, at the time
 * the connection is made. It chooses the server that currently has the least
 * number of connections by keeping a count for each server of how
 * many connections the query router has made to the server.
 *
 * When two servers have the same number of current connections the one with
 * the least number of connections since startup will be used.
 *
 * The router may also have options associated to it that will limit the
 * choice of backend server. Currently two options are supported, the "master"
 * option will cause the router to only connect to servers marked as masters
 * and the "slave" option will limit connections to routers that are marked
 * as slaves. If neither option is specified the router will connect to either
 * masters or slaves.
 *
 * @verbatim
 * Revision History
 *
 * Date     Who     Description
 * 14/06/2013   Mark Riddoch        Initial implementation
 * 25/06/2013   Mark Riddoch        Addition of checks for current server state
 * 26/06/2013   Mark Riddoch        Use server with least connections since
 *                  startup if the number of current
 *                  connections is the same for two servers
 *                  Addition of master and slave options
 * 27/06/2013   Vilho Raatikka      Added skygw_log_write command as an example
 *                  and necessary headers.
 * 17/07/2013   Massimiliano Pinto  Added clientReply routine:
 *                  called by backend server to send data to client
 *                  Included maxscale/protocol/mysql.h
 *                  with macros and MySQL commands with MYSQL_ prefix
 *                  avoiding any conflict with the standard ones
 *                  in mysql.h
 * 22/07/2013   Mark Riddoch        Addition of joined router option for Galera
 *                  clusters
 * 31/07/2013   Massimiliano Pinto  Added a check for candidate server, if NULL return
 * 12/08/2013   Mark Riddoch        Log unsupported router options
 * 04/09/2013   Massimiliano Pinto  Added client NULL check in clientReply
 * 22/10/2013   Massimiliano Pinto  errorReply called from backend, for client error reply
 *                  or take different actions such as open a new backend connection
 * 20/02/2014   Massimiliano Pinto  If router_options=slave, route traffic to master if no slaves available
 * 06/03/2014   Massimiliano Pinto  Server connection counter is now updated in closeSession
 * 24/06/2014   Massimiliano Pinto  New rules for selecting the Master server
 * 27/06/2014   Mark Riddoch        Addition of server weighting
 * 11/06/2015   Martin Brampton         Remove decrement n_current (moved to dcb.c)
 * 09/09/2015   Martin Brampton         Modify error handler
 * 25/09/2015   Martin Brampton         Block callback processing when no router session in the DCB
 * 09/11/2015   Martin Brampton         Modified routeQuery - must free "queue" regardless of outcome
 *
 * @endverbatim
 */

#include "readconnection.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <maxscale/alloc.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/atomic.h>
#include <maxscale/spinlock.h>
#include <maxscale/dcb.h>
#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/modutil.h>

/* The router entry points */
static MXS_ROUTER *createInstance(SERVICE *service, char **options);
static MXS_ROUTER_SESSION *newSession(MXS_ROUTER *instance, MXS_SESSION *session);
static void closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);
static void freeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session);
static int routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue);
static void diagnostics(MXS_ROUTER *instance, DCB *dcb);
static void clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue,
                        DCB *backend_dcb);
static void handleError(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *errbuf,
                        DCB *problem_dcb, mxs_error_action_t action, bool *succp);
static uint64_t getCapabilities(MXS_ROUTER* instance);
static bool rses_begin_locked_router_action(ROUTER_CLIENT_SES* rses);
static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses);
static SERVER_REF *get_root_master(SERVER_REF *servers);
static int handle_state_switch(DCB* dcb, DCB_REASON reason, void * routersession);

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
    MXS_NOTICE("Initialise readconnroute router module.");

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
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "A connection based router to load balance based on connections",
        "V1.1.0",
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

static inline void free_readconn_instance(ROUTER_INSTANCE *router)
{
    if (router)
    {
        MXS_FREE(router);
    }
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 *
 * @param service   The service this router is being create for
 * @param options   An array of options for this query router
 *
 * @return The instance data for this new instance
 */
static MXS_ROUTER *
createInstance(SERVICE *service, char **options)
{
    ROUTER_INSTANCE *inst;
    SERVER_REF *sref;
    int i, n;

    if ((inst = MXS_CALLOC(1, sizeof(ROUTER_INSTANCE))) == NULL)
    {
        return NULL;
    }

    inst->service = service;
    spinlock_init(&inst->lock);

    /*
     * Process the options
     */
    bool error = false;
    inst->bitmask = 0;
    inst->bitvalue = 0;
    if (options)
    {
        for (i = 0; options[i]; i++)
        {
            if (!strcasecmp(options[i], "master"))
            {
                inst->bitmask |= (SERVER_MASTER | SERVER_SLAVE);
                inst->bitvalue |= SERVER_MASTER;
            }
            else if (!strcasecmp(options[i], "slave"))
            {
                inst->bitmask |= (SERVER_MASTER | SERVER_SLAVE);
                inst->bitvalue |= SERVER_SLAVE;
            }
            else if (!strcasecmp(options[i], "running"))
            {
                inst->bitmask |= (SERVER_RUNNING);
                inst->bitvalue |= SERVER_RUNNING;
            }
            else if (!strcasecmp(options[i], "synced"))
            {
                inst->bitmask |= (SERVER_JOINED);
                inst->bitvalue |= SERVER_JOINED;
            }
            else if (!strcasecmp(options[i], "ndb"))
            {
                inst->bitmask |= (SERVER_NDB);
                inst->bitvalue |= SERVER_NDB;
            }
            else
            {
                MXS_WARNING("Unsupported router "
                            "option \'%s\' for readconnroute. "
                            "Expected router options are "
                            "[slave|master|synced|ndb|running]",
                            options[i]);
                error = true;
            }
        }
    }

    if (error)
    {
        free_readconn_instance(inst);
        return NULL;
    }

    if (inst->bitmask == 0 && inst->bitvalue == 0)
    {
        /** No parameters given, use RUNNING as a valid server */
        inst->bitmask |= (SERVER_RUNNING);
        inst->bitvalue |= SERVER_RUNNING;
    }
    /*
     * We have completed the creation of the instance data, so now
     * insert this router instance into the linked list of routers
     * that have been created with this module.
     */

    return (MXS_ROUTER *) inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance  The router instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_ROUTER_SESSION *
newSession(MXS_ROUTER *instance, MXS_SESSION *session)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *client_rses;
    SERVER_REF *candidate = NULL;
    int i;
    SERVER_REF *master_host = NULL;

    MXS_DEBUG("%lu [newSession] new router session with session "
              "%p, and inst %p.",
              pthread_self(),
              session,
              inst);

    client_rses = (ROUTER_CLIENT_SES *) MXS_CALLOC(1, sizeof(ROUTER_CLIENT_SES));

    if (client_rses == NULL)
    {
        return NULL;
    }

#if defined(SS_DEBUG)
    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif
    client_rses->client_dcb = session->client_dcb;

    /**
     * Find the Master host from available servers
     */
    master_host = get_root_master(inst->service->dbref);

    /**
     * Find a backend server to connect to. This is the extent of the
     * load balancing algorithm we need to implement for this simple
     * connection router.
     */

    /*
     * Loop over all the servers and find any that have fewer connections
     * than the candidate server.
     *
     * If a server has less connections than the current candidate we mark this
     * as the new candidate to connect to.
     *
     * If a server has the same number of connections currently as the candidate
     * and has had less connections over time than the candidate it will also
     * become the new candidate. This has the effect of spreading the
     * connections over different servers during periods of very low load.
     */
    for (SERVER_REF *ref = inst->service->dbref; ref; ref = ref->next)
    {
        if (!SERVER_REF_IS_ACTIVE(ref) || SERVER_IN_MAINT(ref->server))
        {
            continue;
        }
        else
        {
            MXS_DEBUG("%lu [newSession] Examine server in port %d with "
                      "%d connections. Status is %s, "
                      "inst->bitvalue is %d",
                      pthread_self(),
                      ref->server->port,
                      ref->connections,
                      STRSRVSTATUS(ref->server),
                      inst->bitmask);
        }

        /* Check server status bits against bitvalue from router_options */
        if (ref && SERVER_IS_RUNNING(ref->server) &&
            (ref->server->status & inst->bitmask & inst->bitvalue))
        {
            if (master_host)
            {
                if (ref == master_host && (inst->bitvalue & SERVER_SLAVE))
                {
                    /* Skip root master here, as it could also be slave of an external server that
                     * is not in the configuration.  Intermediate masters (Relay Servers) are also
                     * slave and will be selected as Slave(s)
                     */

                    continue;
                }
                if (ref == master_host && (inst->bitvalue & SERVER_MASTER))
                {
                    /* If option is "master" return only the root Master as there could be
                     * intermediate masters (Relay Servers) and they must not be selected.
                     */

                    candidate = master_host;
                    break;
                }
            }
            else
            {
                /* Master_host is NULL, no master server.  If requested router_option is 'master'
                 * candidate wll be NULL.
                 */
                if (inst->bitvalue & SERVER_MASTER)
                {
                    candidate = NULL;
                    break;
                }
            }

            /* If no candidate set, set first running server as our initial candidate server */
            if (candidate == NULL)
            {
                candidate = ref;
            }
            else if (ref->weight == 0 || candidate->weight == 0)
            {
                candidate = ref->weight ? ref : candidate;
            }
            else if (((ref->connections + 1) * 1000) / ref->weight <
                     ((candidate->connections + 1) * 1000) / candidate->weight)
            {
                /* This running server has fewer connections, set it as a new candidate */
                candidate = ref;
            }
            else if (((ref->connections + 1) * 1000) / ref->weight ==
                     ((candidate->connections + 1) * 1000) / candidate->weight &&
                     ref->server->stats.n_connections < candidate->server->stats.n_connections)
            {
                /* This running server has the same number of connections currently as the candidate
                but has had fewer connections over time than candidate, set this server to
                candidate*/
                candidate = ref;
            }
        }
    }

    /* If we haven't found a proper candidate yet but a master server is available, we'll pick that
     * with the assumption that it is "better" than a slave.
     */
    if (!candidate)
    {
        if (master_host)
        {
            candidate = master_host;
        }
        else
        {
            MXS_ERROR("Failed to create new routing session. Couldn't find eligible"
                      " candidate server. Freeing allocated resources.");
            MXS_FREE(client_rses);
            return NULL;
        }
    }

    /*
     * We now have the server with the least connections.
     * Bump the connection count for this server
     */
    client_rses->backend = candidate;

    /** Open the backend connection */
    client_rses->backend_dcb = dcb_connect(candidate->server, session,
                                           candidate->server->protocol);

    if (client_rses->backend_dcb == NULL)
    {
        /** The failure is reported in dcb_connect() */
        MXS_FREE(client_rses);
        return NULL;
    }

    atomic_add(&candidate->connections, 1);

    // TODO: Remove this as it is never called
    dcb_add_callback(client_rses->backend_dcb,
                     DCB_REASON_NOT_RESPONDING,
                     &handle_state_switch,
                     client_rses);
    inst->stats.n_sessions++;

    CHK_CLIENT_RSES(client_rses);

    MXS_INFO("New session for server %s. Connections : %d",
             candidate->server->unique_name, candidate->connections);

    return (void *) client_rses;
}

/**
 * @node Unlink from backend server, unlink from router's connection list,
 * and free memory of a router client session.
 *
 * Parameters:
 * @param router - <usage>
 *          <description>
 *
 * @param router_cli_ses - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void freeSession(MXS_ROUTER* router_instance, MXS_ROUTER_SESSION* router_client_ses)
{
    ROUTER_INSTANCE* router = (ROUTER_INSTANCE *) router_instance;
    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *) router_client_ses;

    ss_debug(int prev_val = ) atomic_add(&router_cli_ses->backend->connections, -1);
    ss_dassert(prev_val > 0);

    MXS_FREE(router_cli_ses);
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance      The router instance data
 * @param router_session    The session being closed
 */
static void
closeSession(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session)
{
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    DCB* backend_dcb;

    CHK_CLIENT_RSES(router_cli_ses);
    /**
     * Lock router client session for secure read and update.
     */
    if (rses_begin_locked_router_action(router_cli_ses))
    {
        /* decrease server current connection counter */

        backend_dcb = router_cli_ses->backend_dcb;
        router_cli_ses->backend_dcb = NULL;
        router_cli_ses->rses_closed = true;
        /** Unlock */
        rses_end_locked_router_action(router_cli_ses);

        /**
         * Close the backend server connection
         */
        if (backend_dcb != NULL)
        {
            CHK_DCB(backend_dcb);
            dcb_close(backend_dcb);
        }
    }
}

/** Log routing failure due to closed session */
static void log_closed_session(mysql_server_cmd_t mysql_command, bool is_closed,
                               SERVER_REF *ref, bool valid)
{
    char msg[MAX_SERVER_NAME_LEN + 200] = ""; // Extra space for message

    if (is_closed)
    {
        sprintf(msg, "Session is closed.");
    }
    else if (SERVER_IS_DOWN(ref->server))
    {
        sprintf(msg, "Server '%s' is down.", ref->server->unique_name);
    }
    else if (SERVER_IN_MAINT(ref->server))
    {
        sprintf(msg, "Server '%s' is in maintenance.", ref->server->unique_name);
    }
    else if (!valid)
    {
        sprintf(msg, "Server '%s' no longer qualifies as a target server.",
                ref->server->unique_name);
    }

    MXS_ERROR("Failed to route MySQL command %d to backend server. %s",
              mysql_command, msg);
}

/**
 * Check if the server we're connected to is still valid
 *
 * @param inst           Router instance
 * @param router_cli_ses Router session
 *
 * @return True if the backend connection is still valid
 */
static inline bool connection_is_valid(ROUTER_INSTANCE* inst, ROUTER_CLIENT_SES* router_cli_ses)
{
    bool rval = false;

    if (SERVER_IS_RUNNING(router_cli_ses->backend->server) &&
        (router_cli_ses->backend->server->status & inst->bitmask & inst->bitvalue))
    {
        if (inst->bitvalue & SERVER_MASTER)
        {
            rval = router_cli_ses->backend == get_root_master(inst->service->dbref);
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

/**
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance      The router instance
 * @param router_session    The router session returned from the newSession call
 * @param queue         The queue of data buffers to route
 * @return if succeed 1, otherwise 0
 */
static int
routeQuery(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue)
{
    ROUTER_INSTANCE *inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    int rc = 0;
    DCB* backend_dcb;
    MySQLProtocol *proto = (MySQLProtocol*)router_cli_ses->client_dcb->protocol;
    mysql_server_cmd_t mysql_command = proto->current_command;
    bool rses_is_closed;

    inst->stats.n_queries++;

    /** Dirty read for quick check if router is closed. */
    if (router_cli_ses->rses_closed)
    {
        rses_is_closed = true;
    }
    else
    {
        /**
         * Lock router client session for secure read of DCBs
         */
        rses_is_closed = !(rses_begin_locked_router_action(router_cli_ses));
    }

    if (!rses_is_closed)
    {
        backend_dcb = router_cli_ses->backend_dcb;
        /** unlock */
        rses_end_locked_router_action(router_cli_ses);
    }

    bool valid;

    if (rses_is_closed || backend_dcb == NULL ||
        (valid = !connection_is_valid(inst, router_cli_ses)))
    {
        log_closed_session(mysql_command, rses_is_closed, router_cli_ses->backend, valid);
        gwbuf_free(queue);
        goto return_rc;

    }

    char* trc = NULL;

    switch (mysql_command)
    {
    case MYSQL_COM_CHANGE_USER:
        rc = backend_dcb->func.auth(backend_dcb, NULL, backend_dcb->session,
                                    queue);
        break;
    case MYSQL_COM_QUERY:
        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            trc = modutil_get_SQL(queue);
        }
    default:
        rc = backend_dcb->func.write(backend_dcb, queue);
        break;
    }

    MXS_INFO("Routed [%s] to '%s'%s%s",
             STRPACKETTYPE(mysql_command),
             backend_dcb->server->unique_name,
             trc ? ": " : ".",
             trc ? trc : "");
    MXS_FREE(trc);

return_rc:

    return rc;
}

/**
 * Display router diagnostics
 *
 * @param instance  Instance of the router
 * @param dcb       DCB to send diagnostics to
 */
static void
diagnostics(MXS_ROUTER *router, DCB *dcb)
{
    ROUTER_INSTANCE *router_inst = (ROUTER_INSTANCE *) router;
    char *weightby;

    dcb_printf(dcb, "\tNumber of router sessions:   	%d\n",
               router_inst->stats.n_sessions);
    dcb_printf(dcb, "\tCurrent no. of router sessions:	%d\n",
               router_inst->service->stats.n_current);
    dcb_printf(dcb, "\tNumber of queries forwarded:   	%d\n",
               router_inst->stats.n_queries);
    if ((weightby = serviceGetWeightingParameter(router_inst->service))
        != NULL)
    {
        dcb_printf(dcb, "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb,
                   "\t\tServer               Target %% Connections\n");
        for (SERVER_REF *ref = router_inst->service->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb, "\t\t%-20s %3.1f%%     %d\n",
                       ref->server->unique_name,
                       (float) ref->weight / 10,
                       ref->connections);
        }
    }
}

/**
 * Client Reply routine
 *
 * The routine will reply to client data from backend server
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       backend_dcb     The backend DCB
 * @param       queue           The GWBUF with reply data
 */
static void
clientReply(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *queue, DCB *backend_dcb)
{
    ss_dassert(backend_dcb->session->client_dcb != NULL);
    MXS_SESSION_ROUTE_REPLY(backend_dcb->session, queue);
}

/**
 * Error Handler routine
 *
 * The routine will handle errors that occurred in writes.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       problem_dcb     The DCB related to the error
 * @param       action      The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param   succp       Result of action: true if router can continue
 *
 */
static void handleError(MXS_ROUTER *instance, MXS_ROUTER_SESSION *router_session, GWBUF *errbuf,
                        DCB *problem_dcb, mxs_error_action_t action, bool *succp)

{
    DCB *client_dcb;
    MXS_SESSION *session = problem_dcb->session;
    mxs_session_state_t sesstate;
    ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *) router_session;

    /** Don't handle same error twice on same DCB */
    if (problem_dcb->dcb_errhandle_called)
    {
        /** we optimistically assume that previous call succeed */
        *succp = true;
        return;
    }
    else
    {
        problem_dcb->dcb_errhandle_called = true;
    }

    sesstate = session->state;
    client_dcb = session->client_dcb;

    if (sesstate == SESSION_STATE_ROUTER_READY)
    {
        CHK_DCB(client_dcb);

        client_dcb->func.write(client_dcb, gwbuf_clone(errbuf));
    }

    if (DCB_ROLE_CLIENT_HANDLER == problem_dcb->dcb_role)
    {
        dcb_close(problem_dcb);
    }
    else if (router_cli_ses && problem_dcb == router_cli_ses->backend_dcb)
    {
        router_cli_ses->backend_dcb = NULL;
        dcb_close(problem_dcb);
    }

    /** false because connection is not available anymore */
    *succp = false;
}

/** to be inline'd */

/**
 * @node Acquires lock to router client session if it is not closed.
 *
 * Parameters:
 * @param rses - in, use
 *
 *
 * @return true if router session was not closed. If return value is true
 * it means that router is locked, and must be unlocked later. False, if
 * router was closed before lock was acquired.
 *
 *
 * @details (write detailed description here)
 *
 */
static bool rses_begin_locked_router_action(ROUTER_CLIENT_SES* rses)
{
    bool succp = false;

    CHK_CLIENT_RSES(rses);

    if (rses->rses_closed)
    {
        goto return_succp;
    }
    spinlock_acquire(&rses->rses_lock);
    if (rses->rses_closed)
    {
        spinlock_release(&rses->rses_lock);
        goto return_succp;
    }
    succp = true;

return_succp:
    return succp;
}

/** to be inline'd */

/**
 * @node Releases router client session lock.
 *
 * Parameters:
 * @param rses - <usage>
 *          <description>
 *
 * @return void
 *
 *
 * @details (write detailed description here)
 *
 */
static void rses_end_locked_router_action(ROUTER_CLIENT_SES* rses)
{
    CHK_CLIENT_RSES(rses);
    spinlock_release(&rses->rses_lock);
}

static uint64_t getCapabilities(MXS_ROUTER* instance)
{
    return RCAP_TYPE_NONE;
}

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param servers   The list of servers
 * @return      The Master found
 *
 */

static SERVER_REF *get_root_master(SERVER_REF *servers)
{
    int i = 0;
    SERVER_REF *master_host = NULL;

    for (SERVER_REF *ref = servers; ref; ref = ref->next)
    {
        if (ref->active && SERVER_IS_MASTER(ref->server))
        {
            if (master_host == NULL)
            {
                master_host = ref;
            }
            else if (ref->server->depth < master_host->server->depth ||
                     (ref->server->depth == master_host->server->depth &&
                      ref->weight > master_host->weight))
            {
                /**
                 * This master has a lower depth than the candidate master or
                 * the depths are equal but this master has a higher weight
                 */
                master_host = ref;
            }
        }
    }
    return master_host;
}

static int handle_state_switch(DCB* dcb, DCB_REASON reason, void * routersession)
{
    ss_dassert(dcb != NULL);
    MXS_SESSION* session = dcb->session;
    ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES*) routersession;
    SERVICE* service = session->service;
    MXS_ROUTER* router = (MXS_ROUTER *) service->router;

    if (NULL == dcb->session->router_session && DCB_REASON_ERROR != reason)
    {
        /*
         * We cannot handle a DCB that does not have a router session,
         * except in the case where error processing is invoked.
         */
        return 0;
    }
    switch (reason)
    {
    case DCB_REASON_CLOSE:
        dcb->func.close(dcb);
        break;
    case DCB_REASON_DRAINED:
        /** Do we need to do anything? */
        break;
    case DCB_REASON_HIGH_WATER:
        /** Do we need to do anything? */
        break;
    case DCB_REASON_LOW_WATER:
        /** Do we need to do anything? */
        break;
    case DCB_REASON_ERROR:
        dcb->func.error(dcb);
        break;
    case DCB_REASON_HUP:
        dcb->func.hangup(dcb);
        break;
    case DCB_REASON_NOT_RESPONDING:
        dcb->func.hangup(dcb);
        break;
    default:
        break;
    }

    return 0;
}
