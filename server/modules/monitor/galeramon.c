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
 * @file galera_mon.c - A MySQL Galera cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 22/07/13	Mark Riddoch		Initial implementation
 * 21/05/14	Massimiliano Pinto	Monitor sets a master server
 *					that has the lowest value of wsrep_local_index
 * 23/05/14	Massimiliano Pinto	Added 1 configuration option (setInterval).
 * 					Interval is printed in diagnostics.
 * 03/06/14	Mark Riddoch		Add support for maintenance mode
 * 24/06/14	Massimiliano Pinto	Added depth level 0 for each node
 * 30/10/14	Massimiliano Pinto	Added disableMasterFailback feature
 * 10/11/14	Massimiliano Pinto	Added setNetworkTimeout for connect,read,write
 * 20/04/15	Guillaume Lefranc	Added availableWhenDonor feature
 * 22/04/15     Martin Brampton         Addition of disableMasterRoleSetting
 * 08/05/15     Markus Makela           Addition of launchable scripts
 * 17/10/15 Martin Brampton     Change DCB callback to hangup
 *
 * @endverbatim
 */


#include <galeramon.h>
#include <dcb.h>

static void monitorMain(void *);

static char *version_str = "V2.0.0";

MODULE_INFO info =
{
    MODULE_API_MONITOR,
    MODULE_GA,
    MONITOR_VERSION,
    "A Galera cluster monitor"
};

static void *startMonitor(void *, void*);
static void stopMonitor(void *);
static void diagnostics(DCB *, void *);
static MONITOR_SERVERS *get_candidate_master(MONITOR*);
static MONITOR_SERVERS *set_cluster_master(MONITOR_SERVERS *, MONITOR_SERVERS *, int);
static void disableMasterFailback(void *, int);
bool isGaleraEvent(monitor_event_t event);

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
    MXS_NOTICE("Initialise the MySQL Galera Monitor module %s.", version_str);
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
 * @return A handle to use when interacting with the monitor
 */
static void *
startMonitor(void *arg, void* opt)
{
    MONITOR* mon = arg;
    GALERA_MONITOR *handle = mon->handle;
    CONFIG_PARAMETER* params = (CONFIG_PARAMETER*) opt;
    bool have_events = false, script_error = false;
    if (handle != NULL)
    {
        handle->shutdown = 0;
    }
    else
    {
        if ((handle = (GALERA_MONITOR *) malloc(sizeof(GALERA_MONITOR))) == NULL)
        {
            return NULL;
        }
        handle->shutdown = 0;
        handle->id = MONITOR_DEFAULT_ID;
        handle->disableMasterFailback = 0;
        handle->availableWhenDonor = 0;
        handle->disableMasterRoleSetting = 0;
        handle->master = NULL;
        handle->script = NULL;
        handle->use_priority = false;
        memset(handle->events, false, sizeof(handle->events));
        spinlock_init(&handle->lock);
    }


    while (params)
    {
        if (!strcmp(params->name, "disable_master_failback"))
        {
            handle->disableMasterFailback = config_truth_value(params->value);
        }
        else if (!strcmp(params->name, "available_when_donor"))
        {
            handle->availableWhenDonor = config_truth_value(params->value);
        }
        else if (!strcmp(params->name, "disable_master_role_setting"))
        {
            handle->disableMasterRoleSetting = config_truth_value(params->value);
        }
        else if (!strcmp(params->name, "use_priority"))
        {
            handle->use_priority = config_truth_value(params->value);
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
                  "for the monitor '%s'. The script will not be used.", mon->name);
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
    MONITOR* mon = (MONITOR*) arg;
    GALERA_MONITOR *handle = (GALERA_MONITOR *) mon->handle;

    handle->shutdown = 1;
    thread_wait((void *) handle->tid);
}

/**
 * Diagnostic interface
 *
 * @param dcb	DCB to send output
 * @param arg	The monitor handle
 */
static void
diagnostics(DCB *dcb, void *arg)
{
    MONITOR* mon = (MONITOR*) arg;
    GALERA_MONITOR *handle = (GALERA_MONITOR *) mon->handle;
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
    dcb_printf(dcb, "\tMaster Failback:\t%s\n", (handle->disableMasterFailback == 1) ? "off" : "on");
    dcb_printf(dcb, "\tAvailable when Donor:\t%s\n", (handle->availableWhenDonor == 1) ? "on" : "off");
    dcb_printf(dcb, "\tMaster Role Setting Disabled:\t%s\n", (handle->disableMasterRoleSetting == 1) ? "on" : "off");
    dcb_printf(dcb, "\tConnect Timeout:\t%i seconds\n", mon->connect_timeout);
    dcb_printf(dcb, "\tRead Timeout:\t\t%i seconds\n", mon->read_timeout);
    dcb_printf(dcb, "\tWrite Timeout:\t\t%i seconds\n", mon->write_timeout);
    dcb_printf(dcb, "\tMonitored servers:	");

    db = mon->databases;
    sep = "";
    while (db)
    {
        dcb_printf(dcb, "%s%s:%d", sep, db->server->name, db->server->port);
        sep = ", ";
        db = db->next;
    }
    dcb_printf(dcb, "\n");
}

/**
 * Monitor an individual server. Does not deal with the setting of master or
 * slave bits, except for clearing them when a server is not joined to the
 * cluster.
 *
 * @param handle        The MySQL Monitor object
 * @param database      The database to probe
 */
static void
monitorDatabase(MONITOR *mon, MONITOR_SERVERS *database)
{
    GALERA_MONITOR* handle = (GALERA_MONITOR*) mon->handle;
    MYSQL_ROW row;
    MYSQL_RES *result, *result2;
    int isjoined = 0;
    unsigned long int server_version = 0;
    char *server_string;
    SERVER temp_server;

    /* Don't even probe server flagged as in maintenance */
    if (SERVER_IN_MAINT(database->server))
        return;

    /** Store previous status */
    database->mon_prev_status = database->server->status;

    server_transfer_status(&temp_server, database->server);
    server_clear_status(&temp_server, SERVER_RUNNING);
    /* Also clear Joined */
    server_clear_status(&temp_server, SERVER_JOINED);

    connect_result_t rval = mon_connect_to_db(mon, database);
    if (rval != MONITOR_CONN_OK)
    {
        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status(&temp_server, SERVER_AUTH_ERROR);
        }
        else
        {
            server_clear_status(&temp_server, SERVER_AUTH_ERROR);
        }

        database->server->node_id = -1;

        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }

        server_clear_status(&temp_server, SERVER_MASTER);
        server_clear_status(&temp_server, SERVER_MASTER_STICKINESS);
        server_clear_status(&temp_server, SERVER_SLAVE);
        server_transfer_status(database->server, &temp_server);

        return;
    }

    /* If we get this far then we have a working connection */
    server_set_status(&temp_server, SERVER_RUNNING);

    /* get server version string */
    server_string = (char *) mysql_get_server_info(database->con);
    if (server_string)
    {
        server_set_version_string(database->server, server_string);
    }

    /* Check if the the Galera FSM shows this node is joined to the cluster */
    if (mysql_query(database->con, "SHOW STATUS LIKE 'wsrep_local_state'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE 'wsrep_local_state'\". "
                      "Expected 2 columns."
                      " MySQL Version: %s", version_str);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            if (strcmp(row[1], "4") == 0)
                isjoined = 1;

                /* Check if the node is a donor and is using xtrabackup, in this case it can stay alive */
            else if (strcmp(row[1], "2") == 0 && handle->availableWhenDonor == 1)
            {
                if (mysql_query(database->con, "SHOW VARIABLES LIKE 'wsrep_sst_method'") == 0
                    && (result2 = mysql_store_result(database->con)) != NULL)
                {
                    if (mysql_field_count(database->con) < 2)
                    {
                        mysql_free_result(result);
                        mysql_free_result(result2);
                        MXS_ERROR("Unexpected result for \"SHOW VARIABLES LIKE "
                                  "'wsrep_sst_method'\". Expected 2 columns."
                                  " MySQL Version: %s", version_str);
                        return;
                    }
                    while ((row = mysql_fetch_row(result2)))
                    {
                        if (strncmp(row[1], "xtrabackup", 10) == 0)
                            isjoined = 1;
                    }
                    mysql_free_result(result2);
                }
            }
        }
        mysql_free_result(result);
    }

    /* Check the the Galera node index in the cluster */
    if (mysql_query(database->con, "SHOW STATUS LIKE 'wsrep_local_index'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        long local_index = -1;

        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW STATUS LIKE 'wsrep_local_index'\". "
                      "Expected 2 columns."
                      " MySQL Version: %s", version_str);
            return;
        }

        while ((row = mysql_fetch_row(result)))
        {
            local_index = strtol(row[1], NULL, 10);
            if ((errno == ERANGE && (local_index == LONG_MAX
                                     || local_index == LONG_MIN)) || (errno != 0 && local_index == 0))
            {
                local_index = -1;
            }
            database->server->node_id = local_index;
        }
        mysql_free_result(result);
    }

    if (isjoined)
    {
        server_set_status(&temp_server, SERVER_JOINED);
    }
    else
    {
        server_clear_status(&temp_server, SERVER_JOINED);
    }

    /* clear bits for non member nodes */
    if (!SERVER_IN_MAINT(database->server) && (!SERVER_IS_JOINED(&temp_server)))
    {
        database->server->depth = -1;

        /* clear M/S status */
        server_clear_status(&temp_server, SERVER_SLAVE);
        server_clear_status(&temp_server, SERVER_MASTER);

        /* clear master sticky status */
        server_clear_status(&temp_server, SERVER_MASTER_STICKINESS);
    }

    server_transfer_status(database->server, &temp_server);
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
    GALERA_MONITOR *handle;
    MONITOR_SERVERS *ptr;
    size_t nrounds = 0;
    MONITOR_SERVERS *candidate_master = NULL;
    int master_stickiness;
    int is_cluster = 0;
    int log_no_members = 1;
    monitor_event_t evtype;

    spinlock_acquire(&mon->lock);
    handle = (GALERA_MONITOR *) mon->handle;
    spinlock_release(&mon->lock);
    master_stickiness = handle->disableMasterFailback;
    if (mysql_thread_init())
    {
        MXS_ERROR("mysql_thread_init failed in monitor module. Exiting.");
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

        if (nrounds != 0 && ((nrounds * MON_BASE_INTERVAL_MS) % mon->interval) >= MON_BASE_INTERVAL_MS)
        {
            nrounds += 1;
            continue;
        }

        nrounds += 1;

        /* reset cluster members counter */
        is_cluster = 0;

        ptr = mon->databases;

        while (ptr)
        {
            ptr->mon_prev_status = ptr->server->status;

            monitorDatabase(mon, ptr);

            /* Log server status change */
            if (mon_status_changed(ptr))
            {
                MXS_DEBUG("Backend server %s:%d state : %s",
                          ptr->server->name,
                          ptr->server->port,
                          STRSRVSTATUS(ptr->server));
            }

            if (!(SERVER_IS_RUNNING(ptr->server)) ||
                !(SERVER_IS_IN_CLUSTER(ptr->server)))
            {
                dcb_hangup_foreach(ptr->server);
            }

            if (SERVER_IS_DOWN(ptr->server))
            {
                /** Increase this server'e error count */
                dcb_hangup_foreach(ptr->server);
                ptr->mon_err_count += 1;

            }
            else
            {
                /** Reset this server's error count */
                ptr->mon_err_count = 0;
            }

            ptr = ptr->next;
        }

        /*
         * Let's select a master server:
         * it could be the candidate master following MIN(node_id) rule or
         * the server that was master in the previous monitor polling cycle
         * Decision depends on master_stickiness value set in configuration
         */

        /* get the candidate master, following MIN(node_id) rule */
        candidate_master = get_candidate_master(mon);

        /* Select the master, based on master_stickiness */
        if (1 == handle->disableMasterRoleSetting)
        {
            handle->master = NULL;
        }
        else
        {
            handle->master = set_cluster_master(handle->master, candidate_master, master_stickiness);
        }

        ptr = mon->databases;

        while (ptr)
        {
            if (!SERVER_IS_JOINED(ptr->server) || SERVER_IN_MAINT(ptr->server))
            {
                ptr = ptr->next;
                continue;
            }

            if (handle->master)
            {
                if (ptr != handle->master)
                {
                    /* set the Slave role and clear master stickiness */
                    server_clear_set_status(ptr->server, (SERVER_SLAVE|SERVER_MASTER|SERVER_MASTER_STICKINESS), SERVER_SLAVE);
                }
                else
                {
                    if (candidate_master && handle->master->server->node_id != candidate_master->server->node_id)
                    {
                        /* set master role and master stickiness */
                        server_clear_set_status(ptr->server,
                            (SERVER_SLAVE|SERVER_MASTER|SERVER_MASTER_STICKINESS),
                            (SERVER_MASTER|SERVER_MASTER_STICKINESS));
                    }
                    else
                    {
                        /* set master role and clear master stickiness */
                        server_clear_set_status(ptr->server,
                            (SERVER_SLAVE|SERVER_MASTER|SERVER_MASTER_STICKINESS),
                            SERVER_MASTER);
                    }
                }
            }

            is_cluster++;

            ptr = ptr->next;
        }

        if (is_cluster == 0 && log_no_members)
        {
            MXS_ERROR("There are no cluster members");
            log_no_members = 0;
        }
        else
        {
            if (is_cluster > 0 && log_no_members == 0)
            {
                MXS_NOTICE("Found cluster members");
                log_no_members = 1;
            }
        }


        ptr = mon->databases;

        while (ptr)
        {

            /** Execute monitor script if a server state has changed */
            if (mon_status_changed(ptr))
            {
                evtype = mon_get_event_type(ptr);
                if (isGaleraEvent(evtype))
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
 * get candidate master from all nodes
 *
 * The current available rule: get the server with MIN(node_id)
 * node_id comes from 'wsrep_local_index' variable
 *
 * @param	servers The monitored servers list
 * @return	The candidate master on success, NULL on failure
 */
static MONITOR_SERVERS *get_candidate_master(MONITOR* mon)
{
    MONITOR_SERVERS *moitor_servers = mon->databases;
    MONITOR_SERVERS *candidate_master = NULL;
    GALERA_MONITOR* handle = mon->handle;
    long min_id = -1;
    int minval = INT_MAX;
    int currval;
    char* value;
    /* set min_id to the lowest value of moitor_servers->server->node_id */
    while (moitor_servers)
    {
        if (!SERVER_IN_MAINT(moitor_servers->server) && SERVER_IS_JOINED(moitor_servers->server))
        {

            moitor_servers->server->depth = 0;

            if (handle->use_priority && (value = serverGetParameter(moitor_servers->server, "priority")) != NULL)
            {
                currval = atoi(value);
                if (currval < minval && currval > 0)
                {
                    minval = currval;
                    candidate_master = moitor_servers;
                }
            }
            else if (moitor_servers->server->node_id >= 0 &&
                     (!handle->use_priority || /** Server priority disabled*/
                      candidate_master == NULL || /** No candidate chosen */
                      serverGetParameter(candidate_master->server, "priority") == NULL)) /** Candidate has no priority */
            {
                if (min_id < 0 || moitor_servers->server->node_id < min_id)
                {
                    min_id = moitor_servers->server->node_id;
                    candidate_master = moitor_servers;
                }
            }
        }
        moitor_servers = moitor_servers->next;
    }

    return candidate_master;
}

/**
 * set the master server in the cluster
 *
 * master could be the last one from previous monitor cycle Iis running) or
 * the candidate master.
 * The selection is based on the configuration option mapped to master_stickiness
 * The candidate master may change over time due to
 * 'wsrep_local_index' value change in the Galera Cluster
 * Enabling master_stickiness will avoid master change unless a failure is spotted
 *
 * @param	current_master Previous master server
 * @param	candidate_master The candidate master server accordingly to the selection rule
 * @return	The  master node pointer (could be NULL)
 */
static MONITOR_SERVERS *set_cluster_master(MONITOR_SERVERS *current_master, MONITOR_SERVERS *candidate_master, int master_stickiness)
{
    /*
     * if current master is not set or master_stickiness is not enable
     * just return candidate_master.
     */
    if (current_master == NULL || master_stickiness == 0)
    {
        return candidate_master;
    }
    else
    {
        /*
         * if current_master is still a cluster member use it
         *
         */
        if (SERVER_IS_JOINED(current_master->server) && (!SERVER_IN_MAINT(current_master->server)))
        {
            return current_master;
        }
        else
        {
            return candidate_master;
        }
    }
}

/**
 * Disable/Enable the Master failback in a Galera Cluster.
 *
 * A restarted / rejoined node may get back the previous 'wsrep_local_index'
 * from Cluster: if the value is the lowest in the cluster it will be selected as Master
 * This will cause a Master change even if there is no failure.
 * The option if set to 1 will avoid this situation, keeping the current Master (if running) available
 *
 * @param arg           The handle allocated by startMonitor
 * @param disable       To disable it use 1, 0 keeps failback
 */
static void
disableMasterFailback(void *arg, int disable)
{
    GALERA_MONITOR *handle = (GALERA_MONITOR *) arg;
    memcpy(&handle->disableMasterFailback, &disable, sizeof(int));
}

/**
 * Allow a Galera node to be in sync when Donor.
 *
 * When enabled, the monitor will check if the node is using xtrabackup or xtrabackup-v2
 * as SST method. In that case, node will stay as synced.
 *
 * @param arg		The handle allocated by startMonitor
 * @param disable	To allow sync status use 1, 0 for traditional behavior
 */
static void
availableWhenDonor(void *arg, int disable)
{
    GALERA_MONITOR *handle = (GALERA_MONITOR *) arg;
    memcpy(&handle->availableWhenDonor, &disable, sizeof(int));
}

static monitor_event_t galera_events[] =
{
    MASTER_DOWN_EVENT,
    MASTER_UP_EVENT,
    SLAVE_DOWN_EVENT,
    SLAVE_UP_EVENT,
    SERVER_DOWN_EVENT,
    SERVER_UP_EVENT,
    SYNCED_DOWN_EVENT,
    SYNCED_UP_EVENT,
    DONOR_DOWN_EVENT,
    DONOR_UP_EVENT,
    LOST_MASTER_EVENT,
    LOST_SLAVE_EVENT,
    LOST_SYNCED_EVENT,
    LOST_DONOR_EVENT,
    NEW_MASTER_EVENT,
    NEW_SLAVE_EVENT,
    NEW_SYNCED_EVENT,
    NEW_DONOR_EVENT,
    MAX_MONITOR_EVENT
};

/**
 * Check if the Galera monitor is monitoring this event type.
 * @param event Event to check
 * @return True if the event is monitored, false if it is not
 * */
bool isGaleraEvent(monitor_event_t event)
{
    int i;
    for (i = 0; galera_events[i] != MAX_MONITOR_EVENT; i++)
    {
        if (event == galera_events[i])
        {
            return true;
        }
    }
    return false;
}
