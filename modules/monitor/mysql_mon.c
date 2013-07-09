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
#include <stdio.h>
#include <stdlib.h>
#include <monitor.h>
#include <mysqlmon.h>
#include <thread.h>
#include <mysql.h>
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
 * @param handle	Handle on thr running monior
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
 * @param handle	A handle on the running monitor module
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
 * @param handle	A handle on the running monitor module
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

	// If we get this far then we have a workign connection
	server_set_status(database->server, SERVER_RUNNING);
	
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

    if (mysql_thread_init()) {
        skygw_log_write_flush(NULL,
                              LOGFILE_ERROR,
                              "Fatal : mysql_init_thread failed in monitor "
                              "module. Exiting.\n");
        return ;
    }                         
	while (1)
	{
		thread_millisleep(1000);

		if (handle->shutdown) {
            mysql_thread_done();
			return;
        }
		ptr = handle->databases;
		while (ptr)
		{
			monitorDatabase(ptr);
			ptr = ptr->next;
		}
	}
}
