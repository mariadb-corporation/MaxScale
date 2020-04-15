/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-04-23
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "csmonitor.hh"
#include <chrono>

namespace
{

const char ARG_MONITOR_DESC[] = "Monitor name";

const char CLUSTER_ADD_NODE_DESC[]    = "Add a node to a Columnstore cluster.";
const char CLUSTER_CONFIG_GET_DESC[]  = "Get Columnstore cluster [or server] config.";
const char CLUSTER_CONFIG_SET_DESC[]  = "Set Columnstore cluster [or server] config.";
const char CLUSTER_MODE_SET_DESC[]    = "Set Columnstore cluster mode.";
const char CLUSTER_PING_DESC[]        = "Ping Columnstore cluster [or server].";
const char CLUSTER_REMOVE_NODE_DESC[] = "Remove a node from a Columnstore cluster.";
const char CLUSTER_SHUTDOWN_DESC[]    = "Shutdown Columnstore cluster [or server].";
const char CLUSTER_START_DESC[]       = "Start Columnstore cluster [or server].";
const char CLUSTER_STATUS_DESC[]      = "Get Columnstore cluster [or server] status.";

const modulecmd_arg_type_t cluster_start_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to start" }
};

const modulecmd_arg_type_t cluster_shutdown_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_STRING, "Timeout, 0 means no timeout." },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to shutdown" }
};

const modulecmd_arg_type_t cluster_ping_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to ping" }
};

const modulecmd_arg_type_t cluster_status_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to query status" }
};

const modulecmd_arg_type_t cluster_config_get_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to to obtain config from" }
};

const modulecmd_arg_type_t cluster_config_set_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_STRING, "Configuration as JSON object" },
    { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to configure" }
};

const modulecmd_arg_type_t cluster_add_node_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER, "Server to add to Columnstore cluster" }
};

const modulecmd_arg_type_t cluster_remove_node_argv[] =
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_SERVER, "Server to remove from Columnstore cluster" }
};

const modulecmd_arg_type_t cluster_mode_set_argv[]
{
    { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    { MODULECMD_ARG_STRING, "Cluster mode; readonly or readwrite" }
};


bool get_args(const MODULECMD_ARG* pArgs,
              json_t** ppOutput,
              CsMonitor** ppMonitor,
              CsMonitorServer** ppServer)
{
    bool rv = true;

    mxb_assert(pArgs->argc >= 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(pArgs->argc == 1 || MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_SERVER);

    CsMonitor* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    CsMonitorServer* pServer = nullptr;

    if (pArgs->argc >= 2)
    {
        pServer = pMonitor->get_monitored_server(pArgs->argv[1].value.server);

        if (!pServer)
        {
            PRINT_MXS_JSON_ERROR(ppOutput, "The provided server '%s' is not monitored by this monitor.",
                                 pArgs->argv[1].value.server->name());
            rv = false;
        }
    }

    *ppMonitor = pMonitor;
    *ppServer = pServer;

    return rv;
}

bool get_args(const MODULECMD_ARG* pArgs,
              json_t** ppOutput,
              CsMonitor** ppMonitor,
              const char** pzText,
              CsMonitorServer** ppServer)
{
    bool rv = true;

    mxb_assert(pArgs->argc >= 2);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_STRING);
    mxb_assert(pArgs->argc == 2 || MODULECMD_GET_TYPE(&pArgs->argv[2].type) == MODULECMD_ARG_SERVER);

    CsMonitor* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    const char* zText = pArgs->argv[1].value.string;
    CsMonitorServer* pServer = nullptr;

    if (pArgs->argc >= 3)
    {
        pServer = pMonitor->get_monitored_server(pArgs->argv[2].value.server);

        if (!pServer)
        {
            PRINT_MXS_JSON_ERROR(ppOutput, "The provided server '%s' is not monitored by this monitor.",
                                 pArgs->argv[2].value.server->name());
            rv = false;
        }
    }

    *ppMonitor = pMonitor;
    *pzText = zText;
    *ppServer = pServer;

    return rv;
}


bool cluster_start(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_start(ppOutput, pServer);
    }

    return rv;
}

bool cluster_shutdown(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    const char* zTimeout;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &zTimeout, &pServer);

    if (rv)
    {
        std::chrono::seconds timeout(0);

        if (strcmp(zTimeout, "0") != 0)
        {
            std::chrono::milliseconds duration;
            mxs::config::DurationUnit unit;
            rv = get_suffixed_duration(zTimeout, mxs::config::NO_INTERPRETATION, &duration, &unit);

            if (rv)
            {
                if (unit == mxs::config::DURATION_IN_MILLISECONDS)
                {
                    MXS_WARNING("Duration specified in milliseconds, will be converted to seconds.");
                }

                timeout = std::chrono::duration_cast<std::chrono::seconds>(duration);
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppOutput,
                                     "The timeout must be 0, or specified with a s, m, or h suffix");
                rv = false;
            }
        }

        if (rv)
        {
            rv = pMonitor->command_cluster_shutdown(ppOutput, timeout, pServer);
        }
    }

    return rv;
}

bool cluster_ping(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_ping(ppOutput, pServer);
    }

    return rv;
}

bool cluster_status(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_status(ppOutput, pServer);
    }

    return rv;
}

bool cluster_add_node(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_add_node(ppOutput, pServer);
    }

    return rv;
}

bool cluster_remove_node(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_remove_node(ppOutput, pServer);
    }

    return rv;
}

bool cluster_config_get(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_config_get(ppOutput, pServer);
    }

    return rv;
}

bool cluster_config_set(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    CsMonitor* pMonitor;
    const char* zJson;
    CsMonitorServer* pServer;

    bool rv = get_args(pArgs, ppOutput, &pMonitor, &zJson, &pServer);

    if (rv)
    {
        rv = pMonitor->command_cluster_config_set(ppOutput, zJson, pServer);
    }

    return rv;
}

bool cluster_mode_set(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 2);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_STRING);

    CsMonitor* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    const char* zMode = pArgs->argv[1].value.string;

    return pMonitor->command_cluster_mode_set(ppOutput, zMode);
}

void register_commands()
{
    modulecmd_register_command(MXS_MODULE_NAME, "cluster-start", MODULECMD_TYPE_ACTIVE,
                               cluster_start,
                               MXS_ARRAY_NELEMS(cluster_start_argv), cluster_start_argv,
                               CLUSTER_START_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-shutdown", MODULECMD_TYPE_ACTIVE,
                               cluster_shutdown,
                               MXS_ARRAY_NELEMS(cluster_shutdown_argv), cluster_shutdown_argv,
                               CLUSTER_SHUTDOWN_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-ping", MODULECMD_TYPE_PASSIVE,
                               cluster_ping,
                               MXS_ARRAY_NELEMS(cluster_ping_argv), cluster_ping_argv,
                               CLUSTER_PING_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-status", MODULECMD_TYPE_PASSIVE,
                               cluster_status,
                               MXS_ARRAY_NELEMS(cluster_status_argv), cluster_status_argv,
                               CLUSTER_STATUS_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-config-get", MODULECMD_TYPE_PASSIVE,
                               cluster_config_get,
                               MXS_ARRAY_NELEMS(cluster_config_get_argv), cluster_config_get_argv,
                               CLUSTER_CONFIG_GET_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-config-set", MODULECMD_TYPE_PASSIVE,
                               cluster_config_set,
                               MXS_ARRAY_NELEMS(cluster_config_set_argv), cluster_config_set_argv,
                               CLUSTER_CONFIG_SET_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-add-node", MODULECMD_TYPE_ACTIVE,
                               cluster_add_node,
                               MXS_ARRAY_NELEMS(cluster_add_node_argv), cluster_add_node_argv,
                               CLUSTER_ADD_NODE_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-remove-node", MODULECMD_TYPE_ACTIVE,
                               cluster_remove_node,
                               MXS_ARRAY_NELEMS(cluster_remove_node_argv), cluster_remove_node_argv,
                               CLUSTER_REMOVE_NODE_DESC);

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-mode-set", MODULECMD_TYPE_ACTIVE,
                               cluster_mode_set,
                               MXS_ARRAY_NELEMS(cluster_mode_set_argv), cluster_mode_set_argv,
                               CLUSTER_MODE_SET_DESC);
}
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "MariaDB ColumnStore monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<CsMonitor>::s_api,
        NULL,                                   /* Process init. */
        NULL,                                   /* Process finish. */
        NULL,                                   /* Thread init. */
        NULL,                                   /* Thread finish. */
    };

    static bool populated = false;

    if (!populated)
    {
        register_commands();

        CsConfig::populate(info);
        populated = true;
    }

    return &info;
}
