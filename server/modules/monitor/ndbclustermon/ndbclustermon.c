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
 * @file ndbcluster_mon.c - A MySQL cluster SQL node monitor
 *
 * @verbatim
 * Revision History
 *
 * Date     Who                 Description
 * 25/07/14 Massimiliano Pinto  Initial implementation
 * 10/11/14 Massimiliano Pinto  Added setNetworkTimeout for connect,read,write
 * 08/05/15 Markus Makela       Addition of launchable scripts
 *
 * @endverbatim
 */


#include "../mysqlmon.h"
#include <maxscale/alloc.h>

static void monitorMain(void *);

static char *version_str = "V2.1.0";

/* @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
MODULE_INFO info =
{
    MODULE_API_MONITOR,
    MODULE_BETA_RELEASE,
    MONITOR_VERSION,
    "A MySQL cluster SQL node monitor"
};
/*lint +e14 */

static void *startMonitor(MONITOR *, const CONFIG_PARAMETER *params);
static void stopMonitor(MONITOR *);
static void diagnostics(DCB *, const MONITOR *);
bool isNdbEvent(monitor_event_t event);

static MONITOR_OBJECT MyObject =
{
    startMonitor,
    stopMonitor,
    diagnostics
};

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 *
 * @see function load_module in load_utils.c for explanation of the following
 * lint directives.
 */
/*lint -e14 */
char *
version()
{
    return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
    MXS_NOTICE("Initialise the MySQL Cluster Monitor module %s.", version_str);
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MONITOR_OBJECT *
GetModuleObject()
{
    return &MyObject;
}
/*lint +e14 */

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @return A handle to use when interacting with the monitor
 */
static void *
startMonitor(MONITOR *mon, const CONFIG_PARAMETER *params)
{
    MYSQL_MONITOR *handle = mon->handle;
    bool have_events = false, script_error = false;

    if (handle != NULL)
    {
        handle->shutdown = 0;
    }
    else
    {
        if ((handle = (MYSQL_MONITOR *) MXS_MALLOC(sizeof(MYSQL_MONITOR))) == NULL)
        {
            return NULL;
        }
        handle->shutdown = 0;
        handle->id = MONITOR_DEFAULT_ID;
        handle->script = NULL;
        handle->master = NULL;
        memset(handle->events, false, sizeof(handle->events));
        spinlock_init(&handle->lock);
    }
    while (params)
    {
        if (!strcmp(params->name, "script"))
        {
            if (externcmd_can_execute(params->value))
            {
                MXS_FREE(handle->script);
                handle->script = MXS_STRDUP_A(params->value);
            }
            else
            {
                script_error = true;
            }
        }
        else if (!strcmp(params->name, "events"))
        {
            if (mon_parse_event_string((bool *)handle->events,
                                       sizeof(handle->events), params->value) != 0)
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

    /** SHOW STATUS doesn't require any special permissions */
    if (!check_monitor_permissions(mon, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'"))
    {
        MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        MXS_FREE(handle->script);
        MXS_FREE(handle);
        return NULL;
    }

    if (script_error)
    {
        MXS_ERROR("Errors were found in the script configuration parameters "
                  "for the monitor '%s'. The script will not be used.", mon->name);
        MXS_FREE(handle->script);
        handle->script = NULL;
    }
    /** If no specific events are given, enable them all */
    if (!have_events)
    {
        memset(handle->events, true, sizeof(handle->events));
    }

    if (thread_start(&handle->thread, monitorMain, mon) == NULL)
    {
        MXS_ERROR("Failed to start monitor thread for monitor '%s'.", mon->name);
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
    MYSQL_MONITOR *handle = (MYSQL_MONITOR *) mon->handle;

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
diagnostics(DCB *dcb, const MONITOR *mon)
{
}

/**
 * Monitor an individual server
 *
 * @param database  The database to probe
 */
static void
monitorDatabase(MONITOR_SERVERS *database, char *defaultUser, char *defaultPasswd, MONITOR *mon)
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

    connect_result_t rval = mon_connect_to_db(mon, database);
    if (rval != MONITOR_CONN_OK)
    {
        server_clear_status(database->server, SERVER_RUNNING);

        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status(database->server, SERVER_AUTH_ERROR);
        }

        database->server->node_id = -1;

        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }
        return;
    }

    server_clear_status(database->server, SERVER_AUTH_ERROR);
    /* If we get this far then we have a working connection */
    server_set_status(database->server, SERVER_RUNNING);

    /* get server version string */
    server_string = (char *) mysql_get_server_info(database->con);
    if (server_string)
    {
        server_set_version_string(database->server, server_string);
    }

    /* Check if the the SQL node is able to contact one or more data nodes */
    if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE "
                      "'Ndb_number_of_ready_data_nodes'\". Expected 2 columns."
                      " MySQL Version: %s", version_str);
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

    /* Check the the SQL node id in the MySQL cluster */
    if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_cluster_node_id'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE 'Ndb_cluster_node_id'\". "
                      "Expected 2 columns."
                      " MySQL Version: %s", version_str);
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

    if (isjoined)
    {
        server_set_status(database->server, SERVER_NDB);
        database->server->depth = 0;
    }
    else
    {
        server_clear_status(database->server, SERVER_NDB);
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
    MONITOR* mon = arg;
    MYSQL_MONITOR *handle;
    MONITOR_SERVERS *ptr;
    size_t nrounds = 0;

    spinlock_acquire(&mon->lock);
    handle = (MYSQL_MONITOR *) mon->handle;
    spinlock_release(&mon->lock);

    if (mysql_thread_init())
    {
        MXS_ERROR("Fatal : mysql_thread_init failed in monitor module. Exiting.");
        return;
    }
    handle->status = MONITOR_RUNNING;

    while (1)
    {
        if (handle->shutdown)
        {
            handle->status = MONITOR_STOPPING;
            mysql_thread_end();
            handle->status = MONITOR_STOPPED;
            return;
        }

        /** Wait base interval */
        thread_millisleep(MON_BASE_INTERVAL_MS);
        /**
         * Calculate how far away the monitor interval is from its full
         * cycle and if monitor interval time further than the base
         * interval, then skip monitoring checks. Excluding the first
         * round.
         */
        if (nrounds != 0 &&
            ((nrounds * MON_BASE_INTERVAL_MS) % mon->interval) >=
            MON_BASE_INTERVAL_MS)
        {
            nrounds += 1;
            continue;
        }
        nrounds += 1;
        ptr = mon->databases;

        while (ptr)
        {
            ptr->mon_prev_status = ptr->server->status;
            monitorDatabase(ptr, mon->user, mon->password, mon);

            if (ptr->server->status != ptr->mon_prev_status ||
                SERVER_IS_DOWN(ptr->server))
            {
                MXS_DEBUG("Backend server %s:%d state : %s",
                          ptr->server->name,
                          ptr->server->port,
                          STRSRVSTATUS(ptr->server));
            }

            ptr = ptr->next;
        }

        ptr = mon->databases;
        monitor_event_t evtype;

        while (ptr)
        {
            /** Execute monitor script if a server state has changed */
            if (mon_status_changed(ptr))
            {
                evtype = mon_get_event_type(ptr);
                if (isNdbEvent(evtype))
                {
                    mon_log_state_change(ptr);
                    if (handle->script && handle->events[evtype])
                    {
                        monitor_launch_script(mon, ptr, handle->script);
                    }
                }
            }
            ptr = ptr->next;
        }

        mon_hangup_failed_servers(mon);
    }
}


static monitor_event_t ndb_events[] =
{
    MASTER_DOWN_EVENT,
    MASTER_UP_EVENT,
    SLAVE_DOWN_EVENT,
    SLAVE_UP_EVENT,
    SERVER_DOWN_EVENT,
    SERVER_UP_EVENT,
    NDB_UP_EVENT,
    NDB_DOWN_EVENT,
    LOST_MASTER_EVENT,
    LOST_SLAVE_EVENT,
    LOST_NDB_EVENT,
    NEW_MASTER_EVENT,
    NEW_SLAVE_EVENT,
    NEW_NDB_EVENT,
    MAX_MONITOR_EVENT
};

/**
 * Check if the event type is one the ndbcustermonitor is interested in.
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 */
bool isNdbEvent(monitor_event_t event)
{
    int i;
    for (i = 0; ndb_events[i] != MAX_MONITOR_EVENT; i++)
    {
        if (event == ndb_events[i])
        {
            return true;
        }
    }
    return false;
}
