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

/**
 * @file ndbcluster_mon.c - A MySQL cluster SQL node monitor
 */

#define MXS_MODULE_NAME "ndbclustermon"

#include "ndbclustermon.hh"
#include <maxscale/alloc.h>
#include <maxscale/mysql_utils.h>

bool isNdbEvent(mxs_monitor_event_t event);

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
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {
                "script",
                MXS_MODULE_PARAM_PATH,
                NULL,
                MXS_MODULE_OPT_PATH_X_OK
            },
            {
                "events",
                MXS_MODULE_PARAM_ENUM,
                MXS_MONITOR_EVENT_DEFAULT_VALUE,
                MXS_MODULE_OPT_NONE,
                mxs_monitor_event_enum_values
            },
            {MXS_END_MODULE_PARAMS} // No parameters
        }
    };

    return &info;
}


NDBCMonitor::NDBCMonitor(MXS_MONITOR *monitor)
    : maxscale::MonitorInstance(monitor)
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

void NDBCMonitor::destroy()
{
    delete this;
}

bool NDBCMonitor::has_sufficient_permissions() const
{
    return check_monitor_permissions(m_monitor, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'");
}

void NDBCMonitor::configure(const MXS_CONFIG_PARAMETER* params)
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param arg   The monitor handle
 */
static void
diagnostics(const MXS_MONITOR_INSTANCE *mon, DCB *dcb)
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param arg   The monitor handle
 */
static json_t* diagnostics_json(const MXS_MONITOR_INSTANCE *mon)
{
    return NULL;
}

/**
 * Monitor an individual server
 *
 * @param database  The database to probe
 */
void NDBCMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    mxs_connect_result_t rval = mon_ping_or_connect_to_db(m_monitor, monitored_server);

    if (!mon_connection_is_ok(rval))
    {
        monitor_clear_pending_status(monitored_server, SERVER_RUNNING);

        if (mysql_errno(monitored_server->con) == ER_ACCESS_DENIED_ERROR)
        {
            monitor_set_pending_status(monitored_server, SERVER_AUTH_ERROR);
        }
        else
        {
            monitor_clear_pending_status(monitored_server, SERVER_AUTH_ERROR);
        }

        monitored_server->server->node_id = -1;

        if (mon_status_changed(monitored_server) && mon_print_fail_status(monitored_server))
        {
            mon_log_connect_error(monitored_server, rval);
        }

        return;
    }

    monitor_clear_pending_status(monitored_server, SERVER_AUTH_ERROR);
    monitor_set_pending_status(monitored_server, SERVER_RUNNING);

    MYSQL_ROW row;
    MYSQL_RES *result;
    int isjoined = 0;
    char *server_string;

    /* get server version string */
    mxs_mysql_set_server_version(monitored_server->con, monitored_server->server);
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
                      " MySQL Version: %s", server_string);
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
                      " MySQL Version: %s", server_string);
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
        monitored_server->server->depth = 0;
    }
    else
    {
        monitor_clear_pending_status(monitored_server, SERVER_NDB);
        monitored_server->server->depth = -1;
    }

    monitored_server->server->status = monitored_server->pending_status;
}
