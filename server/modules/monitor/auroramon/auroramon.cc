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
 * @file auroramon.c - Amazon RDS Aurora monitor
 */

#define MXS_MODULE_NAME "auroramon"

#include "auroramon.hh"
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/modinfo.h>
#include <maxscale/mysql_utils.h>


AuroraMonitor::AuroraMonitor(MXS_MONITOR* monitor)
    : maxscale::MonitorInstance(monitor)
{
}

AuroraMonitor::~AuroraMonitor()
{
}

//static
AuroraMonitor* AuroraMonitor::create(MXS_MONITOR* monitor)
{
    return new AuroraMonitor(monitor);
}

/**
 * @brief Update the status of a server
 *
 * This function connects to the database and queries it for its status. The
 * status of the server is adjusted accordingly based on the results of the
 * query.
 *
 * @param monitored_server  Server whose status should be updated
 */
void AuroraMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    monitor_clear_pending_status(monitored_server, SERVER_MASTER | SERVER_SLAVE);

    MYSQL_RES *result;

    /** Connection is OK, query for replica status */
    if (mxs_mysql_query(monitored_server->con, "SELECT @@aurora_server_id, server_id FROM "
                        "information_schema.replica_host_status "
                        "WHERE session_id = 'MASTER_SESSION_ID'") == 0 &&
        (result = mysql_store_result(monitored_server->con)))
    {
        ss_dassert(mysql_field_count(monitored_server->con) == 2);
        MYSQL_ROW row = mysql_fetch_row(result);
        int status = SERVER_SLAVE;

        /** The master will return a row with two identical non-NULL fields */
        if (row[0] && row[1] && strcmp(row[0], row[1]) == 0)
        {
            status = SERVER_MASTER;
        }

        monitor_set_pending_status(monitored_server, status);
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(monitored_server);
    }
}

bool AuroraMonitor::has_sufficient_permissions() const
{
    return check_monitor_permissions(m_monitor, "SELECT @@aurora_server_id, server_id FROM "
                                     "information_schema.replica_host_status "
                                     "WHERE session_id = 'MASTER_SESSION_ID'");
}

void AuroraMonitor::configure(const MXS_CONFIG_PARAMETER* params)
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param mon   The monitor
 */
void AuroraMonitor::diagnostics(DCB *dcb) const
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param mon   The monitor
 */
json_t* AuroraMonitor::diagnostics_json() const
{
    return NULL;
}

/**
 * The module entry point routine. It is this routine that must populate the
 * structure that is referred to as the "module object", this is a structure
 * with the set of external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "Aurora monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<AuroraMonitor>::s_api,
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
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
