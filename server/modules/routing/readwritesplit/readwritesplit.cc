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

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <cmath>
#include <new>

#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/query_classifier.h>
#include <maxscale/router.h>
#include <maxscale/spinlock.h>
#include <maxscale/mysql_utils.h>

#include "rwsplitsession.hh"
#include "routeinfo.hh"

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

/**
 * @brief Process router options
 *
 * @param router Router instance
 * @param options Router options
 * @return True on success, false if a configuration error was found
 */
static bool rwsplit_process_router_options(Config& config,
                                           char **options)
{
    int i;
    char *value;
    select_criteria_t c;

    if (options == NULL)
    {
        return true;
    }

    MXS_WARNING("Router options for readwritesplit are deprecated.");

    bool success = true;

    for (i = 0; options[i]; i++)
    {
        if ((value = strchr(options[i], '=')) == NULL)
        {
            MXS_ERROR("Unsupported router option \"%s\" for readwritesplit router.", options[i]);
            success = false;
        }
        else
        {
            *value = 0;
            value++;
            if (strcmp(options[i], "slave_selection_criteria") == 0)
            {
                c = GET_SELECT_CRITERIA(value);
                ss_dassert(c == LEAST_GLOBAL_CONNECTIONS ||
                           c == LEAST_ROUTER_CONNECTIONS || c == LEAST_BEHIND_MASTER ||
                           c == LEAST_CURRENT_OPERATIONS || c == UNDEFINED_CRITERIA);

                if (c == UNDEFINED_CRITERIA)
                {
                    MXS_ERROR("Unknown slave selection criteria \"%s\". "
                              "Allowed values are LEAST_GLOBAL_CONNECTIONS, "
                              "LEAST_ROUTER_CONNECTIONS, LEAST_BEHIND_MASTER,"
                              "and LEAST_CURRENT_OPERATIONS.",
                              STRCRITERIA(config.slave_selection_criteria));
                    success = false;
                }
                else
                {
                    config.slave_selection_criteria = c;
                }
            }
            else if (strcmp(options[i], "max_sescmd_history") == 0)
            {
                config.max_sescmd_history = atoi(value);
            }
            else if (strcmp(options[i], "disable_sescmd_history") == 0)
            {
                config.disable_sescmd_history = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_accept_reads") == 0)
            {
                config.master_accept_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "strict_multi_stmt") == 0)
            {
                config.strict_multi_stmt = config_truth_value(value);
            }
            else if (strcmp(options[i], "strict_sp_calls") == 0)
            {
                config.strict_sp_calls = config_truth_value(value);
            }
            else if (strcmp(options[i], "retry_failed_reads") == 0)
            {
                config.retry_failed_reads = config_truth_value(value);
            }
            else if (strcmp(options[i], "master_failure_mode") == 0)
            {
                if (strcasecmp(value, "fail_instantly") == 0)
                {
                    config.master_failure_mode = RW_FAIL_INSTANTLY;
                }
                else if (strcasecmp(value, "fail_on_write") == 0)
                {
                    config.master_failure_mode = RW_FAIL_ON_WRITE;
                }
                else if (strcasecmp(value, "error_on_write") == 0)
                {
                    config.master_failure_mode = RW_ERROR_ON_WRITE;
                }
                else
                {
                    MXS_ERROR("Unknown value for 'master_failure_mode': %s", value);
                    success = false;
                }
            }
            else
            {
                MXS_ERROR("Unknown router option \"%s=%s\" for readwritesplit router.",
                          options[i], value);
                success = false;
            }
        }
    } /*< for */

    return success;
}

// TODO: Don't process parameters in readwritesplit
static bool handle_max_slaves(Config& config, const char *str)
{
    bool rval = true;
    char *endptr;
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

RWSplit::RWSplit(SERVICE* service, const Config& config):
    mxs::Router<RWSplit, RWSplitSession>(service),
    m_service(service),
    m_config(config)
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
int RWSplit::max_slave_count() const
{
    int router_nservers = m_service->n_dbref;
    int conf_max_nslaves = m_config.max_slave_connections > 0 ?
                           m_config.max_slave_connections :
                           (router_nservers * m_config.rw_max_slave_conn_percent) / 100;
    return MXS_MIN(router_nservers - 1, MXS_MAX(1, conf_max_nslaves));
}

bool RWSplit::have_enough_servers() const
{
    bool succp = true;
    const int min_nsrv = 1;
    const int router_nsrv = m_service->n_dbref;

    int n_serv = MXS_MAX(m_config.max_slave_connections,
                         (router_nsrv * m_config.rw_max_slave_conn_percent) / 100);

    /** With too few servers session is not created */
    if (router_nsrv < min_nsrv || n_serv < min_nsrv)
    {
        if (router_nsrv < min_nsrv)
        {
            MXS_ERROR("Unable to start %s service. There are "
                      "too few backend servers available. Found %d "
                      "when %d is required.", m_service->name, router_nsrv, min_nsrv);
        }
        else
        {
            int pct = m_config.rw_max_slave_conn_percent / 100;
            int nservers = router_nsrv * pct;

            if (m_config.max_slave_connections < min_nsrv)
            {
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d when %d is required.",
                          m_service->name, m_config.max_slave_connections, min_nsrv);
            }
            if (nservers < min_nsrv)
            {
                double dbgpct = ((double)min_nsrv / (double)router_nsrv) * 100.0;
                MXS_ERROR("Unable to start %s service. There are "
                          "too few backend servers configured in "
                          "MaxScale.cnf. Found %d%% when at least %.0f%% "
                          "would be required.", m_service->name,
                          m_config.rw_max_slave_conn_percent, dbgpct);
            }
        }
        succp = false;
    }

    return succp;
}

/**
 * API function definitions
 */


RWSplit* RWSplit::create(SERVICE *service, char **options)
{

    MXS_CONFIG_PARAMETER* params = service->svc_config_param;
    Config config(params);

    if (!handle_max_slaves(config, config_get_string(params, "max_slave_connections")) ||
        (options && !rwsplit_process_router_options(config, options)))
    {
        return NULL;
    }

    /** These options cancel each other out */
    if (config.disable_sescmd_history && config.max_sescmd_history > 0)
    {
        config.max_sescmd_history = 0;
    }

    return new (std::nothrow) RWSplit(service, config);
}

RWSplitSession* RWSplit::newSession(MXS_SESSION *session)
{
    RWSplitSession* rses = NULL;
    MXS_EXCEPTION_GUARD(rses = RWSplitSession::create(this, session));
    return rses;
}

void RWSplit::diagnostics(DCB *dcb)
{
    RWSplit *router = this;
    const char *weightby = serviceGetWeightingParameter(router->service());
    double master_pct = 0.0, slave_pct = 0.0, all_pct = 0.0;

    dcb_printf(dcb, "\n");
    dcb_printf(dcb, "\tuse_sql_variables_in:      %s\n",
               mxs_target_to_str(router->config().use_sql_variables_in));
    dcb_printf(dcb, "\tslave_selection_criteria:  %s\n",
               select_criteria_to_str(router->config().slave_selection_criteria));
    dcb_printf(dcb, "\tmaster_failure_mode:       %s\n",
               failure_mode_to_str(router->config().master_failure_mode));
    dcb_printf(dcb, "\tmax_slave_replication_lag: %d\n",
               router->config().max_slave_replication_lag);
    dcb_printf(dcb, "\tretry_failed_reads:        %s\n",
               router->config().retry_failed_reads ? "true" : "false");
    dcb_printf(dcb, "\tstrict_multi_stmt:         %s\n",
               router->config().strict_multi_stmt ? "true" : "false");
    dcb_printf(dcb, "\tstrict_sp_calls:           %s\n",
               router->config().strict_sp_calls ? "true" : "false");
    dcb_printf(dcb, "\tdisable_sescmd_history:    %s\n",
               router->config().disable_sescmd_history ? "true" : "false");
    dcb_printf(dcb, "\tmax_sescmd_history:        %lu\n",
               router->config().max_sescmd_history);
    dcb_printf(dcb, "\tmaster_accept_reads:       %s\n",
               router->config().master_accept_reads ? "true" : "false");
    dcb_printf(dcb, "\n");

    if (router->stats().n_queries > 0)
    {
        master_pct = ((double)router->stats().n_master / (double)router->stats().n_queries) * 100.0;
        slave_pct = ((double)router->stats().n_slave / (double)router->stats().n_queries) * 100.0;
        all_pct = ((double)router->stats().n_all / (double)router->stats().n_queries) * 100.0;
    }

    dcb_printf(dcb, "\tNumber of router sessions:           	%" PRIu64 "\n",
               router->stats().n_sessions);
    dcb_printf(dcb, "\tCurrent no. of router sessions:      	%d\n",
               router->service()->stats.n_current);
    dcb_printf(dcb, "\tNumber of queries forwarded:          	%" PRIu64 "\n",
               router->stats().n_queries);
    dcb_printf(dcb, "\tNumber of queries forwarded to master:	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_master, master_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to slave: 	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_slave, slave_pct);
    dcb_printf(dcb, "\tNumber of queries forwarded to all:   	%" PRIu64 " (%.2f%%)\n",
               router->stats().n_all, all_pct);

    if (*weightby)
    {
        dcb_printf(dcb, "\tConnection distribution based on %s "
                   "server parameter.\n",
                   weightby);
        dcb_printf(dcb, "\t\tServer               Target %%    Connections  "
                   "Operations\n");
        dcb_printf(dcb, "\t\t                               Global  Router\n");
        for (SERVER_REF *ref = router->service()->dbref; ref; ref = ref->next)
        {
            dcb_printf(dcb, "\t\t%-20s %3.1f%%     %-6d  %-6d  %d\n",
                       ref->server->unique_name, (float)ref->weight / 10,
                       ref->server->stats.n_current, ref->connections,
                       ref->server->stats.n_current_ops);
        }
    }
}

json_t* RWSplit::diagnostics_json() const
{
    const RWSplit *router = this;
    json_t* rval = json_object();

    json_object_set_new(rval, "use_sql_variables_in",
                        json_string(mxs_target_to_str(router->config().use_sql_variables_in)));
    json_object_set_new(rval, "slave_selection_criteria",
                        json_string(select_criteria_to_str(router->config().slave_selection_criteria)));
    json_object_set_new(rval, "master_failure_mode",
                        json_string(failure_mode_to_str(router->config().master_failure_mode)));
    json_object_set_new(rval, "max_slave_replication_lag",
                        json_integer(router->config().max_slave_replication_lag));
    json_object_set_new(rval, "retry_failed_reads",
                        json_boolean(router->config().retry_failed_reads));
    json_object_set_new(rval, "strict_multi_stmt",
                        json_boolean(router->config().strict_multi_stmt));
    json_object_set_new(rval, "strict_sp_calls",
                        json_boolean(router->config().strict_sp_calls));
    json_object_set_new(rval, "disable_sescmd_history",
                        json_boolean(router->config().disable_sescmd_history));
    json_object_set_new(rval, "max_sescmd_history",
                        json_integer(router->config().max_sescmd_history));
    json_object_set_new(rval, "master_accept_reads",
                        json_boolean(router->config().master_accept_reads));


    json_object_set_new(rval, "connections", json_integer(router->stats().n_sessions));
    json_object_set_new(rval, "current_connections", json_integer(router->service()->stats.n_current));
    json_object_set_new(rval, "queries", json_integer(router->stats().n_queries));
    json_object_set_new(rval, "route_master", json_integer(router->stats().n_master));
    json_object_set_new(rval, "route_slave", json_integer(router->stats().n_slave));
    json_object_set_new(rval, "route_all", json_integer(router->stats().n_all));

    const char *weightby = serviceGetWeightingParameter(router->service());

    if (*weightby)
    {
        json_object_set_new(rval, "weightby", json_string(weightby));
    }

    return rval;
}

uint64_t RWSplit::getCapabilities()
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING |
        RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_SESSION_STATE_TRACKING;
}

MXS_BEGIN_DECLS

/**
 * The module entry point routine. It is this routine that must return
 * the structure that is referred to as the "module object". This is a
 * structure with the set of external entry points for this module.
 */
MXS_MODULE *MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER, MXS_MODULE_GA, MXS_ROUTER_VERSION,
        "A Read/Write splitting router for enhancement read scalability",
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT | RCAP_TYPE_TRANSACTION_TRACKING |
        RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_SESSION_STATE_TRACKING,
        &RWSplit::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
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
            {"max_slave_replication_lag", MXS_MODULE_PARAM_INT, "-1"},
            {"max_slave_connections", MXS_MODULE_PARAM_STRING, MAX_SLAVE_COUNT},
            {"retry_failed_reads", MXS_MODULE_PARAM_BOOL, "true"},
            {"disable_sescmd_history", MXS_MODULE_PARAM_BOOL, "false"},
            {"max_sescmd_history", MXS_MODULE_PARAM_COUNT, "50"},
            {"strict_multi_stmt",  MXS_MODULE_PARAM_BOOL, "false"},
            {"strict_sp_calls",  MXS_MODULE_PARAM_BOOL, "false"},
            {"master_accept_reads", MXS_MODULE_PARAM_BOOL, "false"},
            {"connection_keepalive", MXS_MODULE_PARAM_COUNT, "0"},
            {"enable_causal_read", MXS_MODULE_PARAM_BOOL, "false"},
            {"causal_read_timeout", MXS_MODULE_PARAM_STRING, "0"},
            {"master_reconnection", MXS_MODULE_PARAM_BOOL, "false"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    MXS_NOTICE("Initializing statement-based read/write split router module.");
    return &info;
}

MXS_END_DECLS
