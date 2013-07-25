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
 * Date		Who		Description
 * 08/07/13	Mark Riddoch	Initial implementation
 * 11/07/13	Mark Riddoch	Addition of code to check replication
 * 				status
 * 25/07/13	Mark Riddoch	Addition of decrypt for passwords and
 * 				diagnostic interface
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

static	void	monitorMain(void *);

static char *version_str = "V1.0.0";

static	void 	*startMonitor(void *);
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);
static	void	defaultUser(void *, char *, char *);
static	void	diagnostics(DCB *, void *);

static MONITOR_OBJECT MyObject = { startMonitor, stopMonitor, registerServer, unregisterServer, defaultUser, diagnostics };

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
	skygw_log_write(NULL, LOGFILE_MESSAGE, "Initialise the MySQL Monitor module %s.\n",
					version_str);
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
		spinlock_init(&handle->lock);
	}
	thread_start(monitorMain, handle);
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
 * Monitor an individual server
 *
 * @param database	The database to probe
 * @param defaultUser	Default username for the monitor
 * @param defaultPasswd	Default password for the monitor
 */
static void
monitorDatabase(MONITOR_SERVERS	*database, char *defaultUser, char *defaultPasswd)
{
MYSQL_ROW	row;
MYSQL_RES	*result;
int		num_fields;
int		ismaster = 0, isslave = 0;
char		*uname = defaultUser, *passwd = defaultPasswd;

	if (database->server->monuser != NULL)
	{
		uname = database->server->monuser;
		passwd = database->server->monpw;
	}
	if (uname == NULL)
		return;
	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		char *dpwd = decryptPassword(passwd);
		database->con = mysql_init(NULL);
		if (mysql_real_connect(database->con, database->server->name,
			uname, dpwd, NULL, database->server->port, NULL, 0) == NULL)
		{
			free(dpwd);
			server_clear_status(database->server, SERVER_RUNNING);
			return;
		}
		free(dpwd);
	}

	/* If we get this far then we have a working connection */
	server_set_status(database->server, SERVER_RUNNING);

	/* Check SHOW SLAVE HOSTS - if we get rows then we are a master */
	if (mysql_query(database->con, "SHOW SLAVE HOSTS"))
	{
		if (mysql_errno(database->con) == ER_SPECIFIC_ACCESS_DENIED_ERROR)
		{
			/* Log lack of permission */
		}
	}
	else if ((result = mysql_store_result(database->con)) != NULL)
	{
		num_fields = mysql_num_fields(result);
		while ((row = mysql_fetch_row(result)))
		{
			ismaster = 1;
		}
		mysql_free_result(result);
	}

	/* Check if the Slave_SQL_Running and Slave_IO_Running status is
	 * set to Yes
	 */
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

	if (mysql_thread_init())
	{
		skygw_log_write_flush(NULL,
                              LOGFILE_ERROR,
                              "Fatal : mysql_init_thread failed in monitor "
                              "module. Exiting.\n");
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
			monitorDatabase(ptr, handle->defaultUser, handle->defaultPasswd);
			ptr = ptr->next;
		}
		thread_millisleep(10000);
	}
}
