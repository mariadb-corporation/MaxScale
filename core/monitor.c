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
 * Date		Who		Description
 * 08/07/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <monitor.h>
#include <spinlock.h>
#include <modules.h>


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
		fprintf(stderr, "Unable to load monitor module '%s'\n", name);
		free(mon->name);
		free(mon);
		return NULL;
	}
	mon->handle = (*mon->module->startMonitor)();

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
