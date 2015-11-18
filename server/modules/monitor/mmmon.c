/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file mm_mon.c - A Multi-Master Multi Muster cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/09/14	Massimiliano Pinto	Initial implementation
 * 08/05/15     Markus Makela           Addition of launchable scripts
 * 17/10/15 Martin Brampton     Change DCB callback to hangup
 *
 * @endverbatim
 */

#include <mmmon.h>
#include <dcb.h>

static void monitorMain(void *);

static char *version_str = "V1.1.1";

MODULE_INFO info =
{
    MODULE_API_MONITOR,
    MODULE_BETA_RELEASE,
    MONITOR_VERSION,
    "A Multi-Master Multi Master monitor"
};

static void *startMonitor(void *, void*);
static void stopMonitor(void *);
static void diagnostics(DCB *, void *);
static void detectStaleMaster(void *, int);
static MONITOR_SERVERS *get_current_master(MONITOR *);
bool isMySQLEvent(monitor_event_t event);

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
 */
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
    MXS_NOTICE("Initialise the Multi-Master Monitor module %s.", version_str);
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

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @param arg	The current handle - NULL if first start
 * @return A handle to use when interacting with the monitor
 */
static void *
startMonitor(void *arg, void* opt)
{
    MONITOR* mon = (MONITOR*) arg;
    MM_MONITOR *handle = mon->handle;
    CONFIG_PARAMETER* params = (CONFIG_PARAMETER*) opt;
    bool have_events = false, script_error = false;

    if (handle)
    {
        handle->shutdown = 0;
    }
    else
    {
        if ((handle = (MM_MONITOR *) malloc(sizeof(MM_MONITOR))) == NULL)
        {
            return NULL;
        }
        handle->shutdown = 0;
        handle->id = MONITOR_DEFAULT_ID;
        handle->master = NULL;
        handle->script = NULL;
        handle->detectStaleMaster = false;
        memset(handle->events, false, sizeof(handle->events));
        spinlock_init(&handle->lock);
    }

    while (params)
    {
        if (!strcmp(params->name, "detect_stale_master"))
        {
            handle->detectStaleMaster = config_truth_value(params->value);
        }
        else if (!strcmp(params->name, "script"))
        {
            if (externcmd_can_execute(params->value))
            {
                free(handle->script);
                handle->script = strdup(params->value);
            }
            else
            {
                script_error = true;
            }
        }
        else if (!strcmp(params->name, "events"))
        {
            if (mon_parse_event_string((bool*) & handle->events,
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
    if (script_error)
    {
	MXS_ERROR("Errors were found in the script configuration parameters "
                  "for the monitor '%s'. The script will not be used.",mon->name);
	free(handle->script);
	handle->script = NULL;
    }
    /** If no specific events are given, enable them all */
    if (!have_events)
    {
        memset(handle->events, true, sizeof(handle->events));
    }
    handle->tid = (THREAD) thread_start(monitorMain, mon);
    return handle;
}

/**
 * Stop a running monitor
 *
 * @param arg	Handle on thr running monior
 */
static void
stopMonitor(void *arg)
{
    MONITOR* mon = arg;
    MM_MONITOR *handle = (MM_MONITOR *) mon->handle;

    handle->shutdown = 1;
    thread_wait((void *) handle->tid);
}

/**
 * Daignostic interface
 *
 * @param dcb	DCB to print diagnostics
 * @param arg	The monitor handle
 */
static void diagnostics(DCB *dcb, void *arg)
{
    MONITOR* mon = (MONITOR*) arg;
    MM_MONITOR *handle = (MM_MONITOR *) mon->handle;
    MONITOR_SERVERS *db;
    char *sep;

    switch (handle->status)
    {
        case MONITOR_RUNNING:
            dcb_printf(dcb, "\tMonitor running\n");
            break;
        case MONITOR_STOPPING:
            dcb_printf(dcb, "\tMonitor stopping\n");
            break;
        case MONITOR_STOPPED:
            dcb_printf(dcb, "\tMonitor stopped\n");
            break;
    }

    dcb_printf(dcb, "\tSampling interval:\t%lu milliseconds\n", mon->interval);
    dcb_printf(dcb, "\tDetect Stale Master:\t%s\n", (handle->detectStaleMaster == 1) ? "enabled" : "disabled");
    dcb_printf(dcb, "\tMonitored servers:	");

    db = mon->databases;
    sep = "";
    while (db)
    {
        dcb_printf(dcb,
                   "%s%s:%d",
                   sep,
                   db->server->name,
                   db->server->port);
        sep = ", ";
        db = db->next;
    }
    dcb_printf(dcb, "\n");
}

/**
 * Monitor an individual server
 *
 * @param handle        The MySQL Monitor object
 * @param database	The database to probe
 */
static void
monitorDatabase(MONITOR* mon, MONITOR_SERVERS *database)
{
    MYSQL_ROW row;
    MYSQL_RES *result;
    int isslave = 0;
    int ismaster = 0;
    unsigned long int server_version = 0;
    char *server_string;

    /* Don't probe servers in maintenance mode */
    if (SERVER_IN_MAINT(database->server))
    {
        return;
    }

    /** Store previous status */
    database->mon_prev_status = database->server->status;
    connect_result_t rval = mon_connect_to_db(mon, database);

    if (rval != MONITOR_CONN_OK)
    {
        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status(database->server, SERVER_AUTH_ERROR);
            monitor_set_pending_status(database, SERVER_AUTH_ERROR);
        }
        server_clear_status(database->server, SERVER_RUNNING);
        monitor_clear_pending_status(database, SERVER_RUNNING);

        /* Also clear M/S state in both server and monitor server pending struct */
        server_clear_status(database->server, SERVER_SLAVE);
        server_clear_status(database->server, SERVER_MASTER);
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_clear_pending_status(database, SERVER_MASTER);

        /* Clean addition status too */
        server_clear_status(database->server, SERVER_STALE_STATUS);
        monitor_clear_pending_status(database, SERVER_STALE_STATUS);

        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }
        return;
    }
    else
    {
        server_clear_status(database->server, SERVER_AUTH_ERROR);
        monitor_clear_pending_status(database, SERVER_AUTH_ERROR);
    }

    /* Store current status in both server and monitor server pending struct */
    server_set_status(database->server, SERVER_RUNNING);
    monitor_set_pending_status(database, SERVER_RUNNING);

    /* get server version from current server */
    server_version = mysql_get_server_version(database->con);

    /* get server version string */
    server_string = (char *) mysql_get_server_info(database->con);
    if (server_string)
    {
        server_set_version_string(database->server, server_string);
    }

    /* get server_id form current node */
    if (mysql_query(database->con, "SELECT @@server_id") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        long server_id = -1;

        if (mysql_field_count(database->con) != 1)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for 'SELECT @@server_id'. Expected 1 column."
                      " MySQL Version: %s", version_str);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            server_id = strtol(row[0], NULL, 10);
            if ((errno == ERANGE && (server_id == LONG_MAX
                                     || server_id == LONG_MIN)) || (errno != 0 && server_id == 0))
            {
                server_id = -1;
            }
            database->server->node_id = server_id;
        }
        mysql_free_result(result);
    }

    /* Check if the Slave_SQL_Running and Slave_IO_Running status is
     * set to Yes
     */

    /* Check first for MariaDB 10.x.x and get status for multimaster replication */
    if (server_version >= 100000)
    {

        if (mysql_query(database->con, "SHOW ALL SLAVES STATUS") == 0
            && (result = mysql_store_result(database->con)) != NULL)
        {
            int i = 0;
            long master_id = -1;

            if (mysql_field_count(database->con) < 42)
            {
                mysql_free_result(result);
                MXS_ERROR("\"SHOW ALL SLAVES STATUS\" "
                          "returned less than the expected amount of columns. Expected 42 columns"
                          " MySQL Version: %s", version_str);
                return;
            }

            while ((row = mysql_fetch_row(result)))
            {
                /* get Slave_IO_Running and Slave_SQL_Running values*/
                if (strncmp(row[12], "Yes", 3) == 0
                    && strncmp(row[13], "Yes", 3) == 0)
                {
                    isslave += 1;
                }

                /* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building 
                 * the replication tree, slaves ids will be added to master(s) and we will have at least the 
                 * root master server.
                 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
                 */
                if (strncmp(row[12], "Yes", 3) == 0)
                {
                    /* get Master_Server_Id values */
                    master_id = atol(row[41]);
                    if (master_id == 0)
                    {
                        master_id = -1;
                    }
                }

                i++;
            }
            /* store master_id of current node */
            memcpy(&database->server->master_id, &master_id, sizeof(long));

            mysql_free_result(result);

            /* If all configured slaves are running set this node as slave */
            if (isslave > 0 && isslave == i)
            {
                isslave = 1;
            }
            else
            {
                isslave = 0;
            }
        }
    }
    else
    {
        if (mysql_query(database->con, "SHOW SLAVE STATUS") == 0
            && (result = mysql_store_result(database->con)) != NULL)
        {
            long master_id = -1;

            if (mysql_field_count(database->con) < 40)
            {
                mysql_free_result(result);

                if (server_version < 5 * 10000 + 5 * 100)
                {
                    if (database->log_version_err)
                    {
                        MXS_ERROR("\"SHOW SLAVE STATUS\" "
                                  " for versions less than 5.5 does not have master_server_id, "
                                  "replication tree cannot be resolved for server %s."
                                  " MySQL Version: %s", database->server->unique_name, version_str);
                        database->log_version_err = false;
                    }
                }
                else
                {
                    MXS_ERROR("\"SHOW SLAVE STATUS\" "
                              "returned less than the expected amount of columns. "
                              "Expected 40 columns."
                              " MySQL Version: %s", version_str);
                }
                return;
            }

            while ((row = mysql_fetch_row(result)))
            {
                /* get Slave_IO_Running and Slave_SQL_Running values*/
                if (strncmp(row[10], "Yes", 3) == 0
                    && strncmp(row[11], "Yes", 3) == 0)
                {
                    isslave = 1;
                }

                /* If Slave_IO_Running = Yes, assign the master_id to current server: this allows building 
                 * the replication tree, slaves ids will be added to master(s) and we will have at least the 
                 * root master server.
                 * Please note, there could be no slaves at all if Slave_SQL_Running == 'No'
                 */
                if (strncmp(row[10], "Yes", 3) == 0)
                {
                    /* get Master_Server_Id values */
                    master_id = atol(row[39]);
                    if (master_id == 0)
                    {
                        master_id = -1;
                    }
                }
            }
            /* store master_id of current node */
            memcpy(&database->server->master_id, &master_id, sizeof(long));

            mysql_free_result(result);
        }
    }

    /* get variable 'read_only' set by an external component */
    if (mysql_query(database->con, "SHOW GLOBAL VARIABLES LIKE 'read_only'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW GLOBAL VARIABLES LIKE 'read_only'\". "
                      "Expected 2 columns. MySQL Version: %s", version_str);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            if (strncasecmp(row[1], "OFF", 3) == 0)
            {
                ismaster = 1;
            }
            else
            {
                isslave = 1;
            }
        }
        mysql_free_result(result);
    }

    /* Remove addition info */
    monitor_clear_pending_status(database, SERVER_STALE_STATUS);

    /* Set the Slave Role */
    if (isslave)
    {
        monitor_set_pending_status(database, SERVER_SLAVE);
        /* Avoid any possible stale Master state */
        monitor_clear_pending_status(database, SERVER_MASTER);

        /* Set replication depth to 1 */
        database->server->depth = 1;
    }
    else
    {
        /* Avoid any possible Master/Slave stale state */
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_clear_pending_status(database, SERVER_MASTER);
    }

    /* Set the Master role */
    if (ismaster)
    {
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_set_pending_status(database, SERVER_MASTER);

        /* Set replication depth to 0 */
        database->server->depth = 0;
    }

}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg	The handle of the monitor
 */
static void
monitorMain(void *arg)
{
    MONITOR* mon = (MONITOR*) arg;
    MM_MONITOR *handle;
    MONITOR_SERVERS *ptr;
    int detect_stale_master = false;
    MONITOR_SERVERS *root_master = NULL;
    size_t nrounds = 0;

    spinlock_acquire(&mon->lock);
    handle = (MM_MONITOR *) mon->handle;
    spinlock_release(&mon->lock);
    detect_stale_master = handle->detectStaleMaster;

    if (mysql_thread_init())
    {
        MXS_ERROR("Fatal : mysql_thread_init failed in monitor "
                  "module. Exiting.");
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

        /* start from the first server in the list */
        ptr = mon->databases;

        while (ptr)
        {
            /* copy server status into monitor pending_status */
            ptr->pending_status = ptr->server->status;

            /* monitor current node */
            monitorDatabase(mon, ptr);

            if (mon_status_changed(ptr))
            {
                dcb_hangup_foreach(ptr->server);
            }

            if (mon_status_changed(ptr) ||
                mon_print_fail_status(ptr))
            {
                MXS_DEBUG("Backend server %s:%d state : %s",
                          ptr->server->name,
                          ptr->server->port,
                          STRSRVSTATUS(ptr->server));
            }
            if (SERVER_IS_DOWN(ptr->server))
            {
                /** Increase this server'e error count */
                ptr->mon_err_count += 1;
            }
            else
            {
                /** Reset this server's error count */
                ptr->mon_err_count = 0;
            }

            ptr = ptr->next;
        }

        /* Get Master server pointer */
        root_master = get_current_master(mon);

        /* Update server status from monitor pending status on that server*/

        ptr = mon->databases;
        while (ptr)
        {
            if (!SERVER_IN_MAINT(ptr->server))
            {
                /* If "detect_stale_master" option is On, let's use the previus master */
                if (detect_stale_master && root_master && (!strcmp(ptr->server->name, root_master->server->name) && ptr->server->port == root_master->server->port) && (ptr->server->status & SERVER_MASTER) && !(ptr->pending_status & SERVER_MASTER))
                {
                    /* in this case server->status will not be updated from pending_status */
                    MXS_NOTICE("[mysql_mon]: root server [%s:%i] is no longer Master, let's "
                               "use it again even if it could be a stale master, you have "
                               "been warned!", ptr->server->name, ptr->server->port);
                    /* Set the STALE bit for this server in server struct */
                    server_set_status(ptr->server, SERVER_STALE_STATUS);
                }
                else
                {
                    ptr->server->status = ptr->pending_status;
                }
            }
            ptr = ptr->next;
        }

        ptr = mon->databases;
        monitor_event_t evtype;
        while (ptr)
        {
            if (mon_status_changed(ptr))
            {
                evtype = mon_get_event_type(ptr);
                if (isMySQLEvent(evtype))
                {
                    MXS_INFO("Server changed state: %s[%s:%u]: %s",
                             ptr->server->unique_name,
                             ptr->server->name, ptr->server->port,
                             mon_get_event_name(ptr));
                    if (handle->script && handle->events[evtype])
                    {
                        monitor_launch_script(mon, ptr, handle->script);
                    }
                }
            }
            ptr = ptr->next;
        }
    }
}

/**
 * Enable/Disable the MySQL Replication Stale Master dectection, allowing a previouvsly detected master to still act as a Master.
 * This option must be enabled in order to keep the Master when the replication is stopped or removed from slaves.
 * If the replication is still stopped when MaxSclale is restarted no Master will be available.
 *
 * @param arg		The handle allocated by startMonitor
 * @param enable	To enable it 1, disable it with 0
 */
static void
detectStaleMaster(void *arg, int enable)
{
    MONITOR* mon = (MONITOR*) arg;
    MM_MONITOR *handle = (MM_MONITOR *) mon->handle;
    memcpy(&handle->detectStaleMaster, &enable, sizeof(int));
}

/*******
 * This function returns the master server
 * from a set of MySQL Multi Master monitored servers
 * and returns the root server (that has SERVER_MASTER bit)
 * The server is returned even for servers in 'maintenance' mode.
 *
 * @param handle        The monitor handle
 * @return              The server at root level with SERVER_MASTER bit
 */

static MONITOR_SERVERS *get_current_master(MONITOR *mon)
{
    MM_MONITOR* handle = mon->handle;
    MONITOR_SERVERS *ptr;

    ptr = mon->databases;

    while (ptr)
    {
        /* The server could be in SERVER_IN_MAINT
         * that means SERVER_IS_RUNNING returns 0
         * Let's check only for SERVER_IS_DOWN: server is not running
         */
        if (SERVER_IS_DOWN(ptr->server))
        {
            ptr = ptr->next;
            continue;
        }

        if (ptr->server->depth == 0)
        {
            handle->master = ptr;
        }

        ptr = ptr->next;
    }


    /*
     * Return the root master
     */

    if (handle->master != NULL)
    {
        /* If the root master is in MAINT, return NULL */
        if (SERVER_IN_MAINT(handle->master->server))
        {
            return NULL;
        }
        else
        {
            return handle->master;
        }
    }
    else
    {
        return NULL;
    }
}


static monitor_event_t mysql_events[] =
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

/**
 * Check if the MM monitor is monitoring this event type.
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 * */
bool isMySQLEvent(monitor_event_t event)
{
    int i;
    for (i = 0; mysql_events[i] != MAX_MONITOR_EVENT; i++)
    {
        if (event == mysql_events[i])
        {
            return true;
        }
    }
    return false;
}
