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

void AuroraMonitor::destroy()
{
    delete this;
}

/**
 * @brief Update the status of a server
 *
 * This function connects to the database and queries it for its status. The
 * status of the server is adjusted accordingly based on the results of the
 * query.
 *
 * @param monitor  Monitor object
 * @param database Server whose status should be updated
 */
void update_server_status(MXS_MONITOR *monitor, MXS_MONITORED_SERVER *database)
{
    if (!SERVER_IN_MAINT(database->server))
    {
        SERVER temp_server = {};
        temp_server.status = database->server->status;
        server_clear_status_nolock(&temp_server, SERVER_RUNNING | SERVER_MASTER | SERVER_SLAVE | SERVER_AUTH_ERROR);
        database->mon_prev_status = database->server->status;

        /** Try to connect to or ping the database */
        mxs_connect_result_t rval = mon_ping_or_connect_to_db(monitor, database);

        if (mon_connection_is_ok(rval))
        {
            server_set_status_nolock(&temp_server, SERVER_RUNNING);
            MYSQL_RES *result;

            /** Connection is OK, query for replica status */
            if (mxs_mysql_query(database->con, "SELECT @@aurora_server_id, server_id FROM "
                                "information_schema.replica_host_status "
                                "WHERE session_id = 'MASTER_SESSION_ID'") == 0 &&
                (result = mysql_store_result(database->con)))
            {
                ss_dassert(mysql_field_count(database->con) == 2);
                MYSQL_ROW row = mysql_fetch_row(result);
                int status = SERVER_SLAVE;

                /** The master will return a row with two identical non-NULL fields */
                if (row[0] && row[1] && strcmp(row[0], row[1]) == 0)
                {
                    status = SERVER_MASTER;
                }

                server_set_status_nolock(&temp_server, status);
                mysql_free_result(result);
            }
            else
            {
                mon_report_query_error(database);
            }
        }
        else
        {
            /** Failed to connect to the database */
            if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
            {
                server_set_status_nolock(&temp_server, SERVER_AUTH_ERROR);
            }

            if (mon_status_changed(database) && mon_print_fail_status(database))
            {
                mon_log_connect_error(database, rval);
            }
        }

        server_transfer_status(database->server, &temp_server);
    }
}

/**
 * @brief Main monitoring loop
 *
 * @param arg The MONITOR object for this monitor
 */
void AuroraMonitor::main()
{
    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in Aurora monitor. Exiting.");
        return;
    }

    load_server_journal(m_monitor, NULL);

    while (!m_shutdown)
    {
        lock_monitor_servers(m_monitor);
        servers_status_pending_to_current(m_monitor);

        for (MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers; ptr; ptr = ptr->next)
        {
            update_server_status(m_monitor, ptr);

            if (SERVER_IS_DOWN(ptr->server))
            {
                /** Hang up all DCBs connected to the failed server */
                dcb_hangup_foreach(ptr->server);
            }
        }

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(m_monitor, m_script, m_events);

        servers_status_current_to_pending(m_monitor);
        store_server_journal(m_monitor, NULL);
        release_monitor_servers(m_monitor);

        /** Sleep until the next monitoring interval */
        unsigned int ms = 0;
        while (ms < m_monitor->interval && !m_shutdown)
        {
            if (m_monitor->server_pending_changes)
            {
                // Admin has changed something, skip sleep
                break;
            }
            thread_millisleep(MXS_MON_BASE_INTERVAL_MS);
            ms += MXS_MON_BASE_INTERVAL_MS;
        }
    }

    mysql_thread_end();
}

/**
 * @brief Start the monitor
 *
 * This function initializes the monitor and starts the monitoring thread.
 *
 * @param arg The MONITOR structure for this monitor
 * @param opt The configuration parameters for this monitor
 * @return Monitor handle
 */
bool AuroraMonitor::start(const MXS_CONFIG_PARAMETER *params)
{
    bool started = false;

    if (!m_checked)
    {
        if (!check_monitor_permissions(m_monitor, "SELECT @@aurora_server_id, server_id FROM "
                                       "information_schema.replica_host_status "
                                       "WHERE session_id = 'MASTER_SESSION_ID'"))
        {
            MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        }
        else
        {
            m_checked = true;
        }
    }

    if (m_checked)
    {
        m_script = config_copy_string(params, "script");
        m_events = config_get_enum(params, "events", mxs_monitor_event_enum_values);

        if (thread_start(&m_thread, &maxscale::MonitorInstance::main, this, 0) == NULL)
        {
            MXS_ERROR("Failed to start monitor thread for monitor '%s'.", m_monitor->name);
            MXS_FREE(m_script);
            m_script = NULL;
        }
        else
        {
            started = true;
        }
    }

    return started;
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
