/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
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

// TODO: Remove support for percentage values in 2.6
static bool handle_max_slaves(RWSConfig& config, const char* str)
{
    bool rval = true;
    char* endptr;
    int val = strtol(str, &endptr, 10);

    if (*endptr == '%' && *(endptr + 1) == '\0' && val >= 0)
    {
        config.rw_max_slave_conn_percent = val;
        config.max_slave_connections = 0;
        MXS_WARNING("Use of percentages in 'max_slave_connections' is deprecated");
    }
    else if (*endptr == '\0' && val >= 0)
    {
        config.max_slave_connections = val;
        config.rw_max_slave_conn_percent = 0;
    }
    else
    {
        MXS_ERROR("Invalid value for 'max_slave_connections': %s", str);
        rval = false;
    }

    return rval;
}

bool RWSplit::check_causal_reads(SERVER* server) const
{
    auto var = server->get_session_track_system_variables();
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

RWSplit::RWSplit(SERVICE* service, const RWSConfig& config)
    : mxs::Router<RWSplit, RWSplitSession>(service)
    , m_service(service)
    , m_config(config)
{
}

RWSplit::~RWSplit()
{
}

SERVICE* RWSplit::service() const
{
    return m_service;
}

const RWSConfig& RWSplit::config() const
{
    return m_config;
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
    std::string gtid;
    std::string separator = "";

    for (const auto& g : m_last_gtid)
    {
        gtid += separator + g.second.to_string();
        separator = ",";
    }

    return gtid;
}

void RWSplit::set_last_gtid(const std::string& str)
{
    auto gtid = gtid::from_string(str);
    std::lock_guard<mxb::shared_mutex> guard(m_last_gtid_lock);

    auto& old_gtid = m_last_gtid[gtid.domain];

    if (old_gtid.sequence < gtid.sequence)
    {
        old_gtid = gtid;
    }
}

int RWSplit::max_slave_count() const
{
    int router_nservers = m_service->get_children().size();
    int conf_max_nslaves = m_config->max_slave_connections > 0 ?
        m_config->max_slave_connections :
        (router_nservers * m_config->rw_max_slave_conn_percent) / 100;
    return MXS_MAX(0, MXS_MIN(router_nservers, conf_max_nslaves));
}

bool RWSplit::have_enough_servers() const
{
    return !m_service->get_children().empty();
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

RWSConfig::RWSConfig(const mxs::ConfigParameters& params)
    : slave_selection_criteria(s_slave_selection_criteria.get(params))
    , backend_select_fct(get_backend_select_function(s_slave_selection_criteria.get(params)))
    , use_sql_variables_in(s_use_sql_variables_in.get(params))
    , master_failure_mode(s_master_failure_mode.get(params))
    , max_sescmd_history(s_max_sescmd_history.get(params))
    , prune_sescmd_history(s_prune_sescmd_history.get(params))
    , disable_sescmd_history(s_disable_sescmd_history.get(params))
    , master_accept_reads(s_master_accept_reads.get(params))
    , strict_multi_stmt(s_strict_multi_stmt.get(params))
    , strict_sp_calls(s_strict_sp_calls.get(params))
    , retry_failed_reads(s_retry_failed_reads.get(params))
    , max_slave_replication_lag(s_max_slave_replication_lag.get(params).count())
    , rw_max_slave_conn_percent(0)
    , max_slave_connections(0)
    , slave_connections(s_slave_connections.get(params))
    , causal_reads(s_causal_reads.get(params))
    , causal_reads_timeout(std::to_string(s_causal_reads_timeout.get(params).count()))
    , master_reconnection(s_master_reconnection.get(params))
    , delayed_retry(s_delayed_retry.get(params))
    , delayed_retry_timeout(s_delayed_retry_timeout.get(params).count())
    , transaction_replay(s_transaction_replay.get(params))
    , trx_max_size(s_transaction_replay_max_size.get(params))
    , trx_max_attempts(s_transaction_replay_attempts.get(params))
    , trx_retry_on_deadlock(s_transaction_replay_retry_on_deadlock.get(params))
    , optimistic_trx(s_optimistic_trx.get(params))
    , lazy_connect(s_lazy_connect.get(params))
{
    if (causal_reads != CausalReads::NONE)
    {
        retry_failed_reads = true;
    }

    /** These options cancel each other out */
    if (disable_sescmd_history && max_sescmd_history > 0)
    {
        max_sescmd_history = 0;
    }

    if (optimistic_trx)
    {
        // Optimistic transaction routing requires transaction replay
        transaction_replay = true;
    }

    if (transaction_replay || lazy_connect)
    {
        /**
         * Replaying transactions requires that we are able to do delayed query
         * retries. Both transaction replay and lazy connection creation require
         * fail-on-write failure mode and reconnections to masters.
         */
        if (transaction_replay)
        {
            delayed_retry = true;
        }
        master_reconnection = true;
        master_failure_mode = RW_FAIL_ON_WRITE;
    }
}

// static
std::pair<bool, RWSConfig> RWSConfig::create(const mxs::ConfigParameters& params)
{
    RWSConfig cnf;
    bool rval = false;

    if (s_spec.validate(params))
    {
        cnf = RWSConfig(params);

        if (handle_max_slaves(cnf, params.get_string("max_slave_connections").c_str()))
        {
            if (cnf.master_reconnection && cnf.disable_sescmd_history)
            {
                MXS_ERROR("Both 'master_reconnection' and 'disable_sescmd_history' are enabled: "
                          "Master reconnection cannot be done without session command history.");
            }
            else
            {
                rval = true;
            }
        }
    }

    return {rval, cnf};
}

/**
 * API function definitions
 */

RWSplit* RWSplit::create(SERVICE* service, mxs::ConfigParameters* params)
{
    auto cnf = RWSConfig::create(*params);
    return cnf.first ? new RWSplit(service, cnf.second) : nullptr;
}

RWSplitSession* RWSplit::newSession(MXS_SESSION* session, const Endpoints& endpoints)
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

    if (m_config->causal_reads != CausalReads::NONE)
    {
        set_warnings(rval);
    }

    return rval;
}

constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_TRANSACTION_TRACKING
    | RCAP_TYPE_SESSION_STATE_TRACKING | RCAP_TYPE_RUNTIME_CONFIG;

uint64_t RWSplit::getCapabilities()
{
    return CAPABILITIES;
}

bool RWSplit::configure(mxs::ConfigParameters* params)
{
    auto cnf = RWSConfig::create(*params);

    if (cnf.first)
    {
        m_config.assign(cnf.second);
    }

    return cnf.first;
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
        NULL,
        NULL,
        NULL,
        NULL
    };

    s_spec.populate(info);
    return &info;
}
