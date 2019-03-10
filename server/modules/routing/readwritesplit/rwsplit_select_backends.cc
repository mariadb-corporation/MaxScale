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
#include "rwsplitsession.hh"

#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sstream>
#include <functional>
#include <random>
#include <iostream>
#include <array>

#include <maxbase/stopwatch.hh>
#include <maxscale/router.hh>

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
static bool valid_for_slave(const RWBackend* backend, const RWBackend* master)
{
    return (backend->is_slave() || backend->is_relay())
           && (!master || backend != master);
}

PRWBackends::iterator best_score(PRWBackends& sBackends,
                                 std::function<double(SERVER_REF* server)> server_score)
{
    double min {std::numeric_limits<double>::max()};
    auto best = sBackends.end();
    for (auto ite = sBackends.begin(); ite != sBackends.end(); ++ite)
    {
        double score = server_score((**ite).backend());

        if (!(*ite)->in_use())
        {
            // To prefer servers that we are connected to, inflate the score of unconnected servers
            score = (score + 5.0) * 1.5;
        }

        if (min > score)
        {
            min = score;
            best = ite;
        }
    }

    return best;
}

/** Compare number of connections from this router in backend servers */
PRWBackends::iterator backend_cmp_router_conn(PRWBackends& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->server_weight ? (server->connections + 1) / server->server_weight :
                   std::numeric_limits<double>::max();
        };

    return best_score(sBackends, server_score);
}

/** Compare number of global connections in backend servers */
PRWBackends::iterator backend_cmp_global_conn(PRWBackends& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->server_weight ? (server->server->stats.n_current + 1) / server->server_weight :
                   std::numeric_limits<double>::max();
        };

    return best_score(sBackends, server_score);
}

/** Compare replication lag between backend servers */
PRWBackends::iterator backend_cmp_behind_master(PRWBackends& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->server_weight ? server->server->rlag / server->server_weight :
                   std::numeric_limits<double>::max();
        };

    return best_score(sBackends, server_score);
}

/** Compare number of current operations in backend servers */
PRWBackends::iterator backend_cmp_current_load(PRWBackends& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->server_weight ? (server->server->stats.n_current_ops + 1) / server->server_weight :
                   std::numeric_limits<double>::max();
        };

    return best_score(sBackends, server_score);
}

PRWBackends::iterator backend_cmp_response_time(PRWBackends& sBackends)
{
    const int SZ = sBackends.size();
    double slot[SZ];

    // fill slots with inverses of averages
    double pre_total {0};
    for (int i = 0; i < SZ; ++i)
    {
        SERVER_REF* server = (*sBackends[i]).backend();
        double ave = server->server->response_time_average();

        if (ave == 0)
        {
            constexpr double very_quick = 1.0 / 10000000;   // arbitrary very short duration (0.1 us)
            slot[i] = 1 / very_quick;                       // will be used and updated (almost) immediately.
        }
        else
        {
            slot[i] = 1 / ave;
        }
        slot[i] = slot[i] * slot[i] * slot[i];      // favor faster servers even more
        pre_total += slot[i];
    }

    // make the slowest server(s) at least a good fraction of the total to guarantee
    // some amount of sampling, should the slow server become faster.
    double total = 0;
    constexpr int divisor = 197;    // ~0.5%, not exact because SZ>1
    for (int i = 0; i < SZ; ++i)
    {
        slot[i] = std::max(slot[i], pre_total / divisor);
        total += slot[i];
    }

    // turn slots into a roulette wheel, where sum of slots is 1.0
    for (int i = 0; i < SZ; ++i)
    {
        slot[i] = slot[i] / total;
    }

    // Find the winner, role the ball:
    double ball = maxbase::Worker::get_current()->random_engine().zero_to_one_exclusive();

    double slot_walk {0};
    int winner {0};

    for (; winner < SZ; ++winner)
    {
        slot_walk += slot[winner];
        if (ball < slot_walk)
        {
            break;
        }
    }

    return sBackends.begin() + winner;
}

BackendSelectFunction get_backend_select_function(select_criteria_t sc)
{
    switch (sc)
    {
    case LEAST_GLOBAL_CONNECTIONS:
        return backend_cmp_global_conn;

    case LEAST_ROUTER_CONNECTIONS:
        return backend_cmp_router_conn;

    case LEAST_BEHIND_MASTER:
        return backend_cmp_behind_master;

    case LEAST_CURRENT_OPERATIONS:
        return backend_cmp_current_load;

    case ADAPTIVE_ROUTING:
        return backend_cmp_response_time;
    }

    assert(false && "incorrect use of select_criteria_t");
    return backend_cmp_current_load;
}

// Calculates server priority
int get_backend_priority(RWBackend* backend, bool masters_accepts_reads)
{
    int priority;
    bool is_busy = backend->in_use() && backend->has_session_commands();
    bool acts_slave = backend->is_slave() || (backend->is_master() && masters_accepts_reads);

    if (acts_slave)
    {
        if (!is_busy)
        {
            priority = 0;   // highest priority, idle servers
        }
        else
        {
            priority = 2;   // lowest priority, busy servers
        }
    }
    else
    {
        priority = 1;   // idle masters with masters_accept_reads==false
    }

    return priority;
}

/**
 * @brief Find the best slave candidate for routing reads.
 *
 * @param backends All backends
 * @param select   Server selection function
 * @param masters_accepts_reads
 *
 * @return iterator to the best slave or backends.end() if none found
 */
PRWBackends::iterator find_best_backend(PRWBackends& backends,
                                        BackendSelectFunction select,
                                        bool masters_accepts_reads)
{
    // Group backends by priority and rank (lower is better). The set of best backends will then compete.
    int best_priority {INT_MAX};
    int best_rank {std::numeric_limits<int>::max()};
    thread_local PRWBackends candidates;
    candidates.clear();

    for (auto& psBackend : backends)
    {
        int priority = get_backend_priority(psBackend, masters_accepts_reads);
        int rank = psBackend->server()->rank();

        if (rank < best_rank || (rank == best_rank && priority < best_priority))
        {
            candidates.clear();
            best_rank = rank;
            best_priority = priority;
        }

        if (rank == best_rank && priority == best_priority)
        {
            candidates.push_back(psBackend);
        }
    }

    auto best = select(candidates);
    auto rval = std::find(backends.begin(), backends.end(), *best);

    return rval;
}

void add_backend_with_rank(RWBackend* backend, PRWBackends* candidates, int* best_rank)
{
    int rank = backend->server()->rank();

    if (rank < *best_rank)
    {
        *best_rank = rank;
        candidates->clear();
    }

    if (rank == *best_rank)
    {
        candidates->push_back(backend);
    }
}

/**
 * @brief Log server connections
 *
 * @param criteria Slave selection criteria
 * @param rses     Router client session
 */
static void log_server_connections(select_criteria_t criteria, const PRWBackends& backends)
{
    MXS_INFO("Servers and %s connection counts:",
             criteria == LEAST_GLOBAL_CONNECTIONS ? "all MaxScale" : "router");

    for (PRWBackends::const_iterator it = backends.begin(); it != backends.end(); it++)
    {
        SERVER_REF* b = (*it)->backend();

        switch (criteria)
        {
        case LEAST_GLOBAL_CONNECTIONS:
            MXS_INFO("MaxScale connections : %d in \t[%s]:%d %s",
                     b->server->stats.n_current,
                     b->server->address,
                     b->server->port,
                     b->server->status_string().c_str());
            break;

        case LEAST_ROUTER_CONNECTIONS:
            MXS_INFO("RWSplit connections : %d in \t[%s]:%d %s",
                     b->connections,
                     b->server->address,
                     b->server->port,
                     b->server->status_string().c_str());
            break;

        case LEAST_CURRENT_OPERATIONS:
            MXS_INFO("current operations : %d in \t[%s]:%d %s",
                     b->server->stats.n_current_ops,
                     b->server->address,
                     b->server->port,
                     b->server->status_string().c_str());
            break;

        case LEAST_BEHIND_MASTER:
            MXS_INFO("replication lag : %d in \t[%s]:%d %s",
                     b->server->rlag,
                     b->server->address,
                     b->server->port,
                     b->server->status_string().c_str());
            break;

        case ADAPTIVE_ROUTING:
            {
                maxbase::Duration response_ave(b->server->response_time_average());
                std::ostringstream os;
                os << response_ave;
                MXS_INFO("adaptive avg. select time: %s from \t[%s]:%d %s",
                         os.str().c_str(),
                         b->server->address,
                         b->server->port,
                         b->server->status_string().c_str());
            }
            break;

        default:
            mxb_assert(!true);
            break;
        }
    }
}

RWBackend* get_root_master(const PRWBackends& backends, RWBackend* current_master,
                           const BackendSelectFunction& func)
{
    if (current_master && current_master->in_use() && current_master->is_master())
    {
        return current_master;
    }

    thread_local PRWBackends candidates;
    candidates.clear();
    int best_rank {std::numeric_limits<int>::max()};

    for (const auto& backend : backends)
    {
        if (backend->can_connect() && backend->is_master())
        {
            add_backend_with_rank(backend, &candidates, &best_rank);
        }
    }

    auto it = func(candidates);
    return it != candidates.end() ? *it : nullptr;
}

std::pair<int, int> get_slave_counts(PRWBackends& backends, RWBackend* master)
{
    int slaves_found = 0;
    int slaves_connected = 0;

    /** Calculate how many connections we already have */
    for (PRWBackends::const_iterator it = backends.begin(); it != backends.end(); it++)
    {
        const RWBackend* backend = *it;

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
 * @return True if session can continue
 */
bool RWSplitSession::open_connections()
{
    if (m_config.lazy_connect)
    {
        return true;    // No need to create connections
    }

    RWBackend* master = get_root_master(m_raw_backends, m_current_master, m_config.backend_select_fct);

    if ((!master || !master->can_connect()) && m_config.master_failure_mode == RW_FAIL_INSTANTLY)
    {
        if (!master)
        {
            MXS_ERROR("Couldn't find suitable Master from %lu candidates.", m_raw_backends.size());
        }
        else
        {
            MXS_ERROR("Master exists (%s), but it is being drained and cannot be used.",
                      master->server()->address);
        }
        return false;
    }

    if (mxs_log_is_priority_enabled(LOG_INFO))
    {
        log_server_connections(m_config.slave_selection_criteria, m_raw_backends);
    }

    if (can_recover_servers())
    {
        // A master connection can be safely attempted
        if (master && !master->in_use() && master->can_connect() && prepare_connection(master))
        {
            MXS_INFO("Selected Master: %s", master->name());
            m_current_master = master;
        }
    }

    int n_slaves = get_slave_counts(m_raw_backends, master).second;
    int max_nslaves = m_router->max_slave_count();
    int best_rank {std::numeric_limits<int>::max()};
    PRWBackends candidates;
    mxb_assert(n_slaves <= max_nslaves || max_nslaves == 0);

    for (auto& pBackend : m_raw_backends)
    {
        if (!pBackend->in_use() && pBackend->can_connect() && valid_for_slave(pBackend, master))
        {
            add_backend_with_rank(pBackend, &candidates, &best_rank);
        }
    }

    for (auto ite = m_config.backend_select_fct(candidates);
         n_slaves < max_nslaves && !candidates.empty() && ite != candidates.end();
         ite = m_config.backend_select_fct(candidates))
    {
        if (prepare_connection(*ite))
        {
            MXS_INFO("Selected Slave: %s", (*ite)->name());
            ++n_slaves;
        }

        candidates.erase(ite);
    }

    return true;
}
