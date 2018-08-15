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

#include "readwritesplit.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.h>

using namespace maxscale;

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
static bool valid_for_slave(const SRWBackend& backend, const SRWBackend& master)
{
    return (backend->is_slave() || backend->is_relay()) &&
           (!master || backend != master);
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
static SRWBackend get_slave_candidate(const SRWBackendList& backends, const SRWBackend& master,
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
                     b->server->stats.n_current, b->server->address,
                     b->server->port, STRSRVSTATUS(b->server));
            break;

        case LEAST_ROUTER_CONNECTIONS:
            MXS_INFO("RWSplit connections : %d in \t[%s]:%d %s",
                     b->connections, b->server->address,
                     b->server->port, STRSRVSTATUS(b->server));
            break;

        case LEAST_CURRENT_OPERATIONS:
            MXS_INFO("current operations : %d in \t[%s]:%d %s",
                     b->server->stats.n_current_ops,
                     b->server->address, b->server->port,
                     STRSRVSTATUS(b->server));
            break;

        case LEAST_BEHIND_MASTER:
            MXS_INFO("replication lag : %d in \t[%s]:%d %s",
                     b->server->rlag, b->server->address,
                     b->server->port, STRSRVSTATUS(b->server));
            break;

        default:
            ss_dassert(!true);
            break;
        }
    }
}

SRWBackend get_root_master(const SRWBackendList& backends)
{
    SRWBackend master;
    for (auto candidate : backends)
    {
        if (candidate->is_master())
        {
            master = candidate;
            break;
        }
    }

    return master;
}

std::pair<int, int> get_slave_counts(SRWBackendList& backends, SRWBackend& master)
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
 * Select and connect to backend servers
 *
 * @param inst               Router instance
 * @param session            Client session
 * @param backends           List of backend servers
 * @param current_master     The current master server
 * @param sescmd_list        List of session commands to execute
 * @param expected_responses Pointer where number of expected responses are written
 * @param type               Connection type, ALL for all types, SLAVE for slaves only
 *
 * @return True if session can continue
 */
bool RWSplit::select_connect_backend_servers(MXS_SESSION *session,
                                             SRWBackendList& backends,
                                             SRWBackend& current_master,
                                             SessionCommandList* sescmd_list,
                                             int* expected_responses,
                                             connection_type type)
{
    SRWBackend master = get_root_master(backends);
    Config cnf(config());

    if (!master && cnf.master_failure_mode == RW_FAIL_INSTANTLY)
    {
        MXS_ERROR("Couldn't find suitable Master from %lu candidates.", backends.size());
        return false;
    }

    /** Check slave selection criteria and set compare function */
    select_criteria_t select_criteria = cnf.slave_selection_criteria;
    auto cmpfun = criteria_cmpfun[select_criteria];
    ss_dassert(cmpfun);

    if (mxs_log_is_priority_enabled(LOG_INFO))
    {
        log_server_connections(select_criteria, backends);
    }

    if (type == ALL)
    {
        /** Find a master server */
        for (SRWBackendList::const_iterator it = backends.begin(); it != backends.end(); it++)
        {
            const SRWBackend& backend = *it;

            if (backend->can_connect() && master && backend == master)
            {
                if (backend->connect(session))
                {
                    MXS_INFO("Selected Master: %s", backend->name());
                    current_master = backend;
                }
                break;
            }
        }
    }

    auto counts = get_slave_counts(backends, master);
    int slaves_connected = counts.second;
    int max_nslaves = max_slave_count();

    ss_dassert(slaves_connected <= max_nslaves || max_nslaves == 0);

    if (slaves_connected < max_nslaves)
    {
        /** Connect to all possible slaves */
        for (SRWBackend backend(get_slave_candidate(backends, master, cmpfun));
             backend && slaves_connected < max_nslaves;
             backend = get_slave_candidate(backends, master, cmpfun))
        {
            if (backend->can_connect() && backend->connect(session, sescmd_list))
            {
                MXS_INFO("Selected Slave: %s", backend->name());

                if (sescmd_list && sescmd_list->size() && expected_responses)
                {
                    (*expected_responses)++;
                }

                slaves_connected++;
            }
        }
    }
    else
    {
        /**
         * We are already connected to all possible slaves. Currently this can
         * only happen if this function is called by handle_error_new_connection
         * and the routing of queued queries created new connections.
         */
    }

    return true;
}
