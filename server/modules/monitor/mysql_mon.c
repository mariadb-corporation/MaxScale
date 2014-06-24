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
 * 17/06/14	Massimiliano Pinto	Addition of getServerByNodeId routine
 *					and first implementation for depth of replication for nodes.
 * 23/06/14	Massimiliano Pinto	Added replication consistency after replication tree computation
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
static  bool    mon_status_changed(MONITOR_SERVERS* mon_srv);
static  bool    mon_print_fail_status(MONITOR_SERVERS* mon_srv);
static	SERVER   *getServerByNodeId(MONITOR_SERVERS *, int);
static	SERVER   *getSlaveOfNodeId(MONITOR_SERVERS *, int);
static MONITOR_SERVERS *get_replication_tree(MYSQL_MONITOR *handle, int num_servers);
static void set_master_heartbeat(MYSQL_MONITOR *handle, MONITOR_SERVERS *database);
static void set_slave_heartbeat(MYSQL_MONITOR *handle, MONITOR_SERVERS *database);

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
            handle->replicationHeartbeat = 0;
            handle->master = NULL;
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
        db->mon_err_count = 0;
        db->mon_prev_status = 0;
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
MYSQL_ROW	  row;
MYSQL_RES	  *result;
int		  num_fields;
int		  ismaster = 0;
int               isslave = 0;
char		  *uname  = handle->defaultUser; 
char              *passwd = handle->defaultPasswd;
unsigned long int server_version = 0;
char 		  *server_string;
unsigned long	  id = handle->id;
int		  replication_heartbeat = handle->replicationHeartbeat;
static int        conn_err_count;

        if (database->server->monuser != NULL)
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	
	if (uname == NULL)
		return;
        
	/* Don't probe servers in maintenance mode */
	if (SERVER_IN_MAINT(database->server))
		return;

        /** Store prevous status */
        database->mon_prev_status = database->server->status;
        
	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
                int  rc;
                int  read_timeout = 1;

                database->con = mysql_init(NULL);

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
                        free(dpwd);
                        
                        if (mon_print_fail_status(database))
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Monitor was unable to connect to "
                                        "server %s:%d : \"%s\"",
                                        database->server->name,
                                        database->server->port,
                                        mysql_error(database->con))));
                        }
                        /** Store current status */
			server_clear_status(database->server, SERVER_RUNNING);

			/* clear M/S status */
			server_clear_status(ptr->server, SERVER_SLAVE);
			server_clear_status(ptr->server, SERVER_MASTER);
                                                
			return;
		}
		free(dpwd);
	}       
        /** Store current status */
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

	/* Check if the Slave_SQL_Running and Slave_IO_Running status is
	 * set to Yes
	 */

	/* Check first for MariaDB 10.x.x and get status for multimaster replication */
	if (server_version >= 100000) {

		if (mysql_query(database->con, "SHOW ALL SLAVES STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			int i = 0;
			int master_id = -1;
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				if (strncmp(row[12], "Yes", 3) == 0
						&& strncmp(row[13], "Yes", 3) == 0) {
					isslave += 1;
                                        master_id = atoi(row[41]);
                                        if (master_id == 0)
                                                master_id = -1;
				}
				i++;
			}
			/* store master_id of current node */
			memcpy(&database->server->master_id, &master_id, sizeof(int));

			mysql_free_result(result);

			/* If all configured slaves are running set this node as slave */
			if (isslave > 0 && isslave == i)
				isslave = 1;
			else
				isslave = 0;
		}
	} else {	
		if (mysql_query(database->con, "SHOW SLAVE STATUS") == 0
			&& (result = mysql_store_result(database->con)) != NULL)
		{
			int master_id = -1;
			num_fields = mysql_num_fields(result);
			while ((row = mysql_fetch_row(result)))
			{
				if (strncmp(row[10], "Yes", 3) == 0
						&& strncmp(row[11], "Yes", 3) == 0) {
					isslave = 1;
					master_id = atoi(row[39]);
					if (master_id == 0)
						master_id = -1;
				}
			}
			/* store master_id of current node */
			memcpy(&database->server->master_id, &master_server_id, sizeof(int));

			mysql_free_result(result);
		}
	}

	/* Please note, the Master Role is assigned in the monitorMain() routine */
	
	/* Set the Slave Role */
	if (isslave)
	{
		server_set_status(database->server, SERVER_SLAVE);
	} else {
		/* If not a Slave then remove all Master/Slave status */
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
int replication_heartbeat = handle->replicationHeartbeat;
int     depth = 0;
int     node_id = -1;
int num_servers=0;
int depth_level = 0;
MONITOR_SERVERS *root_master;

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
			/* monitor current node */
			monitorDatabase(handle, ptr);

			/* reset the slave list of current node */
			*ptr->server->slaves = '\0';

			num_servers++;

                        if (mon_status_changed(ptr))
                        {
                                dcb_call_foreach(ptr->server, DCB_REASON_NOT_RESPONDING);
                        }
                        
                        if (mon_status_changed(ptr) || 
                                mon_print_fail_status(ptr))
                        {
                                LOGIF(LM, (skygw_log_write_flush(
                                        LOGFILE_MESSAGE,
                                        "Backend server %s:%d state : %s",
                                        ptr->server->name,
                                        ptr->server->port,
                                        STRSRVSTATUS(ptr->server))));                                
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
		
		/* Replication depth_level is now set its maximum value: num_servers */
		//depth_level = num_servers;

		/* Compute the replication tree */
		root_master = get_replication_tree(handle, num_servers);


		// do the Replication consistency
		if (replication_heartbeat && root_master && (SERVER_IS_MASTER(root_master->server) || SERVER_IS_RELAY_SERVER(root_master->server))) {
			fprintf(stderr, "Node %d is M %d, S %d, R %d Repl depth %d\n", root_master->server->node_id, SERVER_IS_MASTER(root_master->server), SERVER_IS_SLAVE(root_master->server), SERVER_IS_RELAY_SERVER(root_master->server), root_master->server->depth);

			set_master_heartbeat(handle, root_master);
			ptr = handle->databases;

			while (ptr && (! SERVER_IN_MAINT(ptr->server)) && SERVER_IS_RUNNING(ptr->server))
			{
				fprintf(stderr, "Node %d is M %d, S %d, R %d Repl depth %d\n", ptr->server->node_id, SERVER_IS_MASTER(ptr->server), SERVER_IS_SLAVE(ptr->server), SERVER_IS_RELAY_SERVER(ptr->server), ptr->server->depth);

				if (ptr->server->node_id != root_master->server->node_id && (SERVER_IS_SLAVE(ptr->server) || SERVER_IS_RELAY_SERVER(ptr->server))) {
					fprintf(stderr, "Set Slave heartbeat for %d\n", ptr->server->node_id);
					set_slave_heartbeat(handle, ptr);
				}

				ptr = ptr->next;
			}

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

static bool mon_status_changed(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        
        if (mon_srv->mon_prev_status != mon_srv->server->status)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

static bool mon_print_fail_status(
        MONITOR_SERVERS* mon_srv)
{
        bool succp;
        int errcount = mon_srv->mon_err_count;
        uint8_t modval;
        
        modval = 1<<(MIN(errcount/10, 7));

        if (SERVER_IS_DOWN(mon_srv->server) && errcount%modval == 0)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        return succp;
}

/**
 * Fetch a MySQL node by node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id	The MySQL server_id to fetch
 */
static SERVER *
getServerByNodeId(MONITOR_SERVERS *ptr, int node_id) {
        SERVER *current;
        while (ptr)
        {
                current = ptr->server;
                if (current->node_id == node_id) {
                        return current;
                }
                ptr = ptr->next;
        }
        return NULL;
}

/**
 * Fetch a MySQL slave node from a node_id
 *
 * @param ptr           The list of servers to monitor
 * @param node_id	The MySQL server_id to fetch
 */
static SERVER *getSlaveOfNodeId(MONITOR_SERVERS *ptr, int node_id) {
        SERVER *current;
        while (ptr)
        {
                current = ptr->server;
                if (current->master_id == node_id) {
                        return current;
                }
                ptr = ptr->next;
        }
        return NULL;
}

static void set_master_heartbeat(MYSQL_MONITOR *handle, MONITOR_SERVERS *database) {
	unsigned long id = handle->id;
	time_t heartbeat;
	time_t purge_time;
	char heartbeat_insert_query[128]="";
	char heartbeat_purge_query[128]="";

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

	sprintf(heartbeat_insert_query, "UPDATE maxscale_schema.replication_heartbeat SET master_timestamp = %lu WHERE master_server_id = %i AND maxscale_id = %lu", heartbeat, handle->master->server->node_id, id);

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
			sprintf(heartbeat_insert_query, "REPLACE INTO maxscale_schema.replication_heartbeat (master_server_id, maxscale_id, master_timestamp ) VALUES ( %i, %lu, %lu)", handle->master->server->node_id, id, heartbeat);

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

static void set_slave_heartbeat(MYSQL_MONITOR *handle, MONITOR_SERVERS *database) {
	unsigned long id = handle->id;
	time_t heartbeat;
	char select_heartbeat_query[256] = "";
	MYSQL_ROW row;
	MYSQL_RES *result;
	int  num_fields;

	/* Get the master_timestamp value from maxscale_schema.replication_heartbeat table */

	sprintf(select_heartbeat_query, "SELECT master_timestamp "
		"FROM maxscale_schema.replication_heartbeat "
		"WHERE maxscale_id = %lu AND master_server_id = %i",
		id, handle->master->server->node_id);

	/* if there is a master then send the query to the slave with master_id */
	if (handle->master !=NULL && (mysql_query(database->con, select_heartbeat_query) == 0
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

		if (handle->master->node_id < 0) {
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

/* get the replication tree */
static MONITOR_SERVERS *get_replication_tree(MYSQL_MONITOR *handle, int num_servers) {
	MONITOR_SERVERS *ptr;
	SERVER *current;
	SERVER *backend;
	int depth=0;
	int node_id;
	int root_level;

	ptr = handle->databases;

	while (ptr && (! SERVER_IN_MAINT(ptr->server)) && (SERVER_IS_RUNNING(ptr->server)))
	{
		depth = 0;
		current = ptr->server;

		fprintf(stderr, "Current node to check is %d, depth %d, master %d\n", current->node_id, depth, current->master_id);
			
		node_id = current->master_id;
		if (node_id < 1) {
			SERVER *find_slave;
			find_slave = getSlaveOfNodeId(handle->databases, current->node_id);

			if (find_slave == NULL) {
				current->depth = -1;
				ptr = ptr->next;

				fprintf(stderr, "no slaves for %d, continue\n", current->node_id);

				continue;
			} else {
				current->depth = 0;
				fprintf(stderr, "Found slave for %d: %d. Master level is %d\n", current->node_id, find_slave->node_id, current->depth);
			}
		} else {
			depth++;
		} 

		while(depth <= num_servers) {
			/* set the root master at lowest depth level */
			if (current->depth > -1 && current->depth < root_level) {
				root_level = current->depth;
				handle->master = ptr;
			}
			fprintf(stderr, "Repl depth is %d, servers are %d\n", depth, num_servers);

			fprintf(stderr, "Look for backend %d, depth %d\n", node_id, depth);
			backend = getServerByNodeId(handle->databases, node_id);

			if (backend) {
				node_id = backend->master_id;
			} else {
				node_id = -1;
			}

			if (node_id > 0) {
				fprintf(stderr, "Setting Repl Level to %d for node %d\n", depth+1, current->node_id);
				current->depth = depth + 1;
				depth++;

			} else {
				SERVER *master;
				current->depth = depth;

				fprintf(stderr, "no backend, Setting Repl Level to %d for node %d\n", depth, current->node_id);
				fprintf(stderr, "Node %d/%d is slave for %d: curr depth %d\n", current->node_id, current->depth, current->master_id, depth);
				master = getServerByNodeId(handle->databases, current->master_id);
				if (master && master->node_id > 0) {
					char	this_slave[5]="";
					sprintf(this_slave, "%d", current->node_id);
					if (strlen(master->slaves) && master->slaves[strlen(master->slaves)-1] != ',') {
						strcat(master->slaves, ", ");
					}
					strcat(master->slaves, this_slave);

					master->depth = current->depth -1;
					fprintf(stderr, ">> setting slaves [%s] for master %d, master level %d\n", master->slaves, master->node_id, master->depth);

					server_set_status(master, SERVER_MASTER);
				} else {
					if (current->master_id > 0) {
                                               	fprintf(stderr, "!! The Master %d is outside configuration\n", current->master_id);
                                               	fprintf(stderr, "!! Set the bit: SLAVE_OF_OUTSIDE_MASTER for %d\n", current->node_id);
						//server_set_status(current, SERVER_SLAVE_OF_OUSTIDE_MASTER);
					}
				}
				break;
			}

		}

		ptr = ptr->next;
	}

	return handle->master;
}

