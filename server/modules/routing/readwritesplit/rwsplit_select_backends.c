/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <router.h>
#include <readwritesplit.h>
#include <rwsplit_internal.h>
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

static bool connect_server(backend_ref_t *bref, SESSION *session, bool execute_history);

static void log_server_connections(select_criteria_t select_criteria,
                            backend_ref_t *backend_ref, int router_nservers);

static BACKEND *get_root_master(backend_ref_t *servers, int router_nservers);

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

/*
 * The following function is the only one that is called from elsewhere in
 * the read write split router. It is not intended for use from outside this
 * router. Other functions in this module are internal and are called 
 * directly or indirectly by this function.
 */

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
                                           SESSION *session,
                                           ROUTER_INSTANCE *router)
{
    if (p_master_ref == NULL || backend_ref == NULL)
    {
        MXS_ERROR("Master reference (%p) or backend reference (%p) is NULL.",
                  p_master_ref, backend_ref);
        ss_dassert(false);
        return false;
    }

    /* get the root Master */
    BACKEND *master_host = get_root_master(backend_ref, router_nservers);

    if (router->rwsplit_config.rw_master_failure_mode == RW_FAIL_INSTANTLY &&
        (master_host == NULL || SERVER_IS_DOWN(master_host->backend_server)))
    {
        MXS_ERROR("Couldn't find suitable Master from %d candidates.", router_nservers);
        return false;
    }

    /**
     * Existing session : master is already chosen and connected.
     * The function was called because new slave must be selected to replace
     * failed one.
     */
    bool master_connected = *p_master_ref != NULL;

    /** Check slave selection criteria and set compare function */
    int (*p)(const void *, const void *) = criteria_cmpfun[select_criteria];
    ss_dassert(p);

    /** Sort the pointer list to servers according to slave selection criteria.
     * The servers that match the criteria the best are at the beginning of
     * the list. */
    qsort(backend_ref, (size_t) router_nservers, sizeof(backend_ref_t), p);

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        log_server_connections(select_criteria, backend_ref, router_nservers);
    }

    int slaves_found = 0;
    int slaves_connected = 0;
    const int min_nslaves = 0; /*< not configurable at the time */
    bool succp = false;

    /**
     * Choose at least 1+min_nslaves (master and slave) and at most 1+max_nslaves
     * servers from the sorted list. First master found is selected.
     */
    for (int i = 0; i < router_nservers &&
         (slaves_connected < max_nslaves || !master_connected); i++)
    {
        SERVER *serv = backend_ref[i].bref_backend->backend_server;

        if (!BREF_HAS_FAILED(&backend_ref[i]) && SERVER_IS_RUNNING(serv))
        {
            /* check also for relay servers and don't take the master_host */
            if (slaves_found < max_nslaves &&
                (max_slave_rlag == MAX_RLAG_UNDEFINED ||
                 (serv->rlag != MAX_RLAG_NOT_AVAILABLE &&
                  serv->rlag <= max_slave_rlag)) &&
                (SERVER_IS_SLAVE(serv) || SERVER_IS_RELAY_SERVER(serv)) &&
                (master_host == NULL || (serv != master_host->backend_server)))
            {
                slaves_found += 1;

                if (BREF_IS_IN_USE((&backend_ref[i])) ||
                    connect_server(&backend_ref[i], session, true))
                {
                    slaves_connected += 1;
                }
            }
                /* take the master_host for master */
            else if (master_host && (serv == master_host->backend_server))
            {
                /** p_master_ref must be assigned with this backend_ref pointer
                 * because its original value may have been lost when backend
                 * references were sorted with qsort. */
                *p_master_ref = &backend_ref[i];

                if (!master_connected)
                {
                    if (connect_server(&backend_ref[i], session, false))
                    {
                        master_connected = true;
                    }
                }
            }
        }
    } /*< for */

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
                    MXS_INFO("Selected %s in \t%s:%d",
                             STRSRVSTATUS(backend_ref[i].bref_backend->backend_server),
                             backend_ref[i].bref_backend->backend_server->name,
                             backend_ref[i].bref_backend->backend_server->port);
                }
            } /* for */
        }
    }
        /** Failure cases */
    else
    {
        if (slaves_connected < min_nslaves)
        {
            MXS_ERROR("Couldn't establish required amount of "
                      "slave connections for router session.");
        }

        /** Clean up connections */
        for (int i = 0; i < router_nservers; i++)
        {
            if (BREF_IS_IN_USE((&backend_ref[i])))
            {
                ss_dassert(backend_ref[i].bref_backend->backend_conn_count > 0);

                /** disconnect opened connections */
                bref_clear_state(&backend_ref[i], BREF_IN_USE);
                /** Decrease backend's connection counter. */
                atomic_add(&backend_ref[i].bref_backend->backend_conn_count, -1);
                dcb_close(backend_ref[i].bref_dcb);
            }
        }
    }

    return succp;
}

/** Compare number of connections from this router in backend servers */
static int bref_cmp_router_conn(const void *bref1, const void *bref2)
{
    BACKEND *b1 = ((backend_ref_t *)bref1)->bref_backend;
    BACKEND *b2 = ((backend_ref_t *)bref2)->bref_backend;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->backend_server->stats.n_current -
               b2->backend_server->stats.n_current;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->backend_conn_count) / b1->weight) -
           ((1000 + 1000 * b2->backend_conn_count) / b2->weight);
}

/** Compare number of global connections in backend servers */
static int bref_cmp_global_conn(const void *bref1, const void *bref2)
{
    BACKEND *b1 = ((backend_ref_t *)bref1)->bref_backend;
    BACKEND *b2 = ((backend_ref_t *)bref2)->bref_backend;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->backend_server->stats.n_current -
               b2->backend_server->stats.n_current;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * b1->backend_server->stats.n_current) / b1->weight) -
           ((1000 + 1000 * b2->backend_server->stats.n_current) / b2->weight);
}

/** Compare replication lag between backend servers */
static int bref_cmp_behind_master(const void *bref1, const void *bref2)
{
    BACKEND *b1 = ((backend_ref_t *)bref1)->bref_backend;
    BACKEND *b2 = ((backend_ref_t *)bref2)->bref_backend;

    return ((b1->backend_server->rlag < b2->backend_server->rlag) ? -1
            : ((b1->backend_server->rlag > b2->backend_server->rlag) ? 1 : 0));
}

/** Compare number of current operations in backend servers */
static int bref_cmp_current_load(const void *bref1, const void *bref2)
{
    SERVER *s1 = ((backend_ref_t *)bref1)->bref_backend->backend_server;
    SERVER *s2 = ((backend_ref_t *)bref2)->bref_backend->backend_server;
    BACKEND *b1 = ((backend_ref_t *)bref1)->bref_backend;
    BACKEND *b2 = ((backend_ref_t *)bref2)->bref_backend;

    if (b1->weight == 0 && b2->weight == 0)
    {
        return b1->backend_server->stats.n_current -
               b2->backend_server->stats.n_current;
    }
    else if (b1->weight == 0)
    {
        return 1;
    }
    else if (b2->weight == 0)
    {
        return -1;
    }

    return ((1000 * s1->stats.n_current_ops) - b1->weight) -
           ((1000 * s2->stats.n_current_ops) - b2->weight);
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
static bool connect_server(backend_ref_t *bref, SESSION *session, bool execute_history)
{
    SERVER *serv = bref->bref_backend->backend_server;
    bool rval = false;

    bref->bref_dcb = dcb_connect(serv, session, serv->protocol);

    if (bref->bref_dcb != NULL)
    {
        if (!execute_history || execute_sescmd_history(bref))
        {
            /** Add a callback for unresponsive server */
            dcb_add_callback(bref->bref_dcb, DCB_REASON_NOT_RESPONDING,
                             &router_handle_state_switch, (void *) bref);
            bref->bref_state = 0;
            bref_set_state(bref, BREF_IN_USE);
            atomic_add(&bref->bref_backend->backend_conn_count, 1);
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to execute session command in %s (%s:%d). See earlier "
                      "errors for more details.",
                      bref->bref_backend->backend_server->unique_name,
                      bref->bref_backend->backend_server->name,
                      bref->bref_backend->backend_server->port);
            dcb_close(bref->bref_dcb);
            bref->bref_dcb = NULL;
        }
    }
    else
    {
        MXS_ERROR("Unable to establish connection with server %s:%d",
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
            BACKEND *b = backend_ref[i].bref_backend;

            switch (select_criteria)
            {
                case LEAST_GLOBAL_CONNECTIONS:
                    MXS_INFO("MaxScale connections : %d in \t%s:%d %s",
                             b->backend_server->stats.n_current, b->backend_server->name,
                             b->backend_server->port, STRSRVSTATUS(b->backend_server));
                    break;

                case LEAST_ROUTER_CONNECTIONS:
                    MXS_INFO("RWSplit connections : %d in \t%s:%d %s",
                             b->backend_conn_count, b->backend_server->name,
                             b->backend_server->port, STRSRVSTATUS(b->backend_server));
                    break;

                case LEAST_CURRENT_OPERATIONS:
                    MXS_INFO("current operations : %d in \t%s:%d %s",
                             b->backend_server->stats.n_current_ops,
                             b->backend_server->name, b->backend_server->port,
                             STRSRVSTATUS(b->backend_server));
                    break;

                case LEAST_BEHIND_MASTER:
                    MXS_INFO("replication lag : %d in \t%s:%d %s",
                             b->backend_server->rlag, b->backend_server->name,
                             b->backend_server->port, STRSRVSTATUS(b->backend_server));
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
static BACKEND *get_root_master(backend_ref_t *servers, int router_nservers)
{
    int i = 0;
    BACKEND *master_host = NULL;

    for (i = 0; i < router_nservers; i++)
    {
        BACKEND *b;

        if (servers[i].bref_backend == NULL)
        {
            continue;
        }

        b = servers[i].bref_backend;

        if ((b->backend_server->status & (SERVER_MASTER | SERVER_MAINT)) ==
            SERVER_MASTER)
        {
            if (master_host == NULL ||
                (b->backend_server->depth < master_host->backend_server->depth))
            {
                master_host = b;
            }
        }
    }
    return master_host;
}
