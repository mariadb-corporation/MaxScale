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

/**
 * @file ndbcluster_mon.c - A MySQL cluster SQL node monitor
 */

#define MXS_MODULE_NAME "ndbclustermon"

#include "ndbclustermon.hh"
#include <maxscale/alloc.h>
#include <maxscale/mysql_utils.h>


NDBCMonitor::NDBCMonitor(MXS_MONITOR* monitor)
    : maxscale::MonitorInstanceSimple(monitor)
    , m_id(MXS_MONITOR_DEFAULT_ID)
{
}

NDBCMonitor::~NDBCMonitor()
{
}

// static
NDBCMonitor* NDBCMonitor::create(MXS_MONITOR* monitor)
{
    return new NDBCMonitor(monitor);
}

bool NDBCMonitor::has_sufficient_permissions() const
{
    return check_monitor_permissions(m_monitor, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'");
}

void NDBCMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    MYSQL_ROW row;
    MYSQL_RES* result;
    int isjoined = 0;
    char* server_string;

    /* get server version string */
    mxs_mysql_update_server_version(monitored_server->con, monitored_server->server);
    server_string = monitored_server->server->version_string;

    /* Check if the the SQL node is able to contact one or more data nodes */
    if (mxs_mysql_query(monitored_server->con, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'") == 0
        && (result = mysql_store_result(monitored_server->con)) != NULL)
    {
        if (mysql_field_count(monitored_server->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE "
                      "'Ndb_number_of_ready_data_nodes'\". Expected 2 columns."
                      " MySQL Version: %s",
                      server_string);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            if (atoi(row[1]) > 0)
            {
                isjoined = 1;
            }
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(monitored_server);
    }

    /* Check the the SQL node id in the MySQL cluster */
    if (mxs_mysql_query(monitored_server->con, "SHOW STATUS LIKE 'Ndb_cluster_node_id'") == 0
        && (result = mysql_store_result(monitored_server->con)) != NULL)
    {
        if (mysql_field_count(monitored_server->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE 'Ndb_cluster_node_id'\". "
                      "Expected 2 columns."
                      " MySQL Version: %s",
                      server_string);
            return;
        }

        long cluster_node_id = -1;
        while ((row = mysql_fetch_row(result)))
        {
            cluster_node_id = strtol(row[1], NULL, 10);
            if ((errno == ERANGE && (cluster_node_id == LONG_MAX
                                     || cluster_node_id == LONG_MIN)) || (errno != 0 && cluster_node_id == 0))
            {
                cluster_node_id = -1;
            }
            monitored_server->server->node_id = cluster_node_id;
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(monitored_server);
    }

    if (isjoined)
    {
        monitor_set_pending_status(monitored_server, SERVER_NDB);
    }
    else
    {
        monitor_clear_pending_status(monitored_server, SERVER_NDB);
    }
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MySQL Cluster Monitor module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "A MySQL cluster SQL node monitor",
        "V2.1.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<NDBCMonitor>::s_api,
        NULL,   /* Process init. */
        NULL,   /* Process finish. */
        NULL,   /* Thread init. */
        NULL,   /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}     // No parameters
        }
    };

    return &info;
}
