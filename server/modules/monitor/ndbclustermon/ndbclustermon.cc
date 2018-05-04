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

#include "ndbclustermon.h"
#include <maxscale/alloc.h>
#include <maxscale/mysql_utils.h>

static void monitorMain(void *);

/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */

/*lint +e14 */

static MXS_SPECIFIC_MONITOR *createInstance(MXS_MONITOR *,
                                            const MXS_CONFIG_PARAMETER *params);
static void destroyInstance(MXS_SPECIFIC_MONITOR*);
static MXS_SPECIFIC_MONITOR *startMonitor(MXS_MONITOR *,
                                          const MXS_CONFIG_PARAMETER *params);
static void stopMonitor(MXS_SPECIFIC_MONITOR *);
static void diagnostics(const MXS_SPECIFIC_MONITOR *, DCB *);
static json_t* diagnostics_json(const MXS_SPECIFIC_MONITOR *);
bool isNdbEvent(mxs_monitor_event_t event);



extern "C"
{

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise the MySQL Cluster Monitor module.");

    static MXS_MONITOR_OBJECT MyObject =
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
        "A MySQL cluster SQL node monitor",
        "V2.1.0",
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
            {MXS_END_MODULE_PARAMS} // No parameters
        }
    };

    return &info;
}

}
/*lint +e14 */

static MXS_SPECIFIC_MONITOR* createInstance(MXS_MONITOR *mon,
                                            const MXS_CONFIG_PARAMETER *params)
{
    NDBC_MONITOR* handle = static_cast<NDBC_MONITOR*>(MXS_CALLOC(1, sizeof(NDBC_MONITOR)));

    if (handle)
    {
        handle->shutdown = 0;
        handle->id = MXS_MONITOR_DEFAULT_ID;
        handle->master = NULL;
        handle->monitor = mon;

        handle->script = config_copy_string(params, "script");
        handle->events = config_get_enum(params, "events", mxs_monitor_event_enum_values);

        if (check_monitor_permissions(mon, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'"))
        {
            handle->checked = true;
        }
        else
        {
            handle->checked = false;
            MXS_ERROR("Monitor cannot access servers. Starting the monitor will fail "
                      "unless problem was temporary or is addressed");
        }
    }

    return handle;
}

void destroyInstance(MXS_SPECIFIC_MONITOR* mon)
{
    NDBC_MONITOR* handle = static_cast<NDBC_MONITOR*>(mon);

    MXS_FREE(handle->script);
    MXS_FREE(handle);
}

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @return A handle to use when interacting with the monitor
 */
static MXS_SPECIFIC_MONITOR *
startMonitor(MXS_MONITOR *mon, const MXS_CONFIG_PARAMETER *params)
{
    NDBC_MONITOR *handle = static_cast<NDBC_MONITOR*>(mon->handle);
    bool have_events = false, script_error = false;

    if (handle != NULL)
    {
        handle->shutdown = 0;
        MXS_FREE(handle->script);
    }
    else
    {
        if ((handle = (NDBC_MONITOR *) MXS_MALLOC(sizeof(NDBC_MONITOR))) == NULL)
        {
            return NULL;
        }
        handle->shutdown = 0;
        handle->id = MXS_MONITOR_DEFAULT_ID;
        handle->master = NULL;
        handle->monitor = mon;
    }

    handle->script = config_copy_string(params, "script");
    handle->events = config_get_enum(params, "events", mxs_monitor_event_enum_values);

    /** SHOW STATUS doesn't require any special permissions */
    if (!check_monitor_permissions(mon, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'"))
    {
        MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        MXS_FREE(handle->script);
        MXS_FREE(handle);
        return NULL;
    }

    handle->checked = true;

    if (thread_start(&handle->thread, monitorMain, handle, 0) == NULL)
    {
        MXS_ERROR("Failed to start monitor thread for monitor '%s'.", mon->name);
        MXS_FREE(handle->script);
        MXS_FREE(handle);
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
stopMonitor(MXS_SPECIFIC_MONITOR *mon)
{
    NDBC_MONITOR *handle = static_cast<NDBC_MONITOR*>(mon);

    handle->shutdown = 1;
    thread_wait(handle->thread);
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param arg   The monitor handle
 */
static void
diagnostics(const MXS_SPECIFIC_MONITOR *mon, DCB *dcb)
{
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to send output
 * @param arg   The monitor handle
 */
static json_t* diagnostics_json(const MXS_SPECIFIC_MONITOR *mon)
{
    return NULL;
}

/**
 * Monitor an individual server
 *
 * @param database  The database to probe
 */
static void
monitorDatabase(MXS_MONITORED_SERVER *database, char *defaultUser, char *defaultPasswd, MXS_MONITOR *mon)
{
    MYSQL_ROW row;
    MYSQL_RES *result;
    int isjoined = 0;
    char *server_string;

    /* Don't even probe server flagged as in maintenance */
    if (SERVER_IN_MAINT(database->server))
    {
        return;
    }

    mxs_connect_result_t rval = mon_ping_or_connect_to_db(mon, database);
    if (rval != MONITOR_CONN_OK)
    {
        server_clear_status_nolock(database->server, SERVER_RUNNING);

        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(database->server, SERVER_AUTH_ERROR);
        }

        database->server->node_id = -1;

        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }
        return;
    }

    server_clear_status_nolock(database->server, SERVER_AUTH_ERROR);
    /* If we get this far then we have a working connection */
    server_set_status_nolock(database->server, SERVER_RUNNING);

    /* get server version string */
    mxs_mysql_set_server_version(database->con, database->server);
    server_string = database->server->version_string;

    /* Check if the the SQL node is able to contact one or more data nodes */
    if (mxs_mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
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
        mon_report_query_error(database);
    }

    /* Check the the SQL node id in the MySQL cluster */
    if (mxs_mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_cluster_node_id'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
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
            database->server->node_id = cluster_node_id;
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(database);
    }

    if (isjoined)
    {
        server_set_status_nolock(database->server, SERVER_NDB);
        database->server->depth = 0;
    }
    else
    {
        server_clear_status_nolock(database->server, SERVER_NDB);
        database->server->depth = -1;
    }
}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg   The handle of the monitor
 */
static void
monitorMain(void *arg)
{
    NDBC_MONITOR *handle = (NDBC_MONITOR*)arg;
    MXS_MONITOR* mon = handle->monitor;
    MXS_MONITORED_SERVER *ptr;
    size_t nrounds = 0;

    if (mysql_thread_init())
    {
        MXS_ERROR("Fatal : mysql_thread_init failed in monitor module. Exiting.");
        return;
    }

    handle->status = MXS_MONITOR_RUNNING;
    load_server_journal(mon, NULL);

    while (1)
    {
        if (handle->shutdown)
        {
            handle->status = MXS_MONITOR_STOPPING;
            mysql_thread_end();
            handle->status = MXS_MONITOR_STOPPED;
            return;
        }

        /** Wait base interval */
        thread_millisleep(MXS_MON_BASE_INTERVAL_MS);
        /**
         * Calculate how far away the monitor interval is from its full
         * cycle and if monitor interval time further than the base
         * interval, then skip monitoring checks. Excluding the first
         * round.
         */
        if (nrounds != 0 &&
            ((nrounds * MXS_MON_BASE_INTERVAL_MS) % mon->interval) >=
            MXS_MON_BASE_INTERVAL_MS)
        {
            nrounds += 1;
            continue;
        }
        nrounds += 1;

        lock_monitor_servers(mon);
        servers_status_pending_to_current(mon);

        ptr = mon->monitored_servers;
        while (ptr)
        {
            ptr->mon_prev_status = ptr->server->status;
            monitorDatabase(ptr, mon->user, mon->password, mon);

            if (ptr->server->status != ptr->mon_prev_status ||
                SERVER_IS_DOWN(ptr->server))
            {
                MXS_DEBUG("Backend server [%s]:%d state : %s",
                          ptr->server->address,
                          ptr->server->port,
                          STRSRVSTATUS(ptr->server));
            }

            ptr = ptr->next;
        }

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(mon, handle->script, handle->events);

        mon_hangup_failed_servers(mon);
        servers_status_current_to_pending(mon);
        store_server_journal(mon, NULL);
        release_monitor_servers(mon);
    }
}
