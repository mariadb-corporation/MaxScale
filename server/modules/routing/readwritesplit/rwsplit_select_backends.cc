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
#include <functional>
#include <random>
#include <iostream>

#include <maxbase/stopwatch.hh>
#include <maxscale/router.h>

using namespace maxscale;

// TODO, there should be a utility with the most common used random cases.
// FYI: rand() is about twice as fast as the below toss.
static std::mt19937 random_engine;
static std::uniform_real_distribution<> zero_to_one(0.0, 1.0);
double toss()
{
    return zero_to_one(random_engine);
}

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
    return (backend->is_slave() || backend->is_relay())
           && (!master || backend != master);
}

SRWBackendVector::iterator best_score(SRWBackendVector& sBackends,
                                      std::function<double(SERVER_REF* server)> server_score)
{
    double min {std::numeric_limits<double>::max()};
    auto best = sBackends.end();
    for (auto ite = sBackends.begin(); ite != sBackends.end(); ++ite)
    {
        double score = server_score((***ite).backend());
        if (min > score)
        {
            min = score;
            best = ite;
        }
    }

    return best;
}

/** Compare number of connections from this router in backend servers */
SRWBackendVector::iterator backend_cmp_router_conn(SRWBackendVector& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->inv_weight * server->connections;
        };

    return best_score(sBackends, server_score);
}

/** Compare number of global connections in backend servers */
SRWBackendVector::iterator backend_cmp_global_conn(SRWBackendVector& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->inv_weight * server->server->stats.n_current;
        };

    return best_score(sBackends, server_score);
}

/** Compare replication lag between backend servers */
SRWBackendVector::iterator backend_cmp_behind_master(SRWBackendVector& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->inv_weight * server->server->rlag;
        };

    return best_score(sBackends, server_score);
}

/** Compare number of current operations in backend servers */
SRWBackendVector::iterator backend_cmp_current_load(SRWBackendVector& sBackends)
{
    static auto server_score = [](SERVER_REF* server) {
            return server->inv_weight * server->server->stats.n_current_ops;
        };

    return best_score(sBackends, server_score);
}

SRWBackendVector::iterator backend_cmp_response_time(SRWBackendVector& sBackends)
{
    const int SZ = sBackends.size();
    double slot[SZ];

    // fill slots with inverses of averages
    double total {0};
    for (int i = 0; i < SZ; ++i)
    {
        SERVER_REF* server = (**sBackends[i]).backend();
        auto ave = server_response_time_average(server->server);

        if (ave==0)
        {
            constexpr double very_quick = 1.0 / 10000000;   // arbitrary very short duration (0.1
                                                            // microseconds)
            slot[i] = 1 / very_quick;                       // will be used and updated (almost) immediately.
        }
        else
        {
            slot[i] = 1 / ave;
        }
        slot[i] = slot[i] * slot[i];    // favor faster servers even more
        total += slot[i];
    }

    // turn slots into a roulette wheel, where sum of slots is 1.0
    for (int i = 0; i < SZ; ++i)
    {
        slot[i] = slot[i] / total;
    }

    // Find the winner, role the ball:
    double ball = toss();
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

    case LOWEST_RESPONSE_TIME:
        return backend_cmp_response_time;
    }

    assert(false && "incorrect use of select_criteria_t");
    return backend_cmp_current_load;
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
SRWBackendVector::iterator find_best_backend(SRWBackendVector& backends,
                                             BackendSelectFunction select,
                                             bool masters_accepts_reads)
{
    // Group backends by priority. The set of highest priority backends will then compete.
    std::map<int, SRWBackendVector> priority_map;
    int best_priority {INT_MAX};    // low numbers are high priority

    for (auto& psBackend : backends)
    {
        auto& backend = **psBackend;
        bool is_busy = backend.in_use() && backend.has_session_commands();
        bool acts_slave = backend.is_slave() || (backend.is_master() && masters_accepts_reads);

        int priority;
        if (acts_slave)
        {
            if (!is_busy)
            {
                priority = 1;   // highest priority, idle servers
            }
            else
            {
                priority = 13;      // lowest priority, busy servers
            }
        }
        else
        {
            priority = 2;   // idle masters with masters_accept_reads==false
        }

        priority_map[priority].push_back(psBackend);
        best_priority = std::min(best_priority, priority);
    }

    auto best = select(priority_map[best_priority]);

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
                     b->server->stats.n_current,
                     b->server->address,
                     b->server->port,
                     STRSRVSTATUS(b->server));
            break;

        case LEAST_ROUTER_CONNECTIONS:
            MXS_INFO("RWSplit connections : %d in \t[%s]:%d %s",
                     b->connections,
                     b->server->address,
                     b->server->port,
                     STRSRVSTATUS(b->server));
            break;

        case LEAST_CURRENT_OPERATIONS:
            MXS_INFO("current operations : %d in \t[%s]:%d %s",
                     b->server->stats.n_current_ops,
                     b->server->address,
                     b->server->port,
                     STRSRVSTATUS(b->server));
            break;

        case LEAST_BEHIND_MASTER:
            MXS_INFO("replication lag : %d in \t[%s]:%d %s",
                     b->server->rlag,
                     b->server->address,
                     b->server->port,
                     STRSRVSTATUS(b->server));
            break;

        case LOWEST_RESPONSE_TIME:
            {
                maxbase::Duration response_ave(server_response_time_average(b->server));
                std::ostringstream os;
                os << response_ave;
                MXS_INFO("Average response time : %s from \t[%s]:%d %s",
                         os.str().c_str(),
                         b->server->address,
                         b->server->port,
                         STRSRVSTATUS(b->server));
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
bool RWSplit::select_connect_backend_servers(MXS_SESSION* session,
                                             SRWBackendList& backends,
                                             SRWBackend& current_master,
                                             SessionCommandList* sescmd_list,
                                             int* expected_responses,
                                             connection_type type)
{
    SRWBackend master = get_root_master(backends);
    const Config& cnf {config()};

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

    SRWBackendVector candidates;
    for (auto& sBackend : backends)
    {
        if (!sBackend->in_use()
            && sBackend->can_connect()
            && valid_for_slave(sBackend, master))
        {
            candidates.push_back(&sBackend);
        }
    }

    while (slaves_connected < max_nslaves && candidates.size())
    {
        auto ite = m_config->backend_select_fct(candidates);
        if (ite == candidates.end())
        {
            break;
        }

        auto& backend = **ite;

        if (backend->connect(session, sescmd_list))
        {
            MXS_INFO("Selected Slave: %s", backend->name());

            if (sescmd_list && sescmd_list->size() && expected_responses)
            {
                (*expected_responses)++;
            }

            ++slaves_connected;
        }
        candidates.erase(ite);
    }
    return true;
}

/** Documenting ideas and tests. This will be removed before merging to develop.
 * The strategi with least opearations performs very well.
 * Lowest response time (should rename to fastest server) beats all other methods
 * but least operations comes near. There are probably a whole set of rules for adaptive
 * load balancing. For example,
 * 1. If there is low traffic (few operations), pick the fastest machine. But due to its nature
 *    other machines need to be tested once in awhile.
 * 2. Favour the fast machines more than the distribution would suggest. Squaring the normalized
 *    fitness (goodness) is clearly right, but maybe not the optimal choise.
 * 3. The parameters of EMAverage do not seem to weigh in as much as my intuition suggested.
 *    The tests with machines with very different speeds still give great results.
 *    The important thing is that it responds fast enough to changes in speed, some of which is
 *    caused by the load balancing itself (favoring a fast machine makes it slower).
 * 4. My tests have used a single and simple sql query. In the face of very divergent queries,
 *    maybe a standard query could be used to asses a servers speed, initially and once in a while,
 *    if traffic slows down.
 * 5. Alternatively to 4), the EMAverage could take the time between queries into account, so that
 *    it converges faster to new measurments if they are far apart in time. I have a feeling
 *    this could make a real difference.
 * 5. It might make sense to do a little math to see how to best use slower machines. It is clear
 *    some queries should be offloaded to them when volume is high, and the random method I use
 *    could be made better.
 * 6. Another idea is to favor faster machines even more, but at the same time increase the rating
 *    of slower machines as time goes by. In that way slower machines are not used unecessarily,
 *    but in time they still get some traffic, which might show them to now be faster, or immediately
 *    be downgraded again.
 * 7. Canonicals could be used, but I don't really see how...
 * 8. are all those preconditions needed (like rlag)
 */
