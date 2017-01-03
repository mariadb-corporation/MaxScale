/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file auroramon.c - Amazon RDS Aurora monitor
 */

#include <maxscale/modinfo.h>
#include <maxscale/thread.h>
#include <maxscale/monitor.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/debug.h>

typedef struct aurora_monitor
{
    bool   shutdown;            /**< True if the monitor is stopped */
    THREAD thread;              /**< Monitor thread */
    char*  script;              /**< Launchable script */
    bool   events[MAX_MONITOR_EVENT]; /**< Enabled monitor events */
} AURORA_MONITOR;

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
void update_server_status(MONITOR *monitor, MONITOR_SERVERS *database)
{
    if (!SERVER_IN_MAINT(database->server))
    {
        SERVER temp_server = {.status = database->server->status};
        server_clear_status_nolock(&temp_server, SERVER_RUNNING | SERVER_MASTER | SERVER_SLAVE | SERVER_AUTH_ERROR);
        database->mon_prev_status = database->server->status;

        /** Try to connect to or ping the database */
        connect_result_t rval = mon_connect_to_db(monitor, database);

        if (rval == MONITOR_CONN_OK)
        {
            server_set_status_nolock(&temp_server, SERVER_RUNNING);
            MYSQL_RES *result;

            /** Connection is OK, query for replica status */
            if (mysql_query(database->con, "SELECT @@aurora_server_id, server_id FROM "
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
                MXS_ERROR("Failed to query server %s (%s:%d): %d, %s",
                          database->server->unique_name, database->server->name,
                          database->server->port, mysql_errno(database->con),
                          mysql_error(database->con));
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
 * @brief Check if this is an event that the Aurora monitor handles
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 * */
bool is_aurora_event(monitor_event_t event)
{
    static monitor_event_t aurora_events[] =
    {
        MASTER_DOWN_EVENT,
        MASTER_UP_EVENT,
        SLAVE_DOWN_EVENT,
        SLAVE_UP_EVENT,
        SERVER_DOWN_EVENT,
        SERVER_UP_EVENT,
        LOST_MASTER_EVENT,
        LOST_SLAVE_EVENT,
        NEW_MASTER_EVENT,
        NEW_SLAVE_EVENT,
        MAX_MONITOR_EVENT
    };

    for (int i = 0; aurora_events[i] != MAX_MONITOR_EVENT; i++)
    {
        if (event == aurora_events[i])
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Main monitoring loop
 *
 * @param arg The MONITOR object for this monitor
 */
static void
monitorMain(void *arg)
{
    MONITOR *monitor = (MONITOR*)arg;
    AURORA_MONITOR *handle = monitor->handle;

    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in Aurora monitor. Exiting.");
        return;
    }

    while (!handle->shutdown)
    {
        lock_monitor_servers(monitor);
        servers_status_pending_to_current(monitor);

        for (MONITOR_SERVERS *ptr = monitor->databases; ptr; ptr = ptr->next)
        {
            update_server_status(monitor, ptr);

            if (SERVER_IS_DOWN(ptr->server))
            {
                /** Hang up all DCBs connected to the failed server */
                dcb_hangup_foreach(ptr->server);
            }
        }

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         *
         * TODO: Move this functionality into monitor.c, it is duplicated in
         * every monitor.
         */
        for (MONITOR_SERVERS *ptr = monitor->databases; ptr; ptr = ptr->next)
        {
            if (mon_status_changed(ptr))
            {
                monitor_event_t evtype = mon_get_event_type(ptr);
                if (is_aurora_event(evtype))
                {
                    mon_log_state_change(ptr);
                    if (handle->script && handle->events[evtype])
                    {
                        monitor_launch_script(monitor, ptr, handle->script);
                    }
                }
            }
        }
        servers_status_current_to_pending(monitor);
        release_monitor_servers(monitor);

        /** Sleep until the next monitoring interval */
        int ms = 0;
        while (ms < monitor->interval && !handle->shutdown)
        {
            if (monitor->server_pending_changes)
            {
                // Admin has changed something, skip sleep
                break;
            }
            thread_millisleep(MON_BASE_INTERVAL_MS);
            ms += MON_BASE_INTERVAL_MS;
        }
    }

    mysql_thread_end();
}

/**
 * Helper function to free the monitor handle
 */
static void auroramon_free(AURORA_MONITOR *handle)
{
    if (handle)
    {
        MXS_FREE(handle->script);
        MXS_FREE(handle);
    }
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
static void *
startMonitor(MONITOR *mon, const CONFIG_PARAMETER *params)
{
    bool have_events = false, script_error = false;
    AURORA_MONITOR *handle = mon->handle;

    if (handle)
    {
        handle->shutdown = false;
    }
    else
    {
        if ((handle = (AURORA_MONITOR *) MXS_MALLOC(sizeof(AURORA_MONITOR))) == NULL)
        {
            return NULL;
        }

        handle->shutdown = false;
        handle->script = NULL;
        memset(handle->events, false, sizeof(handle->events));

        while (params)
        {
            if (strcmp(params->name, "script") == 0)
            {
                if (externcmd_can_execute(params->value))
                {
                    handle->script = MXS_STRDUP_A(params->value);
                }
                else
                {
                    script_error = true;
                }
            }
            else if (strcmp(params->name, "events") == 0)
            {
                if (mon_parse_event_string(handle->events, sizeof(handle->events), params->value) != 0)
                {
                    script_error = true;
                }
                else
                {
                    have_events = true;
                }
            }
            params = params->next;
        }

        if (!check_monitor_permissions(mon, "SELECT @@aurora_server_id, server_id FROM "
                                       "information_schema.replica_host_status "
                                       "WHERE session_id = 'MASTER_SESSION_ID'"))
        {
            MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
            auroramon_free(handle);
            return NULL;
        }

        if (script_error)
        {
            MXS_ERROR("Errors were found in the script configuration parameters "
                      "for the monitor '%s'.", mon->name);
            auroramon_free(handle);
            return NULL;
        }

        /** If no specific events are given, enable them all */
        if (!have_events)
        {
            memset(handle->events, true, sizeof(handle->events));
        }
    }

    if (thread_start(&handle->thread, monitorMain, mon) == NULL)
    {
        MXS_ERROR("Failed to start monitor thread for monitor '%s'.", mon->name);
        auroramon_free(handle);
        return NULL;
    }

    return handle;
}

/**
 * Stop a running monitor
 *
 * @param arg   Handle on thr running monior
 */
static void
stopMonitor(MONITOR *mon)
{
    AURORA_MONITOR *handle = (AURORA_MONITOR *) mon->handle;

    handle->shutdown = true;
    thread_wait(handle->thread);
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param mon   The monitor
 */
static void
diagnostics(DCB *dcb, const MONITOR *mon)
{
}

/**
 * The module entry point routine. It is this routine that must populate the
 * structure that is referred to as the "module object", this is a structure
 * with the set of external entry points for this module.
 *
 * @return The module object
 */
MODULE_INFO* GetModuleObject()
{
    static MONITOR_OBJECT MyObject =
    {
        startMonitor,
        stopMonitor,
        diagnostics
    };

    static MODULE_INFO info =
    {
        MODULE_API_MONITOR,
        MODULE_BETA_RELEASE,
        MONITOR_VERSION,
        "Aurora monitor",
        "V1.0.0",
        &MyObject
    };

    return &info;
}
