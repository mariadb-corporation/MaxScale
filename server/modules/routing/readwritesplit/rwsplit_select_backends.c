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

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>
#include "rwsplit_internal.h"
/**
 * @file rwsplit_select_backends.c   The functions that implement back end
 * selection for the read write split router. All of these functions are
 * internal to that router and not intended to be called from elsewhere.
 *
 * @verbatim
 * Revision History
 *
 * Date          Who                 Description
 * 08/08/2016    Martin Brampton     Initial implementation
 *
 * @endverbatim
 */

static bool connect_server(backend_ref_t *bref, MXS_SESSION *session, bool execute_history);

static void log_server_connections(select_criteria_t select_criteria,
                                   backend_ref_t *backend_ref, int router_nservers);

static SERVER_REF *get_root_master(backend_ref_t *servers, int router_nservers);

static int bref_cmp_global_conn(const void *bref1, const void *bref2);

static int bref_cmp_router_conn(const void *bref1, const void *bref2);

static int bref_cmp_behind_master(const void *bref1, const void *bref2);

static int bref_cmp_current_load(const void *bref1, const void *bref2);

/**
 * The order of functions _must_ match with the order the select criteria are
 * listed in select_criteria_t definition in readwritesplit.h
 */
int (*criteria_cmpfun[LAST_CRITERIA])(const void *, const void *) =
{
    NULL,
    bref_cmp_global_conn,
    bref_cmp_router_conn,
    bref_cmp_behind_master,
    bref_cmp_current_load
};

/**
 * @brief Check whether it's possible to connect to this server
 *
 * @param bref Backend reference
 * @return True if a connection to this server can be attempted
 */
static bool bref_valid_for_connect(const backend_ref_t *bref)
{
    return !BREF_HAS_FAILED(bref) && SERVER_IS_RUNNING(bref->ref->server);
}

/**
 * Check whether it's possible to use this server as a slave
 *
 * @param bref Backend reference
 * @param master_host The master server
 * @return True if this server is a valid slave candidate
 */
static bool bref_valid_for_slave(const backend_ref_t *bref, const SERVER *master_host)
{
    SERVER *server = bref->ref->server;

    return (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server)) &&
           (master_host == NULL || (server != master_host));
}

/**
 * @brief Find the best slave candidate
 *
 * This function iterates through @c bref and tries to find the best backend
 * reference that is not in use. @c cmpfun will be called to compare the backends.
 *
 * @param bref Backend reference
 * @param n Size of @c bref
 * @param master The master server
 * @param cmpfun qsort() compatible comparison function
 * @return The best slave backend reference or NULL if no candidates could be found
 */
backend_ref_t* get_slave_candidate(backend_ref_t *bref, int n, const SERVER *master,
                                   int (*cmpfun)(const void *, const void *))
{
    backend_ref_t *candidate = NULL;

    for (int i = 0; i < n; i++)
    {
        if (!BREF_IS_IN_USE(&bref[i]) &&
            bref_valid_for_connect(&bref[i]) &&
            bref_valid_for_slave(&bref[i], master))
        {
            if (candidate)
            {
                if (cmpfun(candidate, &bref[i]) > 0)
                {
                    candidate = &bref[i];
                }
            }
            else
            {
                candidate = &bref[i];
            }
        }
    }

    return candidate;
}

/**
 * @brief Search suitable backend servers from those of router instance
 *
 * It is assumed that there is only one master among servers of a router instance.
 * As a result, the first master found is chosen. There will possibly be more
 * backend references than connected backends because only those in correct state
 * are connected to.
 *
 * @param p_master_ref Pointer to location where master's backend reference is to  be stored
 * @param backend_ref Pointer to backend server reference object array
 * @param router_nservers Number of backend server pointers pointed to by @p backend_ref
 * @param max_nslaves Upper limit for the number of slaves
 * @param max_slave_rlag Maximum allowed replication lag for any slave
 * @param select_criteria Slave selection criteria
 * @param session Client session
 * @param router Router instance
 * @return true, if at least one master and one slave was found.
 */
bool select_connect_backend_servers(backend_ref_t **p_master_ref,
                                    backend_ref_t *backend_ref,
                                    int router_nservers, int max_nslaves,
                                    int max_slave_rlag,
                                    select_criteria_t select_criteria,
                                    MXS_SESSION *session,
                                    ROUTER_INSTANCE *router,
                                    bool active_session)
{
    if (p_master_ref == NULL || backend_ref == NULL)
    {
        MXS_ERROR("Master reference (%p) or backend reference (%p) is NULL.",
                  p_master_ref, backend_ref);
        ss_dassert(false);
        return false;
    }

    /* get the root Master */
    SERVER_REF *master_backend = get_root_master(backend_ref, router_nservers);
    SERVER  *master_host = master_backend ? master_backend->server : NULL;

    if (router->rwsplit_config.master_failure_mode == RW_FAIL_INSTANTLY &&
        (master_host == NULL || SERVER_IS_DOWN(master_host)))
    {
        MXS_ERROR("Couldn't find suitable Master from %d candidates.", router_nservers);
        return false;
    }

    /**
     * New session:
     *
     * Connect to both master and slaves
     *
     * Existing session:
     *
     * Master is already connected or we don't have a master. The function was
     * called because new slaves must be selected to replace failed ones.
     */
    bool master_connected = active_session || *p_master_ref != NULL;

    /** Check slave selection criteria and set compare function */
    int (*p)(const void *, const void *) = criteria_cmpfun[select_criteria];
    ss_dassert(p);

    SERVER *old_master = *p_master_ref ? (*p_master_ref)->ref->server : NULL;

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        log_server_connections(select_criteria, backend_ref, router_nservers);
    }

    int slaves_found = 0;
    int slaves_connected = 0;
    const int min_nslaves = 0; /*< not configurable at the time */
    bool succp = false;

    if (!master_connected)
    {
        /** Find a master server */
        for (int i = 0; i < router_nservers; i++)
        {
            SERVER *serv = backend_ref[i].ref->server;

            if (bref_valid_for_connect(&backend_ref[i]) &&
                master_host && serv == master_host)
            {
                if (connect_server(&backend_ref[i], session, false))
                {
                    *p_master_ref = &backend_ref[i];
                    break;
                }
            }
        }
    }

    /** Calculate how many connections we already have */
    for (int i = 0; i < router_nservers; i++)
    {
        if (bref_valid_for_connect(&backend_ref[i]) &&
            bref_valid_for_slave(&backend_ref[i], master_host))
        {
            slaves_found += 1;

            if (BREF_IS_IN_USE(&backend_ref[i]))
            {
                slaves_connected += 1;
            }
        }
    }

    ss_dassert(slaves_connected < max_nslaves || max_nslaves == 0);

    backend_ref_t *bref = get_slave_candidate(backend_ref, router_nservers, master_host, p);

    /** Connect to all possible slaves */
    while (bref && slaves_connected < max_nslaves)
    {
        if (connect_server(bref, session, true))
        {
            slaves_connected += 1;
        }
        else
        {
            /** Failed to connect, mark server as failed */
            bref_set_state(bref, BREF_FATAL_FAILURE);
        }

        bref = get_slave_candidate(backend_ref, router_nservers, master_host, p);
    }

    /**
     * Successful cases
     */
    if (slaves_connected >= min_nslaves && slaves_connected <= max_nslaves)
    {
        succp = true;

        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            if (slaves_connected < max_nslaves)
            {
                MXS_INFO("Couldn't connect to maximum number of "
                         "slaves. Connected successfully to %d slaves "
                         "of %d of them.", slaves_connected, slaves_found);
            }

            for (int i = 0; i < router_nservers; i++)
            {
                if (BREF_IS_IN_USE((&backend_ref[i])))
                {
                    MXS_INFO("Selected %s in \t[%s]:%d",
                             STRSRVSTATUS(backend_ref[i].ref->server),
                             backend_ref[i].ref->server->name,
                             backend_ref[i].ref->server->port);
                }
            } /* for */
        }
    }
    /** Failure cases */
    else
    {
        MXS_ERROR("Couldn't establish required amount of slave connections for "
                  "router session. Would need between %d and %d slaves but only have %d.",
                  min_nslaves, max_nslaves, slaves_connected);

        /** Clean up connections */
        for (int i = 0; i < router_nservers; i++)
        {
            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                ss_dassert(backend_ref[i].ref->connections > 0);

                close_failed_bref(&backend_ref[i], true);

                /** Decrease backend's connection counter. */
                atomic_add(&backend_ref[i].ref->connections, -1);
                RW_CHK_DCB(&backend_ref[i], backend_ref[i].bref_dcb);
                dcb_close(backend_ref[i].bref_dcb);
                RW_CLOSE_BREF(&backend_ref[i]);
            }
        }
    }

    return succp;
}

/** Compare number of connections from this router in backend servers */
static int bref_cmp_router_conn(const void *bref1, const void *bref2)
{
    SERVER_REF *b1 = ((backend_ref_t *)bref1)->ref;
    SERVER_REF *b2 = ((backend_ref_t *)bref2)->ref;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->connections - b2->connections;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->connections) / b1->weight) -
           ((1000 + 1000 * b2->connections) / b2->weight);
}

/** Compare number of global connections in backend servers */
static int bref_cmp_global_conn(const void *bref1, const void *bref2)
{
    SERVER_REF *b1 = ((backend_ref_t *)bref1)->ref;
    SERVER_REF *b2 = ((backend_ref_t *)bref2)->ref;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->server->stats.n_current -
               b2->server->stats.n_current;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->server->stats.n_current) / b1->weight) -
           ((1000 + 1000 * b2->server->stats.n_current) / b2->weight);
}

/** Compare replication lag between backend servers */
static int bref_cmp_behind_master(const void *bref1, const void *bref2)
{
    SERVER_REF *b1 = ((backend_ref_t *)bref1)->ref;
    SERVER_REF *b2 = ((backend_ref_t *)bref2)->ref;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->server->rlag -
               b2->server->rlag;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->server->rlag) / b1->weight) -
           ((1000 + 1000 * b2->server->rlag) / b2->weight);
}

/** Compare number of current operations in backend servers */
static int bref_cmp_current_load(const void *bref1, const void *bref2)
{
    SERVER_REF *b1 = ((backend_ref_t *)bref1)->ref;
    SERVER_REF *b2 = ((backend_ref_t *)bref2)->ref;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->server->stats.n_current_ops - b2->server->stats.n_current_ops;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->server->stats.n_current_ops) / b1->weight) -
           ((1000 + 1000 * b2->server->stats.n_current_ops) / b2->weight);
}

/**
 * @brief Connect a server
 *
 * Connects to a server, adds callbacks to the created DCB and updates
 * router statistics. If @p execute_history is true, the session command
 * history will be executed on this server.
 *
 * @param b Router's backend structure for the server
 * @param session Client's session object
 * @param execute_history Execute session command history
 * @return True if successful, false if an error occurred
 */
static bool connect_server(backend_ref_t *bref, MXS_SESSION *session, bool execute_history)
{
    SERVER *serv = bref->ref->server;
    bool rval = false;

    bref->bref_dcb = dcb_connect(serv, session, serv->protocol);

    if (bref->bref_dcb != NULL)
    {
        bref_clear_state(bref, BREF_CLOSED);
        bref->closed_at = 0;

        if (!execute_history || execute_sescmd_history(bref))
        {
            /** Add a callback for unresponsive server */
            dcb_add_callback(bref->bref_dcb, DCB_REASON_NOT_RESPONDING,
                             &router_handle_state_switch, (void *) bref);
            bref->bref_state = 0;
            bref_set_state(bref, BREF_IN_USE);
            atomic_add(&bref->ref->connections, 1);
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to execute session command in %s ([%s]:%d). See earlier "
                      "errors for more details.",
                      bref->ref->server->unique_name,
                      bref->ref->server->name,
                      bref->ref->server->port);
            RW_CHK_DCB(bref, bref->bref_dcb);
            dcb_close(bref->bref_dcb);
            RW_CLOSE_BREF(bref);
            bref->bref_dcb = NULL;
        }
    }
    else
    {
        MXS_ERROR("Unable to establish connection with server [%s]:%d",
                  serv->name, serv->port);
    }

    return rval;
}

/**
 * @brief Log server connections
 *
 * @param select_criteria Slave selection criteria
 * @param backend_ref Backend reference array
 * @param router_nservers Number of backends in @p backend_ref
 */
static void log_server_connections(select_criteria_t select_criteria,
                                   backend_ref_t *backend_ref, int router_nservers)
{
    if (select_criteria == LEAST_GLOBAL_CONNECTIONS ||
        select_criteria == LEAST_ROUTER_CONNECTIONS ||
        select_criteria == LEAST_BEHIND_MASTER ||
        select_criteria == LEAST_CURRENT_OPERATIONS)
    {
        MXS_INFO("Servers and %s connection counts:",
                 select_criteria == LEAST_GLOBAL_CONNECTIONS ? "all MaxScale"
                 : "router");

        for (int i = 0; i < router_nservers; i++)
        {
            SERVER_REF *b = backend_ref[i].ref;

            switch (select_criteria)
            {
            case LEAST_GLOBAL_CONNECTIONS:
                MXS_INFO("MaxScale connections : %d in \t[%s]:%d %s",
                         b->server->stats.n_current, b->server->name,
                         b->server->port, STRSRVSTATUS(b->server));
                break;

            case LEAST_ROUTER_CONNECTIONS:
                MXS_INFO("RWSplit connections : %d in \t[%s]:%d %s",
                         b->connections, b->server->name,
                         b->server->port, STRSRVSTATUS(b->server));
                break;

            case LEAST_CURRENT_OPERATIONS:
                MXS_INFO("current operations : %d in \t[%s]:%d %s",
                         b->server->stats.n_current_ops,
                         b->server->name, b->server->port,
                         STRSRVSTATUS(b->server));
                break;

            case LEAST_BEHIND_MASTER:
                MXS_INFO("replication lag : %d in \t[%s]:%d %s",
                         b->server->rlag, b->server->name,
                         b->server->port, STRSRVSTATUS(b->server));
            default:
                break;
            }
        }
    }
}

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param   servers     The list of servers
 * @param   router_nservers The number of servers
 * @return          The Master found
 *
 */
static SERVER_REF *get_root_master(backend_ref_t *servers, int router_nservers)
{
    int i = 0;
    SERVER_REF *master_host = NULL;

    for (i = 0; i < router_nservers; i++)
    {
        if (servers[i].ref == NULL)
        {
            /** This should not happen */
            ss_dassert(false);
            continue;
        }

        SERVER_REF *b = servers[i].ref;

        if (SERVER_IS_MASTER(b->server))
        {
            if (master_host == NULL ||
                (b->server->depth < master_host->server->depth))
            {
                master_host = b;
            }
        }
    }
    return master_host;
}
