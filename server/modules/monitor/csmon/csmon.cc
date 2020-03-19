/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "csmonitor.hh"

namespace
{

bool cluster_start(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 2));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(pArgs->argc == 1 || MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_SERVER);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    SERVER* pServer = pArgs->argc == 1 ? nullptr : pArgs->argv[1].value.server;

    return pMonitor->command_cluster_start(pServer, ppOutput);
}

bool cluster_shutdown(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 2));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(pArgs->argc == 1 || MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_SERVER);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    SERVER* pServer = pArgs->argc == 1 ? nullptr : pArgs->argv[1].value.server;

    return pMonitor->command_cluster_shutdown(pServer, ppOutput);
}

bool cluster_ping(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 2));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(pArgs->argc == 1 || MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_SERVER);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    SERVER* pServer = pArgs->argc == 1 ? nullptr : pArgs->argv[1].value.server;

    return pMonitor->command_cluster_ping(pServer, ppOutput);
}

bool cluster_status(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 2));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(pArgs->argc == 1 || MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_SERVER);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    SERVER* pServer = pArgs->argc == 1 ? nullptr : pArgs->argv[1].value.server;

    return pMonitor->command_cluster_status(pServer, ppOutput);
}

bool cluster_add_node(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 1));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_cluster_add_node(ppOutput);
}

bool cluster_remove_node(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert((pArgs->argc >= 1) && (pArgs->argc <= 1));
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_cluster_remove_node(ppOutput);
}

bool cluster_config_get(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_cluster_config_get(ppOutput);
}

bool cluster_config_put(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_cluster_config_put(ppOutput);
}

bool async(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 2);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_STRING);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);
    const auto* zCommand = pArgs->argv[1].value.string;

    return pMonitor->command_async(zCommand, ppOutput);
}

bool result(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_result(ppOutput);
}

bool cancel(const MODULECMD_ARG* pArgs, json_t** ppOutput)
{
    mxb_assert(pArgs->argc == 1);
    mxb_assert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_MONITOR);

    auto* pMonitor = static_cast<CsMonitor*>(pArgs->argv[0].value.monitor);

    return pMonitor->command_cancel(ppOutput);
}

void register_commands()
{
    static const char ARG_MONITOR_DESC[] = "Monitor name";

    static modulecmd_arg_type_t cluster_start_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to start" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-start", MODULECMD_TYPE_ACTIVE,
                               cluster_start,
                               MXS_ARRAY_NELEMS(cluster_start_argv), cluster_start_argv,
                               "Start Columnstore cluster");

    static modulecmd_arg_type_t cluster_shutdown_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to shutdown" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-shutdown", MODULECMD_TYPE_ACTIVE,
                               cluster_shutdown,
                               MXS_ARRAY_NELEMS(cluster_shutdown_argv), cluster_shutdown_argv,
                               "Shutdown Columnstore cluster");

    static modulecmd_arg_type_t cluster_ping_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to ping" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-ping", MODULECMD_TYPE_PASSIVE,
                               cluster_ping,
                               MXS_ARRAY_NELEMS(cluster_ping_argv), cluster_ping_argv,
                               "Ping Columnstore cluster");

    static modulecmd_arg_type_t cluster_status_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_SERVER | MODULECMD_ARG_OPTIONAL, "Specific server to query status" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-status", MODULECMD_TYPE_PASSIVE,
                               cluster_status,
                               MXS_ARRAY_NELEMS(cluster_status_argv), cluster_status_argv,
                               "Get Columnstore cluster status");

    static modulecmd_arg_type_t cluster_config_get_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-config-get", MODULECMD_TYPE_PASSIVE,
                               cluster_config_get,
                               MXS_ARRAY_NELEMS(cluster_config_get_argv), cluster_config_get_argv,
                               "Get Columnstore cluster config");

    static modulecmd_arg_type_t cluster_config_put_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-config-put", MODULECMD_TYPE_PASSIVE,
                               cluster_config_put,
                               MXS_ARRAY_NELEMS(cluster_config_put_argv), cluster_config_put_argv,
                               "Put Columnstore cluster config");

    static modulecmd_arg_type_t cluster_add_node_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-add-node", MODULECMD_TYPE_ACTIVE,
                               cluster_add_node,
                               MXS_ARRAY_NELEMS(cluster_add_node_argv), cluster_add_node_argv,
                               "Add a node to Columnstore cluster");

    static modulecmd_arg_type_t cluster_remove_node_argv[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC}
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-remove-node", MODULECMD_TYPE_ACTIVE,
                               cluster_remove_node,
                               MXS_ARRAY_NELEMS(cluster_remove_node_argv), cluster_remove_node_argv,
                               "Remove a node from Columnstore cluster");

    static modulecmd_arg_type_t async_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
        { MODULECMD_ARG_STRING, "Command to execute" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "async", MODULECMD_TYPE_ACTIVE,
                               async,
                               MXS_ARRAY_NELEMS(async_argv), async_argv,
                               "Execute a command asynchronously");

    static modulecmd_arg_type_t result_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    };

    modulecmd_register_command(MXS_MODULE_NAME, "result", MODULECMD_TYPE_ACTIVE,
                               result,
                               MXS_ARRAY_NELEMS(result_argv), result_argv,
                               "Retrieve result of last command");

    static modulecmd_arg_type_t cancel_argv[] =
    {
        { MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ARG_MONITOR_DESC },
    };

    modulecmd_register_command(MXS_MODULE_NAME, "cancel", MODULECMD_TYPE_ACTIVE,
                               cancel,
                               MXS_ARRAY_NELEMS(result_argv), result_argv,
                               "Cancel on-going command");
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
