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
 * @file monitor.c  - The monitor module management routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/07/13	Mark Riddoch		Initial implementation
 * 23/05/14	Massimiliano Pinto	Addition of monitor_interval parameter
 * 					and monitor id
 * 30/10/14	Massimiliano Pinto	Addition of disable_master_failback parameter
 * 07/11/14	Massimiliano Pinto	Addition of monitor network timeouts
 * 08/05/15     Markus Makela           Moved common monitor variables to MONITOR struct
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <monitor.h>
#include <spinlock.h>
#include <modules.h>
#include <skygw_utils.h>
#include <log_manager.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static MONITOR	*allMonitors = NULL;
static SPINLOCK	monLock = SPINLOCK_INIT;

/**
 * Allocate a new monitor, load the associated module for the monitor
 * and start execution on the monitor.
 *
 * @param name		The name of the monitor module to load
 * @param module	The module to load
 * @return 	The newly created monitor
 */
MONITOR *
monitor_alloc(char *name, char *module)
{
MONITOR	*mon;

	if ((mon = (MONITOR *)malloc(sizeof(MONITOR))) == NULL)
	{
		return NULL;
	}

	if ((mon->module = load_module(module, MODULE_MONITOR)) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Error : Unable to load monitor module '%s'.",
                                   name)));
		free(mon);
		return NULL;
	}
	mon->state = MONITOR_STATE_ALLOC;
	mon->name = strdup(name);
	mon->handle = NULL;
	mon->databases = NULL;
	mon->password = NULL;
	mon->read_timeout = DEFAULT_READ_TIMEOUT;
	mon->write_timeout = DEFAULT_WRITE_TIMEOUT;
	mon->connect_timeout = DEFAULT_CONNECT_TIMEOUT;
	mon->interval = MONITOR_INTERVAL;
	spinlock_init(&mon->lock);
	spinlock_acquire(&monLock);
	mon->next = allMonitors;
	allMonitors = mon;
	spinlock_release(&monLock);

	return mon;
}

/**
 * Free a monitor, first stop the monitor and then remove the monitor from
 * the chain of monitors and free the memory.
 *
 * @param mon	The monitor to free
 */
void
monitor_free(MONITOR *mon)
{
MONITOR	*ptr;

	mon->module->stopMonitor(mon);
	mon->state = MONITOR_STATE_FREED;
	spinlock_acquire(&monLock);
	if (allMonitors == mon)
		allMonitors = mon->next;
	else
	{
		ptr = allMonitors;
		while (ptr->next && ptr->next != mon)
			ptr = ptr->next;
		if (ptr->next)
			ptr->next = mon->next;
	}
	spinlock_release(&monLock);
	free(mon->name);
	free(mon);
}


/**
 * Start an individual monitor that has previoulsy been stopped.
 *
 * @param monitor The Monitor that should be started
 */
void
monitorStart(MONITOR *monitor, void* params)
{
	spinlock_acquire(&monitor->lock);
	monitor->handle = (*monitor->module->startMonitor)(monitor,params);
	monitor->state = MONITOR_STATE_RUNNING;
	spinlock_release(&monitor->lock);
}

/**
 * Stop a given monitor
 *
 * @param monitor	The monitor to stop
 */
void
monitorStop(MONITOR *monitor)
{
    if(monitor->state != MONITOR_STATE_STOPPED)
    {
	monitor->state = MONITOR_STATE_STOPPING;
	monitor->module->stopMonitor(monitor);
	monitor->state = MONITOR_STATE_STOPPED;
    }
}

/**
 * Shutdown all running monitors
 */
void
monitorStopAll()
{
MONITOR	*ptr;

	spinlock_acquire(&monLock);
	ptr = allMonitors;
	while (ptr)
	{
		monitorStop(ptr);
		ptr = ptr->next;
	}
	spinlock_release(&monLock);
}

/**
 * Add a server to a monitor. Simply register the server that needs to be
 * monitored to the running monitor module.
 *
 * @param mon		The Monitor instance
 * @param server	The Server to add to the monitoring
 */
void
monitorAddServer(MONITOR *mon, SERVER *server)
{
    MONITOR_SERVERS	*ptr, *db;

    if ((db = (MONITOR_SERVERS *)malloc(sizeof(MONITOR_SERVERS))) == NULL)
		return;
	db->server = server;
	db->con = NULL;
	db->next = NULL;
        db->mon_err_count = 0;
	/** Server status is uninitialized */
        db->mon_prev_status = -1;
	/* pending status is updated by get_replication_tree */
	db->pending_status = 0;

	spinlock_acquire(&mon->lock);

	if (mon->databases == NULL)
		mon->databases = db;
	else
	{
		ptr = mon->databases;
		while (ptr->next != NULL)
			ptr = ptr->next;
		ptr->next = db;
	}
	spinlock_release(&mon->lock);
}

/**
 * Add a default user to the monitor. This user is used to connect to the
 * monitored databases but may be overriden on a per server basis.
 *
 * @param mon		The monitor instance
 * @param user		The default username to use when connecting
 * @param passwd	The default password associated to the default user.
 */
void
monitorAddUser(MONITOR *mon, char *user, char *passwd)
{
    mon->user = strdup(user);
    mon->password = strdup(passwd);
}

/**
 * Show all monitors
 *
 * @param dcb	DCB for printing output
 */
void
monitorShowAll(DCB *dcb)
{
MONITOR	*ptr;

	spinlock_acquire(&monLock);
	ptr = allMonitors;
	while (ptr)
	{
		dcb_printf(dcb, "Monitor: %p\n", ptr);
		dcb_printf(dcb, "\tName:		%s\n", ptr->name);
		if (ptr->module->diagnostics)
			ptr->module->diagnostics(dcb, ptr);
		ptr = ptr->next;
	}
	spinlock_release(&monLock);
}

/**
 * Show a single monitor
 *
 * @param dcb	DCB for printing output
 */
void
monitorShow(DCB *dcb, MONITOR *monitor)
{

	dcb_printf(dcb, "Monitor: %p\n", monitor);
	dcb_printf(dcb, "\tName:		%s\n", monitor->name);
	if (monitor->module->diagnostics)
		monitor->module->diagnostics(dcb, monitor);
}

/**
 * List all the monitors
 *
 * @param dcb	DCB for printing output
 */
void
monitorList(DCB *dcb)
{
MONITOR	*ptr;

	spinlock_acquire(&monLock);
	ptr = allMonitors;
	dcb_printf(dcb, "---------------------+---------------------\n");
	dcb_printf(dcb, "%-20s | Status\n", "Monitor");
	dcb_printf(dcb, "---------------------+---------------------\n");
	while (ptr)
	{
		dcb_printf(dcb, "%-20s | %s\n", ptr->name,
			ptr->state & MONITOR_STATE_RUNNING
					? "Running" : "Stopped");
		ptr = ptr->next;
	}
	dcb_printf(dcb, "---------------------+---------------------\n");
	spinlock_release(&monLock);
}

/**
 * Find a monitor by name
 *
 * @param	name	The name of the monitor
 * @return	Pointer to the monitor or NULL
 */
MONITOR *
monitor_find(char *name)
{
MONITOR	*ptr;

	spinlock_acquire(&monLock);
	ptr = allMonitors;
	while (ptr)
	{
		if (!strcmp(ptr->name, name))
			break;
		ptr = ptr->next;
	}
	spinlock_release(&monLock);
	return ptr;
}

/**
 * Set the monitor sampling interval.
 *
 * @param mon		The monitor instance
 * @param interval	The sampling interval in milliseconds
 */
void
monitorSetInterval (MONITOR *mon, unsigned long interval)
{
    mon->interval = interval;
}

/**
 * Set Monitor timeouts for connect/read/write
 *
 * @param mon		The monitor instance
 * @param type		The timeout handling type
 * @param value		The timeout to set
 */
void
monitorSetNetworkTimeout(MONITOR *mon, int type, int value) {
	
    int max_timeout = (int)(mon->interval/1000);
    int new_timeout = max_timeout -1;

    if (new_timeout <= 0)
	new_timeout = DEFAULT_CONNECT_TIMEOUT;

    switch(type) {
    case MONITOR_CONNECT_TIMEOUT:
	if (value < max_timeout) {
	    memcpy(&mon->connect_timeout, &value, sizeof(int));
	} else {
	    memcpy(&mon->connect_timeout, &new_timeout, sizeof(int));
	    LOGIF(LE, (skygw_log_write_flush(
		    LOGFILE_ERROR,
					     "warning : Monitor Connect Timeout %i is greater than monitor interval ~%i seconds"
		    ", lowering to %i seconds", value, max_timeout, new_timeout)));
	}
	break;

    case MONITOR_READ_TIMEOUT:
	if (value < max_timeout) {
	    memcpy(&mon->read_timeout, &value, sizeof(int));
	} else {
	    memcpy(&mon->read_timeout, &new_timeout, sizeof(int));
	    LOGIF(LE, (skygw_log_write_flush(
		    LOGFILE_ERROR,
					     "warning : Monitor Read Timeout %i is greater than monitor interval ~%i seconds"
		    ", lowering to %i seconds", value, max_timeout, new_timeout)));
	}
	break;

    case MONITOR_WRITE_TIMEOUT:
	if (value < max_timeout) {
	    memcpy(&mon->write_timeout, &value, sizeof(int));
	} else {
	    memcpy(&mon->write_timeout, &new_timeout, sizeof(int));
	    LOGIF(LE, (skygw_log_write_flush(
		    LOGFILE_ERROR,
					     "warning : Monitor Write Timeout %i is greater than monitor interval ~%i seconds"
		    ", lowering to %i seconds", value, max_timeout, new_timeout)));
	}
	break;
    default:
	LOGIF(LE, (skygw_log_write_flush(
		LOGFILE_ERROR,
					 "Error : Monitor setNetworkTimeout received an unsupported action type %i", type)));
	break;
    }
}

/**
 * Provide a row to the result set that defines the set of monitors
 *
 * @param set	The result set
 * @param data	The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
monitorRowCallback(RESULTSET *set, void *data)
{
int		*rowno = (int *)data;
int		i = 0;;
char		buf[20];
RESULT_ROW	*row;
MONITOR		*ptr;

	spinlock_acquire(&monLock);
	ptr = allMonitors;
	while (i < *rowno && ptr)
	{
		i++;
		ptr = ptr->next;
	}
	if (ptr == NULL)
	{
		spinlock_release(&monLock);
		free(data);
		return NULL;
	}
	(*rowno)++;
	row = resultset_make_row(set);
	resultset_row_set(row, 0, ptr->name);
	resultset_row_set(row, 1, ptr->state & MONITOR_STATE_RUNNING
                                        ? "Running" : "Stopped");
	spinlock_release(&monLock);
	return row;
}

/**
 * Return a resultset that has the current set of monitors in it
 *
 * @return A Result set
 */
RESULTSET *
monitorGetList()
{
RESULTSET	*set;
int		*data;

	if ((data = (int *)malloc(sizeof(int))) == NULL)
		return NULL;
	*data = 0;
	if ((set = resultset_create(monitorRowCallback, data)) == NULL)
	{
		free(data);
		return NULL;
	}
	resultset_add_column(set, "Monitor", 20, COL_TYPE_VARCHAR);
	resultset_add_column(set, "Status", 10, COL_TYPE_VARCHAR);

	return set;
}
