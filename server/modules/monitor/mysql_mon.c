/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */

/**
 * @file mysql_mon.c - A MySQL replication cluster monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/07/13	Mark Riddoch		Initial implementation
 * 11/07/13	Mark Riddoch		Addition of code to check replication
 * 					status
 * 25/07/13	Mark Riddoch		Addition of decrypt for passwords and
 * 					diagnostic interface
 * 20/05/14	Massimiliano Pinto	Addition of support for MariadDB multimaster replication setup.
 *					New server field version_string is updated.
 * 28/05/14	Massimiliano Pinto	Added set Id and configuration options (setInverval)
 *					Parameters are now printed in diagnostics
 * 03/06/14	Mark Ridoch		Add support for maintenance mode
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <monitor.h>
#include <mysqlmon.h>
#include <thread.h>
#include <mysql.h>
#include <mysqld_error.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <dcb.h>
#include <modinfo.h>

extern int lm_enabled_logfiles_bitmask;

static	void	monitorMain(void *);

static char *version_str = "V1.2.0";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_ALPHA_RELEASE,
	MONITOR_VERSION,
	"A MySQL Master/Slave replication monitor"
};

static	void 	*startMonitor(void *);
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);
static	void	defaultUser(void *, char *, char *);
static	void	diagnostics(DCB *, void *);
static  void    setInterval(void *, unsigned long);
static  void    defaultId(void *, unsigned long);
static	void	replicationHeartbeat(void *, int);

static MONITOR_OBJECT MyObject = { startMonitor, stopMonitor, registerServer, unregisterServer, defaultUser, diagnostics, setInterval, defaultId, replicationHeartbeat };

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
	LOGIF(LM, (skygw_log_write(
                           LOGFILE_MESSAGE,
                           "Initialise the MySQL Monitor module %s.",
                           version_str)));
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
static	void 	*
startMonitor(void *arg)
{
MYSQL_MONITOR *handle;

        if (arg)
        {
            handle = arg;	/* Must be a restart */
            handle->shutdown = 0;
        }
        else
        {
            if ((handle = (MYSQL_MONITOR *)malloc(sizeof(MYSQL_MONITOR))) == NULL)
                return NULL;
            handle->databases = NULL;
            handle->shutdown = 0;
            handle->defaultUser = NULL;
            handle->defaultPasswd = NULL;
            handle->id = MONITOR_DEFAULT_ID;
            handle->interval = MONITOR_INTERVAL;
            spinlock_init(&handle->lock);
        }
        handle->tid = (THREAD)thread_start(monitorMain, handle);
        return handle;
}

/**
 * Stop a running monitor
 *
 * @param arg	Handle on thr running monior
 */
static	void	
stopMonitor(void *arg)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;

        handle->shutdown = 1;
        thread_wait((void *)handle->tid);
}

/**
 * Register a server that must be added to the monitored servers for
 * a monitoring module.
 *
 * @param arg	A handle on the running monitor module
 * @param server	The server to add
 */
static	void	
registerServer(void *arg, SERVER *server)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr, *db;

	if ((db = (MONITOR_SERVERS *)malloc(sizeof(MONITOR_SERVERS))) == NULL)
		return;
	db->server = server;
	db->con = NULL;
	db->next = NULL;
	spinlock_acquire(&handle->lock);
	if (handle->databases == NULL)
		handle->databases = db;
	else
	{
		ptr = handle->databases;
		while (ptr->next != NULL)
			ptr = ptr->next;
		ptr->next = db;
	}
	spinlock_release(&handle->lock);
}

/**
 * Remove a server from those being monitored by a monitoring module
 *
 * @param arg	A handle on the running monitor module
 * @param server	The server to remove
 */
static	void	
unregisterServer(void *arg, SERVER *server)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr, *lptr;

	spinlock_acquire(&handle->lock);
	if (handle->databases == NULL)
	{
		spinlock_release(&handle->lock);
		return;
	}
	if (handle->databases->server == server)
	{
		ptr = handle->databases;
		handle->databases = handle->databases->next;
		free(ptr);
	}
	else
	{
		ptr = handle->databases;
		while (ptr->next != NULL && ptr->next->server != server)
			ptr = ptr->next;
		if (ptr->next)
		{
			lptr = ptr->next;
			ptr->next = ptr->next->next;
			free(lptr);
		}
	}
	spinlock_release(&handle->lock);
}

/**
 * Set the default username and password to use to monitor if the server does not
 * override this.
 *
 * @param arg		The handle allocated by startMonitor
 * @param uname		The default user name
 * @param passwd	The default password
 */
static void
defaultUser(void *arg, char *uname, char *passwd)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;

	if (handle->defaultUser)
		free(handle->defaultUser);
	if (handle->defaultPasswd)
		free(handle->defaultPasswd);
	handle->defaultUser = strdup(uname);
	handle->defaultPasswd = strdup(passwd);
}

/**
 * Daignostic interface
 *
 * @param dcb	DCB to print diagnostics
 * @param arg	The monitor handle
 */
static void diagnostics(DCB *dcb, void *arg)
{
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*db;
char		*sep;

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

	dcb_printf(dcb,"\tSampling interval:\t%lu milliseconds\n", handle->interval);
	dcb_printf(dcb,"\tMaxScale MonitorId:\t%lu\n", handle->id);
	dcb_printf(dcb,"\tReplication lag:\t%s\n", (handle->replicationHeartbeat == 1) ? "enabled" : "disabled");
	dcb_printf(dcb, "\tMonitored servers:	");

	db = handle->databases;
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
monitorDatabase(MYSQL_MONITOR *handle, MONITOR_SERVERS *database)
{
<<<<<<< HEAD
MYSQL_ROW	  row;
MYSQL_RES	  *result;
int		  num_fields;
int		  ismaster = 0, isslave = 0;
char		  *uname = defaultUser, *passwd = defaultPasswd;
unsigned long int server_version = 0;
char              *server_string;
static int        conn_err_count;
static int        modval = 10;

        if (database->server->monuser != NULL)
=======
MYSQL_ROW	  row;
MYSQL_RES	  *result;
int		  num_fields;
int		  ismaster = 0, isslave = 0;
char		  *uname = handle->defaultUser, *passwd = handle->defaultPasswd;
unsigned long int server_version = 0;
char 		  *server_string;
unsigned long	  id = handle->id;
int		  replication_heartbeat = handle->replicationHeartbeat;
static int        conn_err_count;
static int        modval = 10;

	if (database->server->monuser != NULL)
>>>>>>> develop
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	
	if (uname == NULL)
		return;
        
<<<<<<< HEAD
=======
	/* Don't probe servers in maintenance mode */
	if (SERVER_IN_MAINT(database->server))
		return;

>>>>>>> develop
	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
                int  rc;
                int  read_timeout = 1;
<<<<<<< HEAD

                database->con = mysql_init(NULL);
=======
		database->con = mysql_init(NULL);
>>>>>>> develop
                rc = mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);
                
		if (mysql_real_connect(database->con,
                                       database->server->name,
                                       uname,
                                       dpwd,
                                       NULL,
                                       database->server->port,
                                       NULL,
                                       0) == NULL)
		{
<<<<<<< HEAD
                        if (conn_err_count%modval == 0)
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Monitor was unable to connect to "
                                        "server %s:%d : \"%s\"",
                                        database->server->name,
                                        database->server->port,
                                        mysql_error(database->con))));
                                conn_err_count = 0;
                                modval += 1;
                        }
                        else
                        {
                                conn_err_count += 1;
                        }
=======
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Monitor was unable to connect to "
                                "server %s:%d : \"%s\"",
                                database->server->name,
                                database->server->port,
                                mysql_error(database->con))));
                        
>>>>>>> develop
			free(dpwd);
			server_clear_status(database->server, SERVER_RUNNING);
                                                
			return;
		}
		free(dpwd);
	}

	/* If we get this far then we have a working connection */
	server_set_status(database->server, SERVER_RUNNING);

	/* get server version from current server */
	server_version = mysql_get_server_version(database->con);

	/* get server version string */
	server_string = (char *)mysql_get_server_info(database->con);
	if (server_string) {
		database->server->server_string = strdup(server_string);
	}

        /* get server_id form current node */
        if (mysql_query(database->con, "SELECT @@server_id") == 0
                && (result = mysql_store_result(database->con)) != NULL)
        {
                long server_id = -1;
                num_fields = mysql_num_fields(result);
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

	/* Check SHOW SLAVE HOSTS - if we get rows then we are a master */
	if (mysql_query(database->con, "SHOW SLAVE HOSTS"))
	{
		if (mysql_errno(database->con) == ER_SPECIFIC_ACCESS_DENIED_ERROR)
		{
			/* Log lack of permission */
		}

		database->server->rlag = -1;
	} else if ((result = mysql_store_result(database->con)) != NULL) {
		num_fields = mysql_num_fields(result);
		while ((row = mysql_fetch_row(result)))
		{
			ismaster = 1;
		}
		mysql_free_result(result);

		if (ismaster && replication_heartbeat == 1) {
			time_t heartbeat;
                        time_t purge_time;
                        char heartbeat_insert_query[128]="";
                        char heartbeat_purge_query[128]="";

			handle->master_id = database->server->node_id;

			/* create the maxscale_schema database */
			if (mysql_query(database->con, "CREATE DATABASE IF NOT EXISTS maxscale_schema")) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: Error creating maxscale_schema database in Master server"
					": %s", mysql_error(database->con))));

                                database->server->rlag = -1;
			}

			/* create repl_heartbeat table in maxscale_schema database */
			if (mysql_query(database->con, "CREATE TABLE IF NOT EXISTS "
					"maxscale_schema.replication_heartbeat "
					"(maxscale_id INT NOT NULL, "
					"master_server_id INT NOT NULL, "
					"master_timestamp INT UNSIGNED NOT NULL, "
					"PRIMARY KEY ( master_server_id, maxscale_id ) ) "
					"ENGINE=MYISAM DEFAULT CHARSET=latin1")) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: Error creating maxscale_schema.replication_heartbeat table in Master server"
					": %s", mysql_error(database->con))));

				database->server->rlag = -1;
			}

			/* auto purge old values after 48 hours*/
			purge_time = time(0) - (3600 * 48);

			sprintf(heartbeat_purge_query, "DELETE FROM maxscale_schema.replication_heartbeat WHERE master_timestamp < %lu", purge_time);

			if (mysql_query(database->con, heartbeat_purge_query)) {
				LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"[mysql_mon]: Error deleting from maxscale_schema.replication_heartbeat table: [%s], %s",
				heartbeat_purge_query,
				mysql_error(database->con))));
			}

			heartbeat = time(0);

			/* set node_ts for master as time(0) */
			database->server->node_ts = heartbeat;

			sprintf(heartbeat_insert_query, "UPDATE maxscale_schema.replication_heartbeat SET master_timestamp = %lu WHERE master_server_id = %i AND maxscale_id = %lu", heartbeat, handle->master_id, id);

			/* Try to insert MaxScale timestamp into master */
			if (mysql_query(database->con, heartbeat_insert_query)) {

				database->server->rlag = -1;

				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: Error updating maxscale_schema.replication_heartbeat table: [%s], %s",
					heartbeat_insert_query,
					mysql_error(database->con))));
			} else {
				if (mysql_affected_rows(database->con) == 0) {
					heartbeat = time(0);
					sprintf(heartbeat_insert_query, "REPLACE INTO maxscale_schema.replication_heartbeat (master_server_id, maxscale_id, master_timestamp ) VALUES ( %i, %lu, %lu)", handle->master_id, id, heartbeat);

					if (mysql_query(database->con, heartbeat_insert_query)) {
	
						database->server->rlag = -1;

						LOGIF(LE, (skygw_log_write_flush(
							LOGFILE_ERROR,
							"[mysql_mon]: Error inserting into maxscale_schema.replication_heartbeat table: [%s], %s",
							heartbeat_insert_query,
							mysql_error(database->con))));
					} else {
						/* Set replication lag to 0 for the master */
						database->server->rlag = 0;

						LOGIF(LD, (skygw_log_write_flush(
							LOGFILE_DEBUG,
							"[mysql_mon]: heartbeat table inserted data for %s:%i", database->server->name, database->server->port)));
					}
				} else {
					/* Set replication lag as 0 for the master */
					database->server->rlag = 0;

					LOGIF(LD, (skygw_log_write_flush(
						LOGFILE_DEBUG,
						"[mysql_mon]: heartbeat table updated for %s:%i", database->server->name, database->server->port)));
				}
			}
		}
	}

	/* Check if the Slave_SQL_Running and Slave_IO_Running status is
	 * set to Yes
	 */

	/* Check first for MariaDB 10.x.x and get status for multimaster replication */
	if (server_version >= 100000) {

		if (mysql_query(database->con, "SHOW ALL SLAVES STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			int i = 0;
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				if (strncmp(row[12], "Yes", 3) == 0
						&& strncmp(row[13], "Yes", 3) == 0) {
					isslave += 1;
				}
				i++;
			}
			mysql_free_result(result);

			if (isslave == i)
				isslave = 1;
			else
				isslave = 0;
		}
	} else {	
		if (mysql_query(database->con, "SHOW SLAVE STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				if (strncmp(row[10], "Yes", 3) == 0
						&& strncmp(row[11], "Yes", 3) == 0)
					isslave = 1;
			}
			mysql_free_result(result);
		}
	}

	/* Get the master_timestamp value from maxscale_schema.replication_heartbeat table */
	if (isslave && replication_heartbeat == 1) {
		time_t heartbeat;
		char select_heartbeat_query[256] = "";

		sprintf(select_heartbeat_query, "SELECT master_timestamp "
			"FROM maxscale_schema.replication_heartbeat "
			"WHERE maxscale_id = %lu AND master_server_id = %i",
		id, handle->master_id);

		/* if there is a master then send the query to the slave with master_id*/
		if (handle->master_id >= 0 && (mysql_query(database->con, select_heartbeat_query) == 0
			&& (result = mysql_store_result(database->con)) != NULL)) {
			num_fields = mysql_num_fields(result);

			while ((row = mysql_fetch_row(result))) {
				int rlag = -1;
				time_t slave_read;

				heartbeat = time(0);
				slave_read = strtoul(row[0], NULL, 10);

				if ((errno == ERANGE && (slave_read == LONG_MAX || slave_read == LONG_MIN)) || (errno != 0 && slave_read == 0)) {
					slave_read = 0;
				}

				if (slave_read) {
					/* set the replication lag */
					rlag = heartbeat - slave_read;
				}

				/* set this node_ts as master_timestamp read from replication_heartbeat table */
				database->server->node_ts = slave_read;

				if (rlag >= 0) {
					/* store rlag only if greater than monitor sampling interval */
					database->server->rlag = (rlag > (handle->interval / 1000)) ? rlag : 0;
				} else {
					database->server->rlag = -1;
				}

				LOGIF(LD, (skygw_log_write_flush(
					LOGFILE_DEBUG,
					"[mysql_mon]: replication heartbeat: "
					"server %s:%i is %i seconds behind master",
					database->server->name,
					database->server->port,
					database->server->rlag)));
			}
			mysql_free_result(result);
		} else {
			database->server->rlag = -1;
			database->server->node_ts = 0;

			if (handle->master_id < 0) {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: error: replication heartbeat: "
					"master_server_id NOT available for %s:%i",
					database->server->name,
					database->server->port)));
			} else {
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"[mysql_mon]: error: replication heartbeat: "
					"failed selecting from hearthbeat table of %s:%i : [%s], %s",
					database->server->name,
					database->server->port,
					select_heartbeat_query,
					mysql_error(database->con))));
			}
		}
	}

	if (ismaster)
	{
		server_set_status(database->server, SERVER_MASTER);
		server_clear_status(database->server, SERVER_SLAVE);
	}
	else if (isslave)
	{
		server_set_status(database->server, SERVER_SLAVE);
		server_clear_status(database->server, SERVER_MASTER);
	}
	if (ismaster == 0 && isslave == 0)
	{
		server_clear_status(database->server, SERVER_SLAVE);
		server_clear_status(database->server, SERVER_MASTER);
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
MYSQL_MONITOR	*handle = (MYSQL_MONITOR *)arg;
MONITOR_SERVERS	*ptr;
static int      err_count;
static int      modval = 10;

	if (mysql_thread_init())
	{
		LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Fatal : mysql_thread_init failed in monitor "
                                   "module. Exiting.\n")));
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
		ptr = handle->databases;
		while (ptr)
		{
                        unsigned int prev_status = ptr->server->status;
                        
<<<<<<< HEAD
			monitorDatabase(ptr, handle->defaultUser, handle->defaultPasswd);
                        
                        if (ptr->server->status != prev_status ||
                                (SERVER_IS_DOWN(ptr->server) && 
                                err_count%modval == 0))
=======
			monitorDatabase(handle, ptr);
                        
                        if (ptr->server->status != prev_status ||
                                SERVER_IS_DOWN(ptr->server))
>>>>>>> develop
                        {
                                LOGIF(LM, (skygw_log_write_flush(
                                        LOGFILE_MESSAGE,
                                        "Backend server %s:%d state : %s",
                                        ptr->server->name,
                                        ptr->server->port,
                                        STRSRVSTATUS(ptr->server))));
<<<<<<< HEAD
                                err_count = 0;
                                modval += 1;
                        }
                        else if (SERVER_IS_DOWN(ptr->server))
                        {
                                err_count += 1;
                        }
=======
                        }
                        
>>>>>>> develop
			ptr = ptr->next;
		}
		thread_millisleep(handle->interval);
	}
}
                        
/**
 * Set the default id to use in the monitor.
 *
 * @param arg           The handle allocated by startMonitor
 * @param id            The id to set in monitor struct
 */
static void
defaultId(void *arg, unsigned long id)
                        {
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->id, &id, sizeof(unsigned long));
                        }
                        
/**
 * Set the monitor sampling interval.
 *
 * @param arg           The handle allocated by startMonitor
 * @param interval      The interval to set in monitor struct, in milliseconds
 */
static void
setInterval(void *arg, unsigned long interval)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->interval, &interval, sizeof(unsigned long));
	}

/**
 * Enable/Disable the MySQL Replication hearbeat, detecting slave lag behind master.
 *
 * @param arg           The handle allocated by startMonitor
 * @param replicationHeartbeat  To enable it 1, disable it with 0
 */
static void
replicationHeartbeat(void *arg, int replicationHeartbeat)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
	memcpy(&handle->replicationHeartbeat, &replicationHeartbeat, sizeof(int));
}
