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
static bool valid_for_slave(const SRWBackend& backend, const SERVER_REF *master)
{
    return (backend->is_slave() || backend->is_relay()) &&
           (master == NULL || (backend->server() != master->server));
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
static SRWBackend get_slave_candidate(const SRWBackendList& backends, const SERVER_REF *master,
                                      int (*cmpfun)(const SRWBackend&, const SRWBackend&))
{
    SRWBackend candidate;

    for (SRWBackendList::const_iterator it = backends.begin();
         it != backends.end(); it++)
    {
        const SRWBackend& backend = *it;

        if (!backend->in_use() && backend->can_connect() &&
            valid_for_slave(backend, master))
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
            break;

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
 * Get the total number of slaves and number of connected slaves
 *
 * @param backends List of backend servers
 * @param master   The master server or NULL for no master
 *
 * @return the total number of slaves and the connected slave count
 */
std::pair<int, int> get_slave_counts(SRWBackendList& backends, SERVER_REF* master)
{
    int slaves_found = 0;
    int slaves_connected = 0;

    /** Calculate how many connections we already have */
    for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
    {
        const SRWBackend& backend = *it;

        if (backend->can_connect() && valid_for_slave(backend, master))
        {
            slaves_found += 1;

            if (backend->in_use())
            {
                slaves_connected += 1;
            }
        }
    }

    return std::make_pair(slaves_found, slaves_connected);
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
    SERVER_REF *master = get_root_master(backends);

    if (!master && config.master_failure_mode == RW_FAIL_INSTANTLY)
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

    if (!master_connected)
    {
        /** Find a master server */
        for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
        {
            const SRWBackend& backend = *it;

            if (backend->can_connect() && master && backend->server() == master->server)
            {
                if (backend->connect(session))
                {
                    current_master = backend;
                }
                break;
            }
        }
    }

    auto counts = get_slave_counts(backends, master);
    int slaves_found = counts.first;
    int slaves_connected = counts.second;

    ss_dassert(slaves_connected < max_nslaves || max_nslaves == 0);

    /** Connect to all possible slaves */
    for (SRWBackend backend(get_slave_candidate(backends, master, cmpfun));
         backend && slaves_connected < max_nslaves;
         backend = get_slave_candidate(backends, master, cmpfun))
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

    return true;
}
