/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cmath>
#include <new>
#include <sstream>

#include <maxbase/alloc.h>
#include <maxscale/cn_strings.hh>
#include <maxscale/dcb.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/router.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/routingworker.hh>

#include "rwsplitsession.hh"

using namespace maxscale;

/**
 * The entry points for the read/write query splitting router module.
 *
 * This file contains the entry points that comprise the API to the read
 * write query splitting router. It also contains functions that are
 * directly called by the entry point functions. Some of these are used by
 * functions in other modules of the read write split router, others are
 * used only within this module.
 */

/** Maximum number of slaves */
#define MAX_SLAVE_COUNT "255"

bool RWSplit::check_causal_reads(SERVER* server) const
{
    auto var = server->get_variable("session_track_system_variables");
    return var.empty() || var == "*" || var.find("last_gtid") != std::string::npos;
}

void RWSplit::set_warnings(json_t* json) const
{
    std::vector<std::string> warnings;

    for (const auto& s : m_pService->reachable_servers())
    {
        if (!check_causal_reads(s))
        {
            std::stringstream ss;
            ss << "`causal_reads` is not supported on server '" << s->name()
               << "': session_track_system_variables does not contain last_gtid";
            warnings.push_back(ss.str());
        }
    }

    if (!warnings.empty())
    {
        json_t* warnings_json = json_array();

        for (const auto& w : warnings)
        {
            json_array_append_new(warnings_json, json_string(w.c_str()));
        }

        json_object_set_new(json, "warnings", warnings_json);
    }
}

RWSplit::RWSplit(SERVICE* service)
    : mxs::Router<RWSplit, RWSplitSession>(service)
    , m_service(service)
    , m_config(service->name())
{
}

RWSplit::~RWSplit()
{
}

SERVICE* RWSplit::service() const
{
    return m_service;
}

const RWSConfig::Values& RWSplit::config() const
{
    return m_config.values();
}

Stats& RWSplit::stats()
{
    return m_stats;
}

const Stats& RWSplit::stats() const
{
    return m_stats;
}

TargetSessionStats& RWSplit::local_server_stats()
{
    return *m_server_stats;
}

maxscale::TargetSessionStats RWSplit::all_server_stats() const
{
    TargetSessionStats stats;

    for (const auto& a : m_server_stats.values())
    {
        for (const auto& b : a)
        {
            if (b.first->active())
            {
                stats[b.first] += b.second;
            }
        }
    }

    return stats;
}

std::string RWSplit::last_gtid() const
{
    mxb::shared_lock<mxb::shared_mutex> guard(m_last_gtid_lock);
    return m_last_gtid.to_string();
}

void RWSplit::set_last_gtid(const std::string& str)
{
    std::lock_guard<mxb::shared_mutex> guard(m_last_gtid_lock);
    m_last_gtid = gtid::from_string(str);
}

// static
RWSplit::gtid RWSplit::gtid::from_string(const std::string& str)
{
    gtid g;
    const char* ptr = str.c_str();
    char* end;
    g.domain = strtoul(ptr, &end, 10);
    mxb_assert(*end == '-');
    ptr = end + 1;
    g.server_id = strtoul(ptr, &end, 10);
    mxb_assert(*end == '-');
    ptr = end + 1;
    g.sequence = strtoul(ptr, &end, 10);
    mxb_assert(*end == '\0');
    return g;
}

std::string RWSplit::gtid::to_string() const
{
    return std::to_string(domain) + '-' + std::to_string(server_id) + '-' + std::to_string(sequence);
}

bool RWSplit::gtid::empty() const
{
    return domain == 0 && server_id == 0 && sequence == 0;
}

RWSConfig::RWSConfig(const char* name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&RWSConfig::m_v, &Values::slave_selection_criteria, &s_slave_selection_criteria);
    add_native(&RWSConfig::m_v, &Values::use_sql_variables_in, &s_use_sql_variables_in);
    add_native(&RWSConfig::m_v, &Values::master_failure_mode, &s_master_failure_mode);
    add_native(&RWSConfig::m_v, &Values::max_sescmd_history, &s_max_sescmd_history);
    add_native(&RWSConfig::m_v, &Values::prune_sescmd_history, &s_prune_sescmd_history);
    add_native(&RWSConfig::m_v, &Values::disable_sescmd_history, &s_disable_sescmd_history);
    add_native(&RWSConfig::m_v, &Values::master_accept_reads, &s_master_accept_reads);
    add_native(&RWSConfig::m_v, &Values::strict_multi_stmt, &s_strict_multi_stmt);
    add_native(&RWSConfig::m_v, &Values::strict_sp_calls, &s_strict_sp_calls);
    add_native(&RWSConfig::m_v, &Values::retry_failed_reads, &s_retry_failed_reads);
    add_native(&RWSConfig::m_v, &Values::max_slave_replication_lag, &s_max_slave_replication_lag);
    add_native(&RWSConfig::m_v, &Values::max_slave_connections, &s_max_slave_connections);
    add_native(&RWSConfig::m_v, &Values::slave_connections, &s_slave_connections);
    add_native(&RWSConfig::m_v, &Values::causal_reads, &s_causal_reads);
    add_native(&RWSConfig::m_v, &Values::causal_reads_timeout, &s_causal_reads_timeout);
    add_native(&RWSConfig::m_v, &Values::master_reconnection, &s_master_reconnection);
    add_native(&RWSConfig::m_v, &Values::delayed_retry, &s_delayed_retry);
    add_native(&RWSConfig::m_v, &Values::delayed_retry_timeout, &s_delayed_retry_timeout);
    add_native(&RWSConfig::m_v, &Values::transaction_replay, &s_transaction_replay);
    add_native(&RWSConfig::m_v, &Values::trx_max_size, &s_transaction_replay_max_size);
    add_native(&RWSConfig::m_v, &Values::trx_max_attempts, &s_transaction_replay_attempts);
    add_native(&RWSConfig::m_v, &Values::trx_retry_on_deadlock, &s_transaction_replay_retry_on_deadlock);
    add_native(&RWSConfig::m_v, &Values::optimistic_trx, &s_optimistic_trx);
    add_native(&RWSConfig::m_v, &Values::lazy_connect, &s_lazy_connect);
}

bool RWSConfig::post_configure()
{
    m_v.backend_select_fct = get_backend_select_function(m_v.slave_selection_criteria);

    if (m_v.causal_reads != CausalReads::NONE)
    {
        m_v.retry_failed_reads = true;
    }

    /** These options cancel each other out */
    if (m_v.disable_sescmd_history && m_v.max_sescmd_history > 0)
    {
        m_v.max_sescmd_history = 0;
    }

    if (m_v.optimistic_trx)
    {
        // Optimistic transaction routing requires transaction replay
        m_v.transaction_replay = true;
    }

    if (m_v.transaction_replay || m_v.lazy_connect)
    {
        /**
         * Replaying transactions requires that we are able to do delayed query
         * retries. Both transaction replay and lazy connection creation require
         * fail-on-write failure mode and reconnections to masters.
         */
        if (m_v.transaction_replay)
        {
            m_v.delayed_retry = true;
        }
        m_v.master_reconnection = true;
        m_v.master_failure_mode = RW_FAIL_ON_WRITE;
    }

    bool rval = true;

    if (m_v.master_reconnection && m_v.disable_sescmd_history)
    {
        MXS_ERROR("Both 'master_reconnection' and 'disable_sescmd_history' are enabled: "
                  "Master reconnection cannot be done without session command history.");
        rval = false;
    }
    else
    {
        // Configuration is OK, assign it to the shared value
        m_values.assign(m_v);
    }

    return rval;
}

/**
 * API function definitions
 */

RWSplit* RWSplit::create(SERVICE* service, mxs::ConfigParameters* params)
{
    return new RWSplit(service);
}

mxs::RouterSession* RWSplit::newSession(MXS_SESSION* session, const Endpoints& endpoints)
{
    return RWSplitSession::create(this, session, endpoints);
}

json_t* RWSplit::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "queries", json_integer(stats().n_queries));
    json_object_set_new(rval, "route_master", json_integer(stats().n_master));
    json_object_set_new(rval, "route_slave", json_integer(stats().n_slave));
    json_object_set_new(rval, "route_all", json_integer(stats().n_all));
    json_object_set_new(rval, "rw_transactions", json_integer(stats().n_rw_trx));
    json_object_set_new(rval, "ro_transactions", json_integer(stats().n_ro_trx));
    json_object_set_new(rval, "replayed_transactions", json_integer(stats().n_trx_replay));

    json_t* arr = json_array();

    for (const auto& a : all_server_stats())
    {
        SessionStats::CurrentStats stats = a.second.current_stats();

        double active_pct = std::round(100 * stats.ave_session_active_pct) / 100;

        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.first->name()));
        json_object_set_new(obj, "total", json_integer(stats.total_queries));
        json_object_set_new(obj, "read", json_integer(stats.total_read_queries));
        json_object_set_new(obj, "write", json_integer(stats.total_write_queries));
        json_object_set_new(obj, "avg_sess_duration",
                            json_string(mxb::to_string(stats.ave_session_dur).c_str()));
        json_object_set_new(obj, "avg_sess_active_pct", json_real(active_pct));
        json_object_set_new(obj, "avg_selects_per_session", json_integer(stats.ave_session_selects));
        json_array_append_new(arr, obj);
    }

    json_object_set_new(rval, "server_query_statistics", arr);

    if (config().causal_reads != CausalReads::NONE)
    {
        set_warnings(rval);
    }

    return rval;
}

constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_TRANSACTION_TRACKING
    | RCAP_TYPE_SESSION_STATE_TRACKING | RCAP_TYPE_RUNTIME_CONFIG;

uint64_t RWSplit::getCapabilities() const
{
    return CAPABILITIES;
}

bool RWSplit::configure(mxs::ConfigParameters* params)
{
    return false;
}

/**
 * The module entry point routine. It is this routine that must return
 * the structure that is referred to as the "module object". This is a
 * structure with the set of external entry points for this module.
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "A Read/Write splitting router for enhancement read scalability",
        "V1.1.0",
        CAPABILITIES,
        &RWSplit::s_object,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        &s_spec
    };

    return &info;
}
