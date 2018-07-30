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
#include <sstream>

#include <maxbase/stopwatch.hh>
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


/** nantti. TODO. TEMP, this needs to see all eligible servers at the same time.
 */
static int backend_cmp_response_time(const SRWBackend& a, const SRWBackend& b)
{
    // Minimum average response time for use in selection. Avoids special cases (zero),
    // and new servers immediately get some traffic.
    constexpr double min_average = 100.0/1000000000; // 100 nano seconds

    // Invert the response times.
    double lhs = 1/std::max(min_average, a->backend()->server->response_time->average());
    double rhs = 1/std::max(min_average, b->backend()->server->response_time->average());

    // Clamp values to a range where the slowest is at least some fraction of the speed of the
    // fastest. This allows sampling of slaves that have experienced anomalies. Also, if one
    // slave is really slow compared to another, something is wrong and perhaps we should
    // log something informational.
    constexpr int clamp = 20;
    double fastest = std::max(lhs, rhs);
    lhs = std::max(lhs, fastest / clamp);
    rhs = std::max(rhs, fastest / clamp);

    // If random numbers are too slow to generate, an array of, say 500'000
    // random numbers in the range [0.0, 1.0] could be generated during startup.
    double r = rand() / static_cast<double>(RAND_MAX);
    return (r < (lhs / (lhs + rhs))) ? -1 : 1;
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
    backend_cmp_current_load,
    backend_cmp_response_time
};

// This is still the current compare method. The response-time compare, along with anything
// using weights, have to change to use the whole array at once to be correct. Id est, everything
// will change to use the whole array in the next iteration.
static BackendSPtrVec::const_iterator run_comparison(const BackendSPtrVec& candidates,
                                                     select_criteria_t sc)
{
    if (candidates.empty()) return candidates.end();

    auto best = candidates.begin();

    for (auto rival = std::next(best);
         rival != candidates.end();
         rival = std::next(rival))
    {
        if (criteria_cmpfun[sc](**best, **rival) > 0)
        {
            best = rival;
        }
    }

    return best;
}

/**
 * @brief Find the best slave candidate for a new connection.
 *
 * @param bends  backends
 * @param master the master server
 * @param sc     which select_criteria_t to use
 *
 * @return The best slave backend reference or null if no candidates could be found
 */
static SRWBackend get_slave_candidate(const SRWBackendList& bends,
                                      const SRWBackend& master,
                                      select_criteria_t sc)
{
    // TODO, nantti, see if this and get_slave_backend can be combined to a single function
    BackendSPtrVec backends;
    for (auto& b : bends)  // match intefaces. TODO, should go away in the future.
    {
        backends.push_back(const_cast<SRWBackend*>(&b));
    }
    BackendSPtrVec candidates;

    for (auto& backend : backends)
    {
        if (!(*backend)->in_use()
            && (*backend)->can_connect()
            && valid_for_slave(*backend, master))
        {
            candidates.push_back(backend);
        }
    }

    return !candidates.empty() ? **run_comparison(candidates, sc) : SRWBackend();

}

BackendSPtrVec::const_iterator find_best_backend(const BackendSPtrVec& backends,
                                                 select_criteria_t sc,
                                                 bool masters_accept_reads)
{
    // Divide backends to priorities. The set of highest priority backends will then compete.
    std::map<int, BackendSPtrVec> priority_map;;
    int best_priority {INT_MAX}; // low numbers are high priority

    for (auto& pSBackend : backends)
    {
        auto& backend   = **pSBackend;
        bool is_busy    = backend.in_use() && backend.has_session_commands();
        bool acts_slave = backend.is_slave() || (backend.is_master() && masters_accept_reads);

        int priority;
        if (acts_slave)
        {
            if (!is_busy)
            {
                priority = 1;  // highest priority, idle servers
            }
            else
            {
                priority = 13; // lowest priority, busy servers
            }
        }
        else
        {
            priority = 2;  // idle masters with masters_accept_reads==false
        }

        priority_map[priority].push_back(pSBackend);
        best_priority = std::min(best_priority, priority);
    }

    auto best = run_comparison(priority_map[best_priority], sc);

    return std::find(backends.begin(), backends.end(), *best);
}

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

        case LOWEST_RESPONSE_TIME:
            {
                maxbase::Duration response_ave(b->server->response_time->average());
                std::ostringstream os;
                os << response_ave;
                MXS_INFO("Average response time : %s from \t[%s]:%d %s",
                         os.str().c_str(), b->server->address,
                         b->server->port, STRSRVSTATUS(b->server));
            }
            break;

        default:
            mxb_assert(!true);
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

    auto select_criteria = cnf.slave_selection_criteria;

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

    mxb_assert(slaves_connected <= max_nslaves || max_nslaves == 0);

    if (slaves_connected < max_nslaves)
    {
        /** Connect to all possible slaves */
        for (SRWBackend backend(get_slave_candidate(backends, master, select_criteria));
             backend && slaves_connected < max_nslaves;
             backend = get_slave_candidate(backends, master, select_criteria))
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
