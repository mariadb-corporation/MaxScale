/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
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
    const double max_score = std::nexttoward(std::numeric_limits<double>::max(), 0.0);
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

        if (score > max_score)
        {
            // Cap values to a maximum value. This guarantees that we choose a server from the set of
            // available candidates.
            score = max_score;
        }

        if (min > score)
        {
            min = score;
            best = ite;
        }
    }

    mxb_assert_message(best != sBackends.end() || sBackends.empty(),
                       "A candidate must be chosen if we have candidates");

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
    double ball = maxbase::Worker::get_current()->random_engine().zero_to_one_co();

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

int64_t RWSplitSession::get_current_rank()
{
    int64_t rv = 1;

    if (m_current_master && m_current_master->in_use())
    {
        rv = m_current_master->server()->rank();
    }
    else
    {
        auto compare = [](RWBackend* a, RWBackend* b) {
                if (a->in_use() != b->in_use())
                {
                    return a->in_use();
                }
                else if (a->can_connect() != b->can_connect())
                {
                    return a->can_connect();
                }
                else
                {
                    return a->server()->rank() < b->server()->rank();
                }
            };
        auto it = std::min_element(m_raw_backends.begin(), m_raw_backends.end(), compare);

        if (it != m_raw_backends.end())
        {
            rv = (*it)->server()->rank();
        }
    }

    return rv;
}

RWBackend* RWSplitSession::get_slave_backend(int max_rlag)
{
    thread_local PRWBackends candidates;
    candidates.clear();

    auto counts = get_slave_counts(m_raw_backends, m_current_master);
    int best_priority {INT_MAX};
    auto current_rank = get_current_rank();
    // Slaves can be taken into use if we need more slave connections
    bool need_slaves = counts.second < m_router->max_slave_count();

    // Create a list of backends valid for read operations
    for (auto& backend : m_raw_backends)
    {
        // We can take the current master back into use even for reads
        bool my_master = backend == m_current_master;
        bool can_take_into_use = !backend->in_use() && can_recover_servers() && backend->can_connect();
        bool master_or_slave = backend->is_master() || backend->is_slave();

        // The server is usable if it's already in use or it can be taken into use and we need either more
        // slaves or a master.
        bool is_usable = backend->in_use() || (can_take_into_use && (need_slaves || my_master));
        bool rlag_ok = rpl_lag_is_ok(backend, max_rlag);
        int priority = get_backend_priority(backend, m_config.master_accept_reads);
        auto rank = backend->server()->rank();

        if (master_or_slave && is_usable && rlag_ok && rank == current_rank)
        {
            if (priority < best_priority)
            {
                candidates.clear();
                best_priority = priority;
            }

            if (priority == best_priority)
            {
                candidates.push_back(backend);
            }
        }

        if (max_rlag != SERVER::RLAG_UNDEFINED)
        {
            auto state = rlag_ok ? SERVER::RLagState::BELOW_LIMIT : SERVER::RLagState::ABOVE_LIMIT;
            backend->change_rlag_state(state, max_rlag);
        }
    }

    // Let the slave selection function pick the best server
    PRWBackends::const_iterator rval = m_config.backend_select_fct(candidates);

    return (rval == candidates.end()) ? nullptr : *rval;
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

RWBackend* get_root_master(const PRWBackends& backends, RWBackend* current_master)
{
    if (current_master && current_master->in_use() && can_continue_using_master(current_master))
    {
        return current_master;
    }

    thread_local PRWBackends candidates;
    candidates.clear();
    auto best_rank = std::numeric_limits<int64_t>::max();

    for (const auto& backend : backends)
    {
        if (backend->can_connect() && backend->is_master())
        {
            auto rank = backend->server()->rank();

            if (rank < best_rank)
            {
                best_rank = rank;
                candidates.clear();
            }

            if (rank == best_rank)
            {
                candidates.push_back(backend);
            }
        }
    }

    auto it = backend_cmp_global_conn(candidates);
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

    RWBackend* master = get_root_master(m_raw_backends, m_current_master);

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
    mxb_assert(n_slaves <= max_nslaves || max_nslaves == 0);
    auto current_rank = get_current_rank();
    thread_local PRWBackends candidates;
    candidates.clear();

    for (auto& backend : m_raw_backends)
    {
        if (!backend->in_use() && backend->can_connect() && valid_for_slave(backend, master)
            && backend->server()->rank() == current_rank
            && rpl_lag_is_ok(backend, get_max_replication_lag()))
        {
            candidates.push_back(backend);
        }
    }

    auto func = backend_cmp_global_conn;

    for (auto ite = func(candidates);
         n_slaves < max_nslaves && !candidates.empty() && ite != candidates.end();
         ite = func(candidates))
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
