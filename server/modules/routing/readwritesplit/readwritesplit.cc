/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
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

// TODO: Don't process parameters in readwritesplit
static bool handle_max_slaves(Config& config, const char* str)
{
    bool rval = true;
    char* endptr;
    int val = strtol(str, &endptr, 10);

    if (*endptr == '%' && *(endptr + 1) == '\0')
    {
        config.rw_max_slave_conn_percent = val;
        config.max_slave_connections = 0;
    }
    else if (*endptr == '\0')
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

RWSplit::RWSplit(SERVICE* service, const Config& config)
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

const Config& RWSplit::config() const
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

SrvStatMap& RWSplit::local_server_stats()
{
    return *m_server_stats;
}

maxscale::SrvStatMap RWSplit::all_server_stats() const
{
    SrvStatMap stats;

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
    auto gtid = m_last_gtid.load(std::memory_order_relaxed);
    return std::to_string(gtid.domain) + '-'
           + std::to_string(gtid.server_id) + '-'
           + std::to_string(gtid.sequence);
}

void RWSplit::set_last_gtid(const std::string& str)
{
    static bool warn_malformed_gtid = true;
    auto tokens = mxb::strtok(str, "-");

    if (tokens.size() == 3)
    {
        gtid gtid;
        gtid.domain = strtol(tokens[0].c_str(), nullptr, 10);
        gtid.server_id = strtol(tokens[1].c_str(), nullptr, 10);
        gtid.sequence = strtol(tokens[2].c_str(), nullptr, 10);

        m_last_gtid.store(gtid, std::memory_order_relaxed);
    }
    else if (warn_malformed_gtid)
    {
        warn_malformed_gtid = false;
        MXS_WARNING("Malformed GTID received: %s", str.c_str());
    }
}

int RWSplit::max_slave_count() const
{
    int router_nservers = m_service->get_children().size();
    int conf_max_nslaves = m_config->max_slave_connections > 0 ?
        m_config->max_slave_connections :
        (router_nservers * m_config->rw_max_slave_conn_percent) / 100;
    return MXS_MAX(1, MXS_MIN(router_nservers, conf_max_nslaves));
}

bool RWSplit::have_enough_servers() const
{
    bool succp = true;
    const int min_nsrv = 1;
    const int router_nsrv = m_service->get_children().size();

    int n_serv = MXS_MAX(m_config->max_slave_connections,
                         (router_nsrv * m_config->rw_max_slave_conn_percent) / 100);

    /** With too few servers session is not created */
    if (router_nsrv < min_nsrv || n_serv < min_nsrv)
    {
        if (router_nsrv < min_nsrv)
        {
            MXS_ERROR("Unable to start %s service. There are "
                      "too few backend servers available. Found %d "
                      "when %d is required.",
                      m_service->name(),
                      router_nsrv,
                      min_nsrv);
        }
        else
        {
            int pct = m_config->rw_max_slave_conn_percent / 100;
            int nservers = router_nsrv * pct;

            if (m_config->max_slave_connections < min_nsrv)
            {
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d when %d is required.",
                          m_service->name(),
                          m_config->max_slave_connections,
                          min_nsrv);
            }
            if (nservers < min_nsrv)
            {
                double dbgpct = ((double)min_nsrv / (double)router_nsrv) * 100.0;
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d%% when at least %.0f%% "
                          "would be required.",
                          m_service->name(),
                          m_config->rw_max_slave_conn_percent,
                          dbgpct);
            }
        }
        succp = false;
    }

    return succp;
}

static void log_router_options_not_supported(SERVICE* service, std::string router_opts)
{
    std::stringstream ss;

    for (const auto& a : mxs::strtok(router_opts, ", \t"))
    {
        ss << a << "\n";
    }

    MXS_ERROR("`router_options` is no longer supported in readwritesplit. "
              "To define the router options as parameters, add the following "
              "lines to service '%s':\n\n%s\n",
              service->name(),
              ss.str().c_str());
}

/**
 * API function definitions
 */


RWSplit* RWSplit::create(SERVICE* service, MXS_CONFIG_PARAMETER* params)
{

    if (params->contains(CN_ROUTER_OPTIONS))
    {
        log_router_options_not_supported(service, params->get_string(CN_ROUTER_OPTIONS));
        return NULL;
    }

    Config config(params);

    if (!handle_max_slaves(config, params->get_string("max_slave_connections").c_str()))
    {
        return NULL;
    }

    if (config.master_reconnection && config.disable_sescmd_history)
    {
        MXS_ERROR("Both 'master_reconnection' and 'disable_sescmd_history' are enabled: "
                  "Master reconnection cannot be done without session command history.");
        return NULL;
    }

    return new(std::nothrow) RWSplit(service, config);
}

RWSplitSession* RWSplit::newSession(MXS_SESSION* session, const Endpoints& endpoints)
{
    RWSplitSession* rses = NULL;
    MXS_EXCEPTION_GUARD(rses = RWSplitSession::create(this, session, endpoints));
    return rses;
}

json_t* RWSplit::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "connections", json_integer(stats().n_sessions));
    json_object_set_new(rval, "current_connections", json_integer(service()->stats().n_current));
    json_object_set_new(rval, "queries", json_integer(stats().n_queries));
    json_object_set_new(rval, "route_master", json_integer(stats().n_master));
    json_object_set_new(rval, "route_slave", json_integer(stats().n_slave));
    json_object_set_new(rval, "route_all", json_integer(stats().n_all));
    json_object_set_new(rval, "rw_transactions", json_integer(stats().n_rw_trx));
    json_object_set_new(rval, "ro_transactions", json_integer(stats().n_ro_trx));
    json_object_set_new(rval, "replayed_transactions", json_integer(stats().n_trx_replay));

    const char* weightby = serviceGetWeightingParameter(service());

    if (*weightby)
    {
        json_object_set_new(rval, "weightby", json_string(weightby));
    }

    json_t* arr = json_array();

    for (const auto& a : all_server_stats())
    {
        ServerStats::CurrentStats stats = a.second.current_stats();

        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.first->name()));
        json_object_set_new(obj, "total", json_integer(stats.total_queries));
        json_object_set_new(obj, "read", json_integer(stats.total_read_queries));
        json_object_set_new(obj, "write", json_integer(stats.total_write_queries));
        json_object_set_new(obj, "avg_sess_duration", json_string(to_string(stats.ave_session_dur).c_str()));
        json_object_set_new(obj, "avg_sess_active_pct", json_real(stats.ave_session_active_pct));
        json_object_set_new(obj, "avg_selects_per_session", json_integer(stats.ave_session_selects));
        json_array_append_new(arr, obj);
    }

    json_object_set_new(rval, "server_query_statistics", arr);

    return rval;
}

constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING | RCAP_TYPE_TRANSACTION_TRACKING
    | RCAP_TYPE_SESSION_STATE_TRACKING | RCAP_TYPE_RUNTIME_CONFIG;

uint64_t RWSplit::getCapabilities()
{
    return CAPABILITIES;
}

bool RWSplit::configure(MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    Config cnf(params);

    if (handle_max_slaves(cnf, params->get_string("max_slave_connections").c_str()))
    {
        m_config.assign(cnf);
        rval = true;
    }

    return rval;
}

/**
 * The module entry point routine. It is this routine that must return
 * the structure that is referred to as the "module object". This is a
 * structure with the set of external entry points for this module.
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char description[] = "A Read/Write splitting router for enhancement read scalability";

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        description,
        "V1.1.0",
        CAPABILITIES,
        &RWSplit::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {
                "use_sql_variables_in",
                MXS_MODULE_PARAM_ENUM,
                "all",
                MXS_MODULE_OPT_NONE,
                use_sql_variables_in_values
            },
            {
                "slave_selection_criteria",
                MXS_MODULE_PARAM_ENUM,
                "LEAST_CURRENT_OPERATIONS",
                MXS_MODULE_OPT_NONE,
                slave_selection_criteria_values
            },
            {
                "master_failure_mode",
                MXS_MODULE_PARAM_ENUM,
                "fail_instantly",
                MXS_MODULE_OPT_NONE,
                master_failure_mode_values
            },
            {
                "causal_reads_mode",
                MXS_MODULE_PARAM_ENUM,
                "local",
                MXS_MODULE_OPT_NONE,
                causal_reads_mode_values
            },
            {"max_slave_replication_lag",  MXS_MODULE_PARAM_DURATION,  "0s",  MXS_MODULE_OPT_DURATION_S},
            {"max_slave_connections",      MXS_MODULE_PARAM_STRING,    MAX_SLAVE_COUNT},
            {"slave_connections",          MXS_MODULE_PARAM_INT,       MAX_SLAVE_COUNT},
            {"retry_failed_reads",         MXS_MODULE_PARAM_BOOL,      "true"},
            {"prune_sescmd_history",       MXS_MODULE_PARAM_BOOL,      "false"},
            {"disable_sescmd_history",     MXS_MODULE_PARAM_BOOL,      "false"},
            {"max_sescmd_history",         MXS_MODULE_PARAM_COUNT,     "50"},
            {"strict_multi_stmt",          MXS_MODULE_PARAM_BOOL,      "false"},
            {"strict_sp_calls",            MXS_MODULE_PARAM_BOOL,      "false"},
            {"master_accept_reads",        MXS_MODULE_PARAM_BOOL,      "false"},
            {"causal_reads",               MXS_MODULE_PARAM_BOOL,      "false"},
            {"causal_reads_timeout",       MXS_MODULE_PARAM_DURATION,  "10s", MXS_MODULE_OPT_DURATION_S},
            {"master_reconnection",        MXS_MODULE_PARAM_BOOL,      "false"},
            {"delayed_retry",              MXS_MODULE_PARAM_BOOL,      "false"},
            {"delayed_retry_timeout",      MXS_MODULE_PARAM_DURATION,  "10s", MXS_MODULE_OPT_DURATION_S},
            {"transaction_replay",         MXS_MODULE_PARAM_BOOL,      "false"},
            {"transaction_replay_max_size",MXS_MODULE_PARAM_SIZE,      "1Mi"},
            {"transaction_replay_attempts",MXS_MODULE_PARAM_COUNT,     "5"},
            {"optimistic_trx",             MXS_MODULE_PARAM_BOOL,      "false"},
            {"lazy_connect",               MXS_MODULE_PARAM_BOOL,      "false"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}
