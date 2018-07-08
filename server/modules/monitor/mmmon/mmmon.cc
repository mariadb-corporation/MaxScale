/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

MMMonitor::MMMonitor(MXS_MONITOR *monitor)
    : maxscale::MonitorInstanceSimple(monitor)
    , m_id(MXS_MONITOR_DEFAULT_ID)
    , m_detectStaleMaster(false)
{
}

MMMonitor::~MMMonitor()
{
}

// static
MMMonitor* MMMonitor::create(MXS_MONITOR* monitor)
{
    return new MMMonitor(monitor);
}

void MMMonitor::diagnostics(DCB *dcb) const
{
    dcb_printf(dcb, "Detect Stale Master:\t%s\n", (m_detectStaleMaster == 1) ? "enabled" : "disabled");
}

json_t* MMMonitor::diagnostics_json() const
{
    json_t* rval = MonitorInstance::diagnostics_json();
    json_object_set_new(rval, "detect_stale_master", json_boolean(m_detectStaleMaster));
    return rval;
}

bool MMMonitor::configure(const MXS_CONFIG_PARAMETER* params)
{
    m_detectStaleMaster = config_get_bool(params, "detect_stale_master");

    return true;
}

bool MMMonitor::has_sufficient_permissions() const
{
    return check_monitor_permissions(m_monitor, "SHOW SLAVE STATUS");
}

void MMMonitor::update_server_status(MXS_MONITORED_SERVER* monitored_server)
{
    MYSQL_ROW row;
    MYSQL_RES *result;
    int isslave = 0;
    int ismaster = 0;
    unsigned long int server_version = 0;
    char *server_string;


    /* get server version from current server */
    server_version = mysql_get_server_version(monitored_server->con);

    /* get server version string */
    mxs_mysql_set_server_version(monitored_server->con, monitored_server->server);
    server_string = monitored_server->server->version_string;

    /* get server_id form current node */
    if (mxs_mysql_query(monitored_server->con, "SELECT @@server_id") == 0
        && (result = mysql_store_result(monitored_server->con)) != NULL)
    {
        long server_id = -1;

        if (mysql_field_count(monitored_server->con) != 1)
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
            monitored_server->server->node_id = server_id;
        }
        mysql_free_result(result);
    }
    else
    {
        mon_report_query_error(monitored_server);
    }
    /* Check if the Slave_SQL_Running and Slave_IO_Running status is
     * set to Yes
     */

    /* Check first for MariaDB 10.x.x and get status for multimaster replication */
    if (server_version >= 100000)
    {

        if (mxs_mysql_query(monitored_server->con, "SHOW ALL SLAVES STATUS") == 0
            && (result = mysql_store_result(monitored_server->con)) != NULL)
        {
            int i = 0;
            long master_id = -1;

            if (mysql_field_count(monitored_server->con) < 42)
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
            memcpy(&monitored_server->server->master_id, &master_id, sizeof(long));

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
            mon_report_query_error(monitored_server);
        }
    }
    else
    {
        if (mxs_mysql_query(monitored_server->con, "SHOW SLAVE STATUS") == 0
            && (result = mysql_store_result(monitored_server->con)) != NULL)
        {
            long master_id = -1;

            if (mysql_field_count(monitored_server->con) < 40)
            {
                mysql_free_result(result);

                if (server_version < 5 * 10000 + 5 * 100)
                {
                    if (monitored_server->log_version_err)
                    {
                        MXS_ERROR("\"SHOW SLAVE STATUS\" "
                                  " for versions less than 5.5 does not have master_server_id, "
                                  "replication tree cannot be resolved for server %s."
                                  " MySQL Version: %s", monitored_server->server->name, server_string);
                        monitored_server->log_version_err = false;
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
            memcpy(&monitored_server->server->master_id, &master_id, sizeof(long));

            mysql_free_result(result);
        }
        else
        {
            mon_report_query_error(monitored_server);
        }
    }

    /* get variable 'read_only' set by an external component */
    if (mxs_mysql_query(monitored_server->con, "SHOW GLOBAL VARIABLES LIKE 'read_only'") == 0
        && (result = mysql_store_result(monitored_server->con)) != NULL)
    {
        if (mysql_field_count(monitored_server->con) < 2)
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
        mon_report_query_error(monitored_server);
    }

    /* Remove addition info */
    monitor_clear_pending_status(monitored_server, SERVER_WAS_MASTER);

    /* Set the Slave Role */
    /* Set the Master role */
    if (ismaster)
    {
        monitor_clear_pending_status(monitored_server, SERVER_SLAVE);
        monitor_set_pending_status(monitored_server, SERVER_MASTER);
    }
    else if (isslave)
    {
        monitor_set_pending_status(monitored_server, SERVER_SLAVE);
        /* Avoid any possible stale Master state */
        monitor_clear_pending_status(monitored_server, SERVER_MASTER);
    }
    /* Avoid any possible Master/Slave stale state */
    else
    {
        monitor_clear_pending_status(monitored_server, SERVER_SLAVE);
        monitor_clear_pending_status(monitored_server, SERVER_MASTER);
    }
}

void MMMonitor::post_tick()
{
    /* Get Master server pointer */
    MXS_MONITORED_SERVER *root_master = get_current_master();

    /* Update server status from monitor pending status on that server*/

    for(MXS_MONITORED_SERVER *ptr = m_monitor->monitored_servers; ptr; ptr = ptr->next)
    {
        if (!SERVER_IN_MAINT(ptr->server))
        {
            /* If "detect_stale_master" option is On, let's use the previus master */
            if (m_detectStaleMaster && root_master &&
                (!strcmp(ptr->server->address, root_master->server->address) &&
                 ptr->server->port == root_master->server->port) && (ptr->server->status & SERVER_MASTER) &&
                !(ptr->pending_status & SERVER_MASTER))
            {
                /* in this case server->status will not be updated from pending_status */
                MXS_NOTICE("root server [%s:%i] is no longer Master, let's "
                           "use it again even if it could be a stale master, you have "
                           "been warned!", ptr->server->address, ptr->server->port);

                /* Reset the pending_status. */
                ptr->pending_status = ptr->server->status;
                monitor_clear_pending_status(ptr, SERVER_AUTH_ERROR);
                monitor_set_pending_status(ptr, SERVER_RUNNING);

                /* Set the STALE bit for this server in server struct */
                monitor_set_pending_status(ptr, SERVER_WAS_MASTER);
            }
        }
    }
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

        if (ptr->pending_status & SERVER_MASTER)
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
