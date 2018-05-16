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
 * @file mm_mon.c - A Multi-Master Multi Muster cluster monitor
 */

#define MXS_MODULE_NAME "mmmon"

#include "mmmon.hh"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/dcb.h>
#include <maxscale/dcb.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/secrets.h>

static void detectStaleMaster(void *, int);
static bool isMySQLEvent(mxs_monitor_event_t event);

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
    MXS_NOTICE("Initialise the Multi-Master Monitor module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "A Multi-Master Multi Master monitor",
        "V1.1.1",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<MMMonitor>::s_api,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"detect_stale_master", MXS_MODULE_PARAM_BOOL, "false"},
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

/*lint +e14 */

MMMonitor::MMMonitor(MXS_MONITOR *monitor)
    : maxscale::MonitorInstance(monitor)
    , m_id(MXS_MONITOR_DEFAULT_ID)
    , m_detectStaleMaster(false)
    , m_master(NULL)
{
}

MMMonitor::~MMMonitor()
{
    ss_dassert(!m_thread);
    ss_dassert(!m_script);
}

// static
MMMonitor* MMMonitor::create(MXS_MONITOR* monitor)
{
    return new MMMonitor(monitor);
}

void MMMonitor::destroy()
{
    delete this;
}

/**
 * Start the instance of the monitor, returning a handle on the monitor.
 *
 * This function creates a thread to execute the actual monitoring.
 *
 * @param arg   The current handle - NULL if first start
 * @return A handle to use when interacting with the monitor
 */
bool MMMonitor::start(const MXS_CONFIG_PARAMETER *params)
{
    bool started = false;

    if (!m_checked)
    {
        if (!check_monitor_permissions(m_monitor, "SHOW SLAVE STATUS"))
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
        m_detectStaleMaster = config_get_bool(params, "detect_stale_master");
        m_script = config_copy_string(params, "script");
        m_events = config_get_enum(params, "events", mxs_monitor_event_enum_values);

        if (thread_start(&m_thread, MMMonitor::main, this, 0) == NULL)
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
 * Stop a running monitor
 *
 * @param arg   Handle on thr running monior
 */
void MMMonitor::stop()
{
    m_shutdown = 1;
    thread_wait(m_thread);
    m_thread = 0;
    m_shutdown = 0;

    MXS_FREE(m_script);
    m_script = NULL;
}

/**
 * Diagnostic interface
 *
 * @param dcb   DCB to print diagnostics
 * @param arg   The monitor handle
 */
void MMMonitor::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Detect Stale Master:\t%s\n", (m_detectStaleMaster == 1) ? "enabled" : "disabled");
}

/**
 * Diagnostic interface
 *
 * @param arg   The monitor handle
 */
json_t* MMMonitor::diagnostics_json() const
{
    json_t* rval = json_object();
    json_object_set_new(rval, "detect_stale_master", json_boolean(m_detectStaleMaster));
    return rval;
}

/**
 * Monitor an individual server
 *
 * @param handle        The MySQL Monitor object
 * @param database  The database to probe
 */
static void
monitorDatabase(MXS_MONITOR* mon, MXS_MONITORED_SERVER *database)
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
    mxs_connect_result_t rval = mon_ping_or_connect_to_db(mon, database);

    if (!mon_connection_is_ok(rval))
    {
        if (mysql_errno(database->con) == ER_ACCESS_DENIED_ERROR)
        {
            server_set_status_nolock(database->server, SERVER_AUTH_ERROR);
            monitor_set_pending_status(database, SERVER_AUTH_ERROR);
        }
        server_clear_status_nolock(database->server, SERVER_RUNNING);
        monitor_clear_pending_status(database, SERVER_RUNNING);

        /* Also clear M/S state in both server and monitor server pending struct */
        server_clear_status_nolock(database->server, SERVER_SLAVE);
        server_clear_status_nolock(database->server, SERVER_MASTER);
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_clear_pending_status(database, SERVER_MASTER);

        /* Clean addition status too */
        server_clear_status_nolock(database->server, SERVER_STALE_STATUS);
        monitor_clear_pending_status(database, SERVER_STALE_STATUS);

        if (mon_status_changed(database) && mon_print_fail_status(database))
        {
            mon_log_connect_error(database, rval);
        }
        return;
    }
    else
    {
        server_clear_status_nolock(database->server, SERVER_AUTH_ERROR);
        monitor_clear_pending_status(database, SERVER_AUTH_ERROR);
    }

    /* Store current status in both server and monitor server pending struct */
    server_set_status_nolock(database->server, SERVER_RUNNING);
    monitor_set_pending_status(database, SERVER_RUNNING);

    /* get server version from current server */
    server_version = mysql_get_server_version(database->con);

    /* get server version string */
    mxs_mysql_set_server_version(database->con, database->server);
    server_string = database->server->version_string;

    /* get server_id form current node */
    if (mxs_mysql_query(database->con, "SELECT @@server_id") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        long server_id = -1;

        if (mysql_field_count(database->con) != 1)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for 'SELECT @@server_id'. Expected 1 column."
                      " MySQL Version: %s", server_string);
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
    else
    {
        mon_report_query_error(database);
    }
    /* Check if the Slave_SQL_Running and Slave_IO_Running status is
     * set to Yes
     */

    /* Check first for MariaDB 10.x.x and get status for multimaster replication */
    if (server_version >= 100000)
    {

        if (mxs_mysql_query(database->con, "SHOW ALL SLAVES STATUS") == 0
            && (result = mysql_store_result(database->con)) != NULL)
        {
            int i = 0;
            long master_id = -1;

            if (mysql_field_count(database->con) < 42)
            {
                mysql_free_result(result);
                MXS_ERROR("\"SHOW ALL SLAVES STATUS\" returned less than the expected"
                          " amount of columns. Expected 42 columns MySQL Version: %s",
                          server_string);
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
        else
        {
            mon_report_query_error(database);
        }
    }
    else
    {
        if (mxs_mysql_query(database->con, "SHOW SLAVE STATUS") == 0
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
                                  " MySQL Version: %s", database->server->name, server_string);
                        database->log_version_err = false;
                    }
                }
                else
                {
                    MXS_ERROR("\"SHOW SLAVE STATUS\" "
                              "returned less than the expected amount of columns. "
                              "Expected 40 columns. MySQL Version: %s", server_string);
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
        else
        {
            mon_report_query_error(database);
        }
    }

    /* get variable 'read_only' set by an external component */
    if (mxs_mysql_query(database->con, "SHOW GLOBAL VARIABLES LIKE 'read_only'") == 0
        && (result = mysql_store_result(database->con)) != NULL)
    {
        if (mysql_field_count(database->con) < 2)
        {
            mysql_free_result(result);
            MXS_ERROR("Unexpected result for \"SHOW GLOBAL VARIABLES LIKE 'read_only'\". "
                      "Expected 2 columns. MySQL Version: %s", server_string);
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
    else
    {
        mon_report_query_error(database);
    }

    /* Remove addition info */
    monitor_clear_pending_status(database, SERVER_STALE_STATUS);

    /* Set the Slave Role */
    /* Set the Master role */
    if (ismaster)
    {
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_set_pending_status(database, SERVER_MASTER);

        /* Set replication depth to 0 */
        database->server->depth = 0;
    }
    else if (isslave)
    {
        monitor_set_pending_status(database, SERVER_SLAVE);
        /* Avoid any possible stale Master state */
        monitor_clear_pending_status(database, SERVER_MASTER);

        /* Set replication depth to 1 */
        database->server->depth = 1;
    }
    /* Avoid any possible Master/Slave stale state */
    else
    {
        monitor_clear_pending_status(database, SERVER_SLAVE);
        monitor_clear_pending_status(database, SERVER_MASTER);
    }
}

/**
 * The entry point for the monitoring module thread
 *
 * @param arg   The handle of the monitor
 */
void MMMonitor::main(void* arg)
{
    static_cast<MMMonitor*>(arg)->main();
}

void MMMonitor::main()
{
    MXS_MONITOR* mon = m_monitor;
    MXS_MONITORED_SERVER *ptr;
    int detect_stale_master = false;
    MXS_MONITORED_SERVER *root_master = NULL;
    size_t nrounds = 0;

    detect_stale_master = m_detectStaleMaster;

    if (mysql_thread_init())
    {
        MXS_ERROR("Fatal : mysql_thread_init failed in monitor module. Exiting.");
        return;
    }

    m_status = MXS_MONITOR_RUNNING;
    load_server_journal(mon, &m_master);

    while (1)
    {
        if (m_shutdown)
        {
            m_status = MXS_MONITOR_STOPPING;
            mysql_thread_end();
            m_status = MXS_MONITOR_STOPPED;
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
            (((nrounds * MXS_MON_BASE_INTERVAL_MS) % mon->interval) >=
             MXS_MON_BASE_INTERVAL_MS) && (!mon->server_pending_changes))
        {
            nrounds += 1;
            continue;
        }
        nrounds += 1;

        lock_monitor_servers(mon);
        servers_status_pending_to_current(mon);

        /* start from the first server in the list */
        ptr = mon->monitored_servers;

        while (ptr)
        {
            /* copy server status into monitor pending_status */
            ptr->pending_status = ptr->server->status;

            /* monitor current node */
            monitorDatabase(mon, ptr);

            if (mon_status_changed(ptr) ||
                mon_print_fail_status(ptr))
            {
                MXS_DEBUG("Backend server [%s]:%d state : %s",
                          ptr->server->address,
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
        root_master = get_current_master();

        /* Update server status from monitor pending status on that server*/

        ptr = mon->monitored_servers;
        while (ptr)
        {
            if (!SERVER_IN_MAINT(ptr->server))
            {
                /* If "detect_stale_master" option is On, let's use the previus master */
                if (detect_stale_master && root_master &&
                    (!strcmp(ptr->server->address, root_master->server->address) &&
                     ptr->server->port == root_master->server->port) && (ptr->server->status & SERVER_MASTER) &&
                    !(ptr->pending_status & SERVER_MASTER))
                {
                    /* in this case server->status will not be updated from pending_status */
                    MXS_NOTICE("root server [%s:%i] is no longer Master, let's "
                               "use it again even if it could be a stale master, you have "
                               "been warned!", ptr->server->address, ptr->server->port);
                    /* Set the STALE bit for this server in server struct */
                    server_set_status_nolock(ptr->server, SERVER_STALE_STATUS);
                }
                else
                {
                    ptr->server->status = ptr->pending_status;
                }
            }
            ptr = ptr->next;
        }

        /**
         * After updating the status of all servers, check if monitor events
         * need to be launched.
         */
        mon_process_state_changes(mon, m_script, m_events);

        mon_hangup_failed_servers(mon);
        servers_status_current_to_pending(mon);
        store_server_journal(mon, m_master);
        release_monitor_servers(mon);
    }
}

/**
 * Enable/Disable the MySQL Replication Stale Master dectection, allowing a previouvsly detected master to still act as a Master.
 * This option must be enabled in order to keep the Master when the replication is stopped or removed from slaves.
 * If the replication is still stopped when MaxSclale is restarted no Master will be available.
 *
 * @param arg       The handle allocated by startMonitor
 * @param enable    To enable it 1, disable it with 0
 */
/* Not used
static void
detectStaleMaster(void *arg, int enable)
{
    MONITOR* mon = (MONITOR*) arg;
    MM_MONITOR *handle = (MM_MONITOR *) mon->handle;
    memcpy(&handle->detectStaleMaster, &enable, sizeof(int));
}
*/

/*******
 * This function returns the master server
 * from a set of MySQL Multi Master monitored servers
 * and returns the root server (that has SERVER_MASTER bit)
 * The server is returned even for servers in 'maintenance' mode.
 *
 * @param handle        The monitor handle
 * @return              The server at root level with SERVER_MASTER bit
 */

MXS_MONITORED_SERVER *MMMonitor::get_current_master()
{
    MXS_MONITOR* mon = m_monitor;
    MXS_MONITORED_SERVER *ptr;

    ptr = mon->monitored_servers;

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
            m_master = ptr;
        }

        ptr = ptr->next;
    }


    /*
     * Return the root master
     */

    if (m_master != NULL)
    {
        /* If the root master is in MAINT, return NULL */
        if (SERVER_IN_MAINT(m_master->server))
        {
            return NULL;
        }
        else
        {
            return m_master;
        }
    }
    else
    {
        return NULL;
    }
}
