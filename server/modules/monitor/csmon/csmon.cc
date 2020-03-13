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

void register_commands()
{
    static modulecmd_arg_type_t cluster_start_argv[] =
        {
        };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-start", MODULECMD_TYPE_ACTIVE,
                               CsMonitor::cluster_start,
                               MXS_ARRAY_NELEMS(cluster_start_argv), cluster_start_argv,
                               "Start Columnstore cluster");

    static modulecmd_arg_type_t cluster_stop_argv[] =
        {
        };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-stop", MODULECMD_TYPE_ACTIVE,
                               CsMonitor::cluster_stop,
                               MXS_ARRAY_NELEMS(cluster_stop_argv), cluster_stop_argv,
                               "Stop Columnstore cluster");

    static modulecmd_arg_type_t cluster_shutdown_argv[] =
        {
        };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-shutdown", MODULECMD_TYPE_ACTIVE,
                               CsMonitor::cluster_shutdown,
                               MXS_ARRAY_NELEMS(cluster_shutdown_argv), cluster_shutdown_argv,
                               "Shutdown Columnstore cluster");

    static modulecmd_arg_type_t cluster_add_node_argv[] =
        {
        };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-add-node", MODULECMD_TYPE_ACTIVE,
                               CsMonitor::cluster_add_node,
                               MXS_ARRAY_NELEMS(cluster_add_node_argv), cluster_add_node_argv,
                               "Add a node to Columnstore cluster");

    static modulecmd_arg_type_t cluster_remove_node_argv[] =
        {
        };

    modulecmd_register_command(MXS_MODULE_NAME, "cluster-remove-node", MODULECMD_TYPE_ACTIVE,
                               CsMonitor::cluster_remove_node,
                               MXS_ARRAY_NELEMS(cluster_remove_node_argv), cluster_remove_node_argv,
                               "Remove a node from Columnstore cluster");
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
