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

#include <maxscale/modinfo.h>
#include <maxscale/thread.h>
#include <maxscale/monitor.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/mysql_utils.h>

struct AURORA_MONITOR : public MXS_MONITOR_INSTANCE
{
    bool         shutdown;      /**< True if the monitor is stopped */
    THREAD       thread;        /**< Monitor thread */
    char*        script;        /**< Launchable script */
    uint64_t     events;        /**< Enabled monitor events */
    MXS_MONITOR* monitor;       /**< Pointer to generic monitor structure */
    bool         checked;       /**< Whether server access has been checked */
};

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
static void
monitorMain(void *arg)
{
    AURORA_MONITOR *handle = (AURORA_MONITOR*)arg;
    MXS_MONITOR *monitor = handle->monitor;

    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in Aurora monitor. Exiting.");
        return;
    }

    load_server_journal(monitor, NULL);

    while (!handle->shutdown)
    {
        lock_monitor_servers(monitor);
        servers_status_pending_to_current(monitor);

        for (MXS_MONITORED_SERVER *ptr = monitor->monitored_servers; ptr; ptr = ptr->next)
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
         */
        mon_process_state_changes(monitor, handle->script, handle->events);

        servers_status_current_to_pending(monitor);
        store_server_journal(monitor, NULL);
        release_monitor_servers(monitor);

        /** Sleep until the next monitoring interval */
        unsigned int ms = 0;
        while (ms < monitor->interval && !handle->shutdown)
        {
            if (monitor->server_pending_changes)
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

static
MXS_MONITOR_INSTANCE* createInstance(MXS_MONITOR* mon)
{
    AURORA_MONITOR* handle = static_cast<AURORA_MONITOR*>(MXS_CALLOC(1, sizeof(AURORA_MONITOR)));

    if (handle)
    {
        handle->shutdown = false;
        handle->thread = 0;
        handle->script = NULL;
        handle->events = 0;
        handle->monitor = mon;
        handle->checked = false;
    }

    return handle;
}

static void destroyInstance(MXS_MONITOR_INSTANCE* mon)
{
    AURORA_MONITOR* handle = static_cast<AURORA_MONITOR*>(mon);
    ss_dassert(!handle->thread);
    ss_dassert(!handle->script);

    MXS_FREE(handle);
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
static bool startMonitor(MXS_MONITOR_INSTANCE *mon, const MXS_CONFIG_PARAMETER *params)
{
    bool started = false;

    AURORA_MONITOR *handle = static_cast<AURORA_MONITOR*>(mon);
    ss_dassert(handle);

    if (!handle->checked)
    {
        if (!check_monitor_permissions(handle->monitor, "SELECT @@aurora_server_id, server_id FROM "
                                       "information_schema.replica_host_status "
                                       "WHERE session_id = 'MASTER_SESSION_ID'"))
        {
            MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        }
        else
        {
            handle->checked = true;
        }
    }

    if (handle->checked)
    {
        handle->script = config_copy_string(params, "script");
        handle->events = config_get_enum(params, "events", mxs_monitor_event_enum_values);

        if (thread_start(&handle->thread, monitorMain, handle, 0) == NULL)
        {
            MXS_ERROR("Failed to start monitor thread for monitor '%s'.", handle->monitor->name);
            MXS_FREE(handle->script);
            handle->script = NULL;
        }
        else
        {
            started = true;
        }
    }

    return started;
}

/**
 * Stop a running monitor
 *
 * @param arg   Handle on thr running monior
 */
static void
stopMonitor(MXS_MONITOR_INSTANCE *mon)
{
    AURORA_MONITOR *handle = static_cast<AURORA_MONITOR*>(mon);
    ss_dassert(handle->thread);

    handle->shutdown = true;
    thread_wait(handle->thread);
    handle->thread = 0;
    handle->shutdown = false;

    MXS_FREE(handle->script);
    handle->script = NULL;
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param mon   The monitor
 */
static void
diagnostics(const MXS_MONITOR_INSTANCE *mon, DCB *dcb)
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param mon   The monitor
 */
static json_t* diagnostics_json(const MXS_MONITOR_INSTANCE *mon)
{
    return NULL;
}

extern "C"
{
/**
 * The module entry point routine. It is this routine that must populate the
 * structure that is referred to as the "module object", this is a structure
 * with the set of external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MONITOR_API MyObject =
    {
        createInstance,
        destroyInstance,
        startMonitor,
        stopMonitor,
        diagnostics,
        diagnostics_json
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "Aurora monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &MyObject,
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

}
