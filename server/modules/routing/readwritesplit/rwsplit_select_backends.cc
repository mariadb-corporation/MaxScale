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

/**
 * Check whether it's possible to use this server as a slave
 *
 * @param server The slave candidate
 * @param master The master server or NULL if no master is available
 *
 * @return True if this server is a valid slave candidate
 */
static bool valid_for_slave(const SERVER *server, const SERVER *master)
{
    return (SERVER_IS_SLAVE(server) || SERVER_IS_RELAY_SERVER(server)) &&
           (master == NULL || (server != master));
}

/**
 * @brief Find the best slave candidate
 *
 * This function iterates through @c backend and tries to find the best backend
 * reference that is not in use. @c cmpfun will be called to compare the backends.
 *
 * @param rses   Router client session
 * @param master The master server
 * @param cmpfun qsort() compatible comparison function
 *
 * @return The best slave backend reference or NULL if no candidates could be found
 */
SRWBackend get_slave_candidate(const SRWBackendList& backends, const SERVER *master,
                               int (*cmpfun)(const SRWBackend&, const SRWBackend&))
{
    SRWBackend candidate;

    for (SRWBackendList::const_iterator it = backends.begin();
         it != backends.end(); it++)
    {
        const SRWBackend& backend = *it;

        if (!backend->in_use() && backend->can_connect() &&
            valid_for_slave(backend->server(), master))
        {
            if (candidate)
            {
                if (cmpfun(candidate, backend) > 0)
                {
                    candidate = backend;
                }
            }
            else
            {
                candidate = backend;
            }
        }
    }

    return candidate;
}

/** Compare number of connections from this router in backend servers */
static int backend_cmp_router_conn(const SRWBackend& a, const SRWBackend& b)
{
    SERVER_REF *first = a->backend();
    SERVER_REF *second = b->backend();

    if (first->weight == 0 && second->weight == 0)
    {
        return first->connections - second->connections;
    }
    else if (first->weight == 0)
    {
        return 1;
    }
    else if (second->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * first->connections) / first->weight) -
           ((1000 + 1000 * second->connections) / second->weight);
}

/** Compare number of global connections in backend servers */
static int backend_cmp_global_conn(const SRWBackend& a, const SRWBackend& b)
{
    SERVER_REF *first = a->backend();
    SERVER_REF *second = b->backend();

    if (first->weight == 0 && second->weight == 0)
    {
        return first->server->stats.n_current -
               second->server->stats.n_current;
    }
    else if (first->weight == 0)
    {
        return 1;
    }
    else if (second->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * first->server->stats.n_current) / first->weight) -
           ((1000 + 1000 * second->server->stats.n_current) / second->weight);
}

/** Compare replication lag between backend servers */
static int backend_cmp_behind_master(const SRWBackend& a, const SRWBackend& b)
{
    SERVER_REF *first = a->backend();
    SERVER_REF *second = b->backend();

    if (first->weight == 0 && second->weight == 0)
    {
        return first->server->rlag -
               second->server->rlag;
    }
    else if (first->weight == 0)
    {
        return 1;
    }
    else if (second->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * first->server->rlag) / first->weight) -
           ((1000 + 1000 * second->server->rlag) / second->weight);
}

/** Compare number of current operations in backend servers */
static int backend_cmp_current_load(const SRWBackend& a, const SRWBackend& b)
{
    SERVER_REF *first = a->backend();
    SERVER_REF *second = b->backend();

    if (first->weight == 0 && second->weight == 0)
    {
        return first->server->stats.n_current_ops - second->server->stats.n_current_ops;
    }
    else if (first->weight == 0)
    {
        return 1;
    }
    else if (second->weight == 0)
    {
        return -1;
    }

    return ((1000 + 1000 * first->server->stats.n_current_ops) / first->weight) -
           ((1000 + 1000 * second->server->stats.n_current_ops) / second->weight);
}

/**
 * The order of functions _must_ match with the order the select criteria are
 * listed in select_criteria_t definition in readwritesplit.h
 */
int (*criteria_cmpfun[LAST_CRITERIA])(const SRWBackend&, const SRWBackend&) =
{
    NULL,
    backend_cmp_global_conn,
    backend_cmp_router_conn,
    backend_cmp_behind_master,
    backend_cmp_current_load
};

/**
 * @brief Log server connections
 *
 * @param criteria Slave selection criteria
 * @param rses     Router client session
 */
static void log_server_connections(select_criteria_t criteria, const SRWBackendList& backends)
{
    MXS_INFO("Servers and %s connection counts:",
             criteria == LEAST_GLOBAL_CONNECTIONS ? "all MaxScale" : "router");

    for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
    {
        SERVER_REF* b = (*it)->backend();

        switch (criteria)
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
/**
 * @brief Find the master server that is at the root of the replication tree
 *
 * @param rses Router client session
 *
 * @return The root master reference or NULL if no master is found
 */
static SERVER_REF* get_root_master(const SRWBackendList& backends)
{
    SERVER_REF *master_host = NULL;

    for (SRWBackendList::const_iterator it = backends.begin();
         it != backends.end(); it++)
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

/**
 * @brief Search suitable backend servers from those of router instance
 *
 * It is assumed that there is only one master among servers of a router instance.
 * As a result, the first master found is chosen. There will possibly be more
 * backend references than connected backends because only those in correct state
 * are connected to.
 *
 * @param router_nservers Number of backend servers
 * @param max_nslaves     Upper limit for the number of slaves
 * @param select_criteria Slave selection criteria
 * @param session         Client session
 * @param router          Router instance
 * @param rses            Router client session
 * @param type            Connection type, ALL for all types, SLAVE for slaves only
 *
 * @return True if at least one master and one slave was found
 */
bool select_connect_backend_servers(int router_nservers,
                                    int max_nslaves,
                                    MXS_SESSION *session,
                                    const Config& config,
                                    SRWBackendList& backends,
                                    SRWBackend& current_master,
                                    mxs::SessionCommandList* sescmd_list,
                                    int* expected_responses,
                                    connection_type type)
{
    /* get the root Master */
    SERVER_REF *master_backend = get_root_master(backends);
    SERVER  *master_host = master_backend ? master_backend->server : NULL;

    if (config.master_failure_mode == RW_FAIL_INSTANTLY &&
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
    bool master_connected = type == SLAVE || current_master;

    /** Check slave selection criteria and set compare function */
    select_criteria_t select_criteria = config.slave_selection_criteria;
    int (*cmpfun)(const SRWBackend&, const SRWBackend&) = criteria_cmpfun[select_criteria];
    ss_dassert(cmpfun);

    if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
    {
        log_server_connections(select_criteria, backends);
    }

    int slaves_found = 0;
    int slaves_connected = 0;
    const int min_nslaves = 0; /*< not configurable at the time */
    bool succp = false;

    if (!master_connected)
    {
        /** Find a master server */
        for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
        {
            const SRWBackend& backend = *it;

            if (backend->can_connect() && master_host && backend->server() == master_host)
            {
                if (backend->connect(session))
                {
                    current_master = backend;
                }
            }
        }
    }

    /** Calculate how many connections we already have */
    for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
    {
        const SRWBackend& backend = *it;

        if (backend->can_connect() && valid_for_slave(backend->server(), master_host))
        {
            slaves_found += 1;

            if (backend->in_use())
            {
                slaves_connected += 1;
            }
        }
    }

    ss_dassert(slaves_connected < max_nslaves || max_nslaves == 0);

    /** Connect to all possible slaves */
    for (SRWBackend backend(get_slave_candidate(backends, master_host, cmpfun));
         backend && slaves_connected < max_nslaves;
         backend = get_slave_candidate(backends, master_host, cmpfun))
    {
        if (backend->can_connect() && backend->connect(session))
        {
            if (sescmd_list && sescmd_list->size())
            {
                backend->append_session_command(*sescmd_list);

                if (backend->execute_session_command())
                {
                    if (expected_responses)
                    {
                        (*expected_responses)++;
                    }
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

            for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
            {
                const SRWBackend& backend = *it;
                if (backend->in_use())
                {
                    MXS_INFO("Selected %s in \t%s", STRSRVSTATUS(backend->server()),
                             backend->uri());
                }
            }
        }
    }
    else
    {
        MXS_ERROR("Couldn't establish required amount of slave connections for "
                  "router session. Would need between %d and %d slaves but only have %d.",
                  min_nslaves, max_nslaves, slaves_connected);
        close_all_connections(backends);
    }

    return succp;
}
