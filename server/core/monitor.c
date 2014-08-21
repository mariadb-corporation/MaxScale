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
 * @file monitor.c  - The monitor module management routines
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/07/13	Mark Riddoch		Initial implementation
 * 23/05/14	Massimiliano Pinto	Addition of monitor_interval parameter
 * 					and monitor id
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

extern int lm_enabled_logfiles_bitmask;

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

	mon->name = strdup(name);
	if ((mon->module = load_module(module, MODULE_MONITOR)) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Error : Unable to load monitor module '%s'.",
                                   name)));
                free(mon->name);
		free(mon);
		return NULL;
	}
	mon->handle = (*mon->module->startMonitor)(NULL);
	mon->state |= MONITOR_STATE_RUNNING;
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

	mon->module->stopMonitor(mon->handle);
	mon->state &= ~MONITOR_STATE_RUNNING;
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
monitorStart(MONITOR *monitor)
{
	monitor->handle = (*monitor->module->startMonitor)(monitor->handle);
	monitor->state |= MONITOR_STATE_RUNNING;
}

/**
 * Stop a given monitor
 *
 * @param monitor	The monitor to stop
 */
void
monitorStop(MONITOR *monitor)
{
	monitor->module->stopMonitor(monitor->handle);
	monitor->state &= ~MONITOR_STATE_RUNNING;
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
	mon->module->registerServer(mon->handle, server);
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
	mon->module->defaultUser(mon->handle, user, passwd);
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
			ptr->module->diagnostics(dcb, ptr->handle);
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
		monitor->module->diagnostics(dcb, monitor->handle);
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
	dcb_printf(dcb, "+----------------------+---------------------\n");
	dcb_printf(dcb, "| %-20s | Status\n", "Monitor");
	dcb_printf(dcb, "+----------------------+---------------------\n");
	while (ptr)
	{
		dcb_printf(dcb, "| %-20s | %s\n", ptr->name,
			ptr->state & MONITOR_STATE_RUNNING
					? "Running" : "Stopped");
		ptr = ptr->next;
	}
	dcb_printf(dcb, "+----------------------+---------------------\n");
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
 * Set the id of the monitor.
 *
 * @param mon		The monitor instance
 * @param id		The id for the monitor
 */

void
monitorSetId(MONITOR *mon, unsigned long id)
{
	if (mon->module->defaultId != NULL) {
		mon->module->defaultId(mon->handle, id);
	}
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
	if (mon->module->setInterval != NULL) {
		mon->interval = interval;
		mon->module->setInterval(mon->handle, interval);
	}
}

/**
 * Enable Replication Heartbeat support in monitor.
 *
 * @param mon		The monitor instance
 * @param interval	The sampling interval in milliseconds
 */
void
monitorSetReplicationHeartbeat(MONITOR *mon, int replication_heartbeat)
{
	if (mon->module->replicationHeartbeat != NULL) {
		mon->module->replicationHeartbeat(mon->handle, replication_heartbeat);
	}
}
