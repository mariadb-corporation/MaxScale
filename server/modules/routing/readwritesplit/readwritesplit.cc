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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cmath>
#include <new>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxscale/dcb.hh>
#include <maxscale/log.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/query_classifier.h>
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

ServerStats& RWSplit::server_stats(SERVER* server)
{
    return (*m_server_stats)[server];
}

maxscale::SrvStatMap RWSplit::all_server_stats() const
{
    SrvStatMap stats;

    for (const auto& a : m_server_stats.values())
    {
        for (const auto& b : a)
        {
            if (b.first->is_active)
            {
                stats[b.first] += b.second;
            }
        }
    }

    return stats;
}

int RWSplit::max_slave_count() const
{
    int router_nservers = m_service->n_dbref;
    int conf_max_nslaves = m_config->max_slave_connections > 0 ?
        m_config->max_slave_connections :
        (router_nservers * m_config->rw_max_slave_conn_percent) / 100;
    return MXS_MIN(router_nservers - 1, MXS_MAX(1, conf_max_nslaves));
}

bool RWSplit::have_enough_servers() const
{
    bool succp = true;
    const int min_nsrv = 1;
    const int router_nsrv = m_service->n_dbref;

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
                      m_service->name,
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
                          m_service->name,
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
                          m_service->name,
                          m_config->rw_max_slave_conn_percent,
                          dbgpct);
            }
        }
        succp = false;
    }

    return succp;
}

static void log_router_options_not_supported(SERVICE* service, MXS_CONFIG_PARAMETER* p)
{
    std::stringstream ss;

    for (const auto& a : mxs::strtok(p->value, ", \t"))
    {
        ss << a << "\n";
    }

    MXS_ERROR("`router_options` is no longer supported in readwritesplit. "
              "To define the router options as parameters, add the following "
              "lines to service '%s':\n\n%s\n",
              service->name,
              ss.str().c_str());
}

/**
 * API function definitions
 */


RWSplit* RWSplit::create(SERVICE* service, MXS_CONFIG_PARAMETER* params)
{
    if (MXS_CONFIG_PARAMETER* p = config_get_param(params, CN_ROUTER_OPTIONS))
    {
        log_router_options_not_supported(service, p);
        return NULL;
    }

    Config config(params);

    if (!handle_max_slaves(config, config_get_string(params, "max_slave_connections")))
    {
        return NULL;
    }

    return new(std::nothrow) RWSplit(service, config);
}

RWSplitSession* RWSplit::newSession(MXS_SESSION* session)
{
    RWSplitSession* rses = NULL;
    MXS_EXCEPTION_GUARD(rses = RWSplitSession::create(this, session));
    return rses;
}

void RWSplit::diagnostics(DCB* dcb)
{
    const char* weightby = serviceGetWeightingParameter(service());
    double master_pct = 0.0, slave_pct = 0.0, all_pct = 0.0;
    Config cnf = config();

    dcb_printf(dcb, "\n");
    dcb_printf(dcb,
               "\tuse_sql_variables_in:      %s\n",
               mxs_target_to_str(cnf.use_sql_variables_in));
    dcb_printf(dcb,
               "\tslave_selection_criteria:  %s\n",
               select_criteria_to_str(cnf.slave_selection_criteria));
    dcb_printf(dcb,
               "\tmaster_failure_mode:       %s\n",
               failure_mode_to_str(cnf.master_failure_mode));
    dcb_printf(dcb,
               "\tmax_slave_replication_lag: %d\n",
               cnf.max_slave_replication_lag);
    dcb_printf(dcb,
               "\tretry_failed_reads:        %s\n",
               cnf.retry_failed_reads ? "true" : "false");
    dcb_printf(dcb,
               "\tstrict_multi_stmt:         %s\n",
               cnf.strict_multi_stmt ? "true" : "false");
    dcb_printf(dcb,
               "\tstrict_sp_calls:           %s\n",
               cnf.strict_sp_calls ? "true" : "false");
    dcb_printf(dcb,
               "\tdisable_sescmd_history:    %s\n",
               cnf.disable_sescmd_history ? "true" : "false");
    dcb_printf(dcb,
               "\tmax_sescmd_history:        %lu\n",
               cnf.max_sescmd_history);
    dcb_printf(dcb,
               "\tmaster_accept_reads:       %s\n",
               cnf.master_accept_reads ? "true" : "false");
    dcb_printf(dcb,
               "\tconnection_keepalive:       %d\n",
               cnf.connection_keepalive);
    dcb_printf(dcb,
               "\tcausal_reads:       %s\n",
               cnf.causal_reads ? "true" : "false");
    dcb_printf(dcb,
               "\tcausal_reads_timeout:       %s\n",
               cnf.causal_reads_timeout.c_str());
    dcb_printf(dcb,
               "\tmaster_reconnection:       %s\n",
               cnf.master_reconnection ? "true" : "false");
    dcb_printf(dcb,
               "\tdelayed_retry:        %s\n",
               cnf.delayed_retry ? "true" : "false");
    dcb_printf(dcb,
               "\tdelayed_retry_timeout:       %lu\n",
               cnf.delayed_retry_timeout);

    dcb_printf(dcb, "\n");

    if (stats().n_queries > 0)
    {
        master_pct = ((double)stats().n_master / (double)stats().n_queries) * 100.0;
        slave_pct = ((double)stats().n_slave / (double)stats().n_queries) * 100.0;
        all_pct = ((double)stats().n_all / (double)stats().n_queries) * 100.0;
    }

    dcb_printf(dcb,
               "\tNumber of router sessions:              %" PRIu64 "\n",
               stats().n_sessions);
    dcb_printf(dcb,
               "\tCurrent no. of router sessions:         %d\n",
               service()->stats.n_current);
    dcb_printf(dcb,
               "\tNumber of queries forwarded:            %" PRIu64 "\n",
               stats().n_queries);
    dcb_printf(dcb,
               "\tNumber of queries forwarded to master:  %" PRIu64 " (%.2f%%)\n",
               stats().n_master,
               master_pct);
    dcb_printf(dcb,
               "\tNumber of queries forwarded to slave:   %" PRIu64 " (%.2f%%)\n",
               stats().n_slave,
               slave_pct);
    dcb_printf(dcb,
               "\tNumber of queries forwarded to all:     %" PRIu64 " (%.2f%%)\n",
               stats().n_all,
               all_pct);
    dcb_printf(dcb,
               "\tNumber of read-write transactions:      %" PRIu64 "\n",
               stats().n_rw_trx);
    dcb_printf(dcb,
               "\tNumber of read-only transactions:       %" PRIu64 "\n",
               stats().n_ro_trx);
    dcb_printf(dcb,
               "\tNumber of replayed transactions:        %" PRIu64 "\n",
               stats().n_trx_replay);

    if (*weightby)
    {
        dcb_printf(dcb,
                   "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb,
                   "\t\tServer               Target %%    Connections  "
                   "Operations\n");
        dcb_printf(dcb, "\t\t                               Global  Router\n");
        for (SERVER_REF* ref = service()->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb,
                       "\t\t%-20s %3.1f%%     %-6d  %-6d  %d\n",
                       ref->server->name,
                       ref->server_weight * 100,
                       ref->server->stats.n_current,
                       ref->connections,
                       ref->server->stats.n_current_ops);
        }
    }

    auto srv_stats = all_server_stats();

    if (!srv_stats.empty())
    {
        dcb_printf(dcb, "    %10s %10s %10s %10s  Sess Avg:%9s  %10s %10s\n",
                   "Server", "Total", "Read", "Write",
                   "dur", "active", "selects");
        for (const auto& s : srv_stats)
        {
            ServerStats::CurrentStats cs = s.second.current_stats();

            dcb_printf(dcb,
                       "    %10s %10ld %10ld %10ld           %9s %10.02f%% %10ld\n",
                       s.first->name,
                       cs.total_queries,
                       cs.total_read_queries,
                       cs.total_write_queries,
                       to_string(cs.ave_session_dur).c_str(),
                       cs.ave_session_active_pct,
                       cs.ave_session_selects);
        }
    }
}

json_t* RWSplit::diagnostics_json() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "connections", json_integer(stats().n_sessions));
    json_object_set_new(rval, "current_connections", json_integer(service()->stats.n_current));
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
        mxb_assert(a.second.total == a.second.read + a.second.write);

        ServerStats::CurrentStats stats = a.second.current_stats();

        json_t* obj = json_object();
        json_object_set_new(obj, "id", json_string(a.first->name));
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

uint64_t RWSplit::getCapabilities()
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING
           | RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_SESSION_STATE_TRACKING
           | RCAP_TYPE_RUNTIME_CONFIG;
}

bool RWSplit::configure(MXS_CONFIG_PARAMETER* params)
{
    bool rval = false;
    Config cnf(params);

    if (handle_max_slaves(cnf, config_get_string(params, "max_slave_connections")))
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
        RCAP_TYPE_STMT_INPUT
        | RCAP_TYPE_TRANSACTION_TRACKING
        | RCAP_TYPE_PACKET_OUTPUT
        | RCAP_TYPE_SESSION_STATE_TRACKING
        | RCAP_TYPE_RUNTIME_CONFIG,
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
            {"max_slave_replication_lag",  MXS_MODULE_PARAM_INT,     "-1"           },
            {"max_slave_connections",      MXS_MODULE_PARAM_STRING,  MAX_SLAVE_COUNT},
            {"retry_failed_reads",         MXS_MODULE_PARAM_BOOL,    "true"         },
            {"disable_sescmd_history",     MXS_MODULE_PARAM_BOOL,    "false"        },
            {"max_sescmd_history",         MXS_MODULE_PARAM_COUNT,   "50"           },
            {"strict_multi_stmt",          MXS_MODULE_PARAM_BOOL,    "false"        },
            {"strict_sp_calls",            MXS_MODULE_PARAM_BOOL,    "false"        },
            {"master_accept_reads",        MXS_MODULE_PARAM_BOOL,    "false"        },
            {"connection_keepalive",       MXS_MODULE_PARAM_COUNT,   "300"          },
            {"causal_reads",               MXS_MODULE_PARAM_BOOL,    "false"        },
            {"causal_reads_timeout",       MXS_MODULE_PARAM_STRING,  "10"           },
            {"master_reconnection",        MXS_MODULE_PARAM_BOOL,    "false"        },
            {"delayed_retry",              MXS_MODULE_PARAM_BOOL,    "false"        },
            {"delayed_retry_timeout",      MXS_MODULE_PARAM_COUNT,   "10"           },
            {"transaction_replay",         MXS_MODULE_PARAM_BOOL,    "false"        },
            {"transaction_replay_max_size",MXS_MODULE_PARAM_SIZE,    "1Mi"          },
            {"optimistic_trx",             MXS_MODULE_PARAM_BOOL,    "false"        },
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}
