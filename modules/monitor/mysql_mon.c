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

static	void	monitorMain(void *);

static char *version_str = "V1.0.0";

static	void 	*startMonitor();
static	void	stopMonitor(void *);
static	void	registerServer(void *, SERVER *);
static	void	unregisterServer(void *, SERVER *);

static MONITOR_OBJECT MyObject = { startMonitor, stopMonitor, registerServer, unregisterServer };

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
	fprintf(stderr, "Initialise the MySQL Monitor module.\n");
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
startMonitor()
{
MYSQL_MONITOR *handle;

	if ((handle = (MYSQL_MONITOR *)malloc(sizeof(MYSQL_MONITOR))) == NULL)
		return NULL;
	handle->databases = NULL;
	handle->shutdown = 0;
	spinlock_init(&handle->lock);
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
 * Monitor an individual server
 *
 * @param database	The database to probe
 */
static void
monitorDatabase(MONITOR_SERVERS	*database)
{
MYSQL_ROW	row;
MYSQL_RES	*result;
int		num_fields;
int		ismaster = 0, isslave = 0;

	if (database->con == NULL || mysql_ping(database->con) != 0)
	{
		database->con = mysql_init(NULL);
		if (mysql_real_connect(database->con, database->server->name,
			database->server->monuser, database->server->monpw,
				NULL, database->server->port, NULL, 0) == NULL)
		{
			server_clear_status(database->server, SERVER_RUNNING);
			return;
		}
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
	while (1)
	{
		if (handle->shutdown)
		{
			mysql_thread_end();
			return;
		}
		ptr = handle->databases;
		while (ptr)
		{
			monitorDatabase(ptr);
			ptr = ptr->next;
		}
		thread_millisleep(10000);
	}
}
