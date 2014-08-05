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
 * @file ndbcluster_mon.c - A MySQL cluster SQL node monitor
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 25/07/14	Massimiliano Pinto	Initial implementation
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

static char *version_str = "V1.0.0";

MODULE_INFO	info = {
	MODULE_API_MONITOR,
	MODULE_BETA_RELEASE,
	MONITOR_VERSION,
	"A MySQL cluster SQL node monitor"
};

static	void 	*startMonitor(void *);
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);
static	void	defaultUsers(void *, char *, char *);
static	void	diagnostics(DCB *, void *);
static  void    setInterval(void *, unsigned long);

static MONITOR_OBJECT MyObject = { startMonitor, stopMonitor, registerServer, unregisterServer, defaultUsers, diagnostics, setInterval, NULL, NULL };

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
                           "Initialise the MySQL Cluster Monitor module %s.\n",
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
 * @return A handle to use when interacting with the monitor
 */
static	void 	*
startMonitor(void *arg)
{
MYSQL_MONITOR *handle;

	if (arg != NULL)
	{
		handle = (MYSQL_MONITOR *)arg;
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
 * Diagnostic interface
 *
 * @param dcb	DCB to send output
 * @param arg	The monitor handle
 */
static void
diagnostics(DCB *dcb, void *arg)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;
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
	dcb_printf(dcb, "\tMonitored servers:	");

	db = handle->databases;
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
 * Set the default username and password to use to monitor if the server does not
 * override this.
 *
 * @param arg           The handle allocated by startMonitor
 * @param uname         The default user name
 * @param passwd        The default password
 */
static void
defaultUsers(void *arg, char *uname, char *passwd)
{
MYSQL_MONITOR   *handle = (MYSQL_MONITOR *)arg;

 	if (handle->defaultUser)
		free(handle->defaultUser);
	if (handle->defaultPasswd)
		free(handle->defaultPasswd);
	handle->defaultUser = strdup(uname);
	handle->defaultPasswd = strdup(passwd);
}

/**
 * Monitor an individual server
 *
 * @param database	The database to probe
 */
static void
monitorDatabase(MONITOR_SERVERS	*database, char *defaultUser, char *defaultPasswd)
{
MYSQL_ROW	row;
MYSQL_RES	*result;
int		num_fields;
int		isjoined = 0;
char            *uname = defaultUser, *passwd = defaultPasswd;
unsigned long int	server_version = 0;
char 			*server_string;

	if (database->server->monuser != NULL)
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	if (uname == NULL)
		return;

	/* Don't even probe server flagged as in maintenance */
	if (SERVER_IN_MAINT(database->server))
		return;

	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
		int rc;
		int read_timeout = 1;

		database->con = mysql_init(NULL);
		rc = mysql_options(database->con, MYSQL_OPT_READ_TIMEOUT, (void *)&read_timeout);

		if (mysql_real_connect(database->con, database->server->name,
			uname, dpwd, NULL, database->server->port, NULL, 0) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Monitor was unable to connect to "
				"server %s:%d : \"%s\"",
				database->server->name,
				database->server->port,
				mysql_error(database->con))));
			server_clear_status(database->server, SERVER_RUNNING);
			database->server->node_id = -1;
			free(dpwd);
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

	/* Check if the the SQL node is able to contact one or more data nodes */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_number_of_ready_data_nodes'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		num_fields = mysql_num_fields(result);
		while ((row = mysql_fetch_row(result)))
		{
			if (atoi(row[1]) > 0)
				isjoined = 1;
		}
		mysql_free_result(result);
	}

	/* Check the the SQL node id in the MySQL cluster */
	if (mysql_query(database->con, "SHOW STATUS LIKE 'Ndb_cluster_node_id'") == 0
		&& (result = mysql_store_result(database->con)) != NULL)
	{
		long cluster_node_id = -1;
		num_fields = mysql_num_fields(result);
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

	if (isjoined) {
		server_set_status(database->server, SERVER_NDB);
		database->server->depth = 0;
	} else {
		server_clear_status(database->server, SERVER_NDB);
		database->server->depth = -1;
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
long master_id;

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
		master_id = -1;

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
			monitorDatabase(ptr, handle->defaultUser, handle->defaultPasswd);

			if (ptr->server->status != prev_status ||
				SERVER_IS_DOWN(ptr->server))
			{
				LOGIF(LM, (skygw_log_write_flush(
					LOGFILE_MESSAGE,
					"Backend server %s:%d state : %s",
					ptr->server->name,
					ptr->server->port,
					STRSRVSTATUS(ptr->server))));
			}

			ptr = ptr->next;
		}

		thread_millisleep(handle->interval);
	}
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
