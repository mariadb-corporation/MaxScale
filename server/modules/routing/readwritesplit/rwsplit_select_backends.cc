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

#include "readwritesplit.hh"
#include "rwsplit_internal.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>

/**
 * The functions that implement back end selection for the read write
 * split router. All of these functions are internal to that router and
 * not intended to be called from elsewhere.
 */

static void log_server_connections(select_criteria_t select_criteria,
                                   ROUTER_CLIENT_SES* rses);

static SERVER_REF *get_root_master(ROUTER_CLIENT_SES* rses);

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
 * Check whether it's possible to use this server as a slave
 *
 * @param bref Backend reference
 * @param master_host The master server
 * @return True if this server is a valid slave candidate
 */
static bool valid_for_slave(const SERVER *server, const SERVER *master_host)
{
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
SRWBackend get_slave_candidate(ROUTER_CLIENT_SES* rses, const SERVER *master,
                               int (*cmpfun)(const void *, const void *))
{
    SRWBackend candidate;

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend& bref = *it;

        if (!bref->in_use() && bref->can_connect() &&
            valid_for_slave(bref->server(), master))
        {
            if (candidate)
            {
                if (cmpfun(&candidate, &bref) > 0)
                {
                    candidate = bref;
                }
            }
            else
            {
                candidate = bref;
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
bool select_connect_backend_servers(int router_nservers,
                                    int max_nslaves,
                                    select_criteria_t select_criteria,
                                    MXS_SESSION *session,
                                    ROUTER_INSTANCE *router,
                                    ROUTER_CLIENT_SES *rses,
                                    connection_type type)
{
    /* get the root Master */
    SERVER_REF *master_backend = get_root_master(rses);
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
    bool master_connected = type == SLAVE || rses->current_master;

    /** Check slave selection criteria and set compare function */
    int (*cmpfun)(const void *, const void *) = criteria_cmpfun[select_criteria];
    ss_dassert(cmpfun);

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        log_server_connections(select_criteria, rses);
    }

    int slaves_found = 0;
    int slaves_connected = 0;
    const int min_nslaves = 0; /*< not configurable at the time */
    bool succp = false;

    if (!master_connected)
    {
        /** Find a master server */
        for (SRWBackendList::iterator it = rses->backends.begin();
             it != rses->backends.end(); it++)
        {
            SRWBackend& bref = *it;

            if (bref->can_connect() && master_host && bref->server() == master_host)
            {
                if (bref->connect(session))
                {
                    rses->current_master = bref;
                }
            }
        }
    }

    /** Calculate how many connections we already have */
    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SRWBackend& bref = *it;

        if (bref->can_connect() && valid_for_slave(bref->server(), master_host))
        {
            slaves_found += 1;

            if (bref->in_use())
            {
                slaves_connected += 1;
            }
        }
    }

    ss_dassert(slaves_connected < max_nslaves || max_nslaves == 0);

    /** Connect to all possible slaves */
    for (SRWBackend bref(get_slave_candidate(rses, master_host, cmpfun));
         bref && slaves_connected < max_nslaves;
         bref = get_slave_candidate(rses, master_host, cmpfun))
    {
        if (bref->can_connect() && bref->connect(session))
        {
            if (rses->sescmd_list.size())
            {
                bref->append_session_command(rses->sescmd_list);

                if (bref->execute_session_command())
                {
                    rses->expected_responses++;
                    slaves_connected++;
                }
            }
            else
            {
                slaves_connected++;
            }
        }
    }

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

            for (SRWBackendList::iterator it = rses->backends.begin();
                 it != rses->backends.end(); it++)
            {
                SRWBackend& bref = *it;
                if (bref->in_use())
                {
                    MXS_INFO("Selected %s in \t[%s]:%d", STRSRVSTATUS(bref->server()),
                             bref->server()->name, bref->server()->port);
                }
            }
        }
    }
    else
    {
        MXS_ERROR("Couldn't establish required amount of slave connections for "
                  "router session. Would need between %d and %d slaves but only have %d.",
                  min_nslaves, max_nslaves, slaves_connected);
        close_all_connections(rses);
    }

    return succp;
}

/** Compare number of connections from this router in backend servers */
static int bref_cmp_router_conn(const void *bref1, const void *bref2)
{
    const SRWBackend& a = *reinterpret_cast<const SRWBackend*>(bref1);
    const SRWBackend& b = *reinterpret_cast<const SRWBackend*>(bref2);
    SERVER_REF *b1 = a->backend();
    SERVER_REF *b2 = b->backend();

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
    const SRWBackend& a = *reinterpret_cast<const SRWBackend*>(bref1);
    const SRWBackend& b = *reinterpret_cast<const SRWBackend*>(bref2);
    SERVER_REF *b1 = a->backend();
    SERVER_REF *b2 = b->backend();

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
    const SRWBackend& a = *reinterpret_cast<const SRWBackend*>(bref1);
    const SRWBackend& b = *reinterpret_cast<const SRWBackend*>(bref2);
    SERVER_REF *b1 = a->backend();
    SERVER_REF *b2 = b->backend();

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
    const SRWBackend& a = *reinterpret_cast<const SRWBackend*>(bref1);
    const SRWBackend& b = *reinterpret_cast<const SRWBackend*>(bref2);
    SERVER_REF *b1 = a->backend();
    SERVER_REF *b2 = b->backend();

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
 * @brief Log server connections
 *
 * @param select_criteria Slave selection criteria
 * @param backend_ref Backend reference array
 * @param router_nservers Number of backends in @p backend_ref
 */
static void log_server_connections(select_criteria_t select_criteria,
                                   ROUTER_CLIENT_SES* rses)
{
    MXS_INFO("Servers and %s connection counts:",
             select_criteria == LEAST_GLOBAL_CONNECTIONS ? "all MaxScale"
             : "router");

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SERVER_REF* b = (*it)->backend();

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
            ss_dassert(!true);
            break;
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
static SERVER_REF *get_root_master(ROUTER_CLIENT_SES* rses)
{
    SERVER_REF *master_host = NULL;

    for (SRWBackendList::iterator it = rses->backends.begin();
         it != rses->backends.end(); it++)
    {
        SERVER_REF* b = (*it)->backend();

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
