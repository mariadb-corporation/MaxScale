#ifndef _MONITOR_H
#define _MONITOR_H
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
#include <server.h>

/**
 * @file monitor.h	The interface to the monitor module
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 07/07/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The "Module Object" for a monitor module.
 *
 * The monitor modules are designed to monitor the backend databases that the gateway 
 * connects to and provide information regarding the status of the databases that
 * is used in the routing decisions.
 *
 * startMonitor is called to start the monitoring process, it is called on the main
 * thread of the gateway and is responsible for creating a thread for the monitor
 * itself to run on. This should use the entry points defined in the thread.h 
 * header file rather than make direct calls to the operating system thrading libraries.
 * The return from startMonitor is a void * handle that will be passed to all other monitor
 * API calls.
 *
 * stopMonitor is responsible for shuting down and destroying a monitor, it is called 
 * with the void * handle that was returned by startMonitor.
 *
 * registerServer is called to register a server that must be monitored with a running
 * monitor. this will be called with the handle returned from the startMonitor call and
 * the SERVER structure that the monitor must update and monitor. The SERVER structure
 * contains the information required to connect to the monitored server.
 *
 * unregisterServer is called to remove a server from the set of servers that need to be
 * monitored.
 */
typedef struct {
	void 	*(*startMonitor)();
	void	(*stopMonitor)(void *);
	void	(*registerServer)(void *, SERVER *);
	void	(*unregisterServer)(void *, SERVER *);
} MONITOR_OBJECT;

/**
 * Representation of the running monitor.
 */
typedef struct monitor {
	char		*name;		/**< The name of the monitor module */
	MONITOR_OBJECT	*module;	/**< The "monitor object" */
	void		*handle;	/**< Handle returned from startMonitor */
	struct monitor	*next;		/**< Next monitor in the linked list */
} MONITOR;

extern MONITOR	*monitor_alloc(char *, char *);
extern void	monitor_free(MONITOR *);
extern void	monitorAddServer(MONITOR *, SERVER *);
#endif
