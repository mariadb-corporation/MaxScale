#ifndef _MONITOR_H
#define _MONITOR_H
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
#include <server.h>
#include <dcb.h>
#include <resultset.h>

/**
 * @file monitor.h	The interface to the monitor module
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 07/07/13	Mark Riddoch		Initial implementation
 * 25/07/13	Mark Riddoch		Addition of diagnotics
 * 23/05/14	Mark Riddoch		Addition of routine to find monitors by name
 * 23/05/14	Massimiliano Pinto	Addition of defaultId and setInterval
 * 23/06/14	Massimiliano Pinto	Addition of replicationHeartbeat
 * 28/08/14	Massimiliano Pinto	Addition of detectStaleMaster
 * 30/10/14	Massimiliano Pinto	Addition of disableMasterFailback
 * 07/11/14	Massimiliano Pinto	Addition of setNetworkTimeout
 * 19/02/15	Mark Riddoch		Addition of monitorGetList
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
	void 	*(*startMonitor)(void *, void*);
	void	(*stopMonitor)(void *);
	void	(*registerServer)(void *, SERVER *);
	void	(*unregisterServer)(void *, SERVER *);
	void	(*defaultUser)(void *, char *, char *);
	void	(*diagnostics)(DCB *, void *);
	void	(*setInterval)(void *, size_t);
	void	(*setNetworkTimeout)(void *, int, int);
} MONITOR_OBJECT;

/**
 * The monitor API version number. Any change to the monitor module API
 * must change these versions usign the rules defined in modinfo.h
 */
#define	MONITOR_VERSION	{2, 0, 0}

/** Monitor's poll frequency */
#define MON_BASE_INTERVAL_MS 100

/**
 * Monitor state bit mask values
 */
typedef enum 
{
	MONITOR_STATE_ALLOC	= 0x00,
	MONITOR_STATE_RUNNING	= 0x01,
	MONITOR_STATE_STOPPING	= 0x02,
	MONITOR_STATE_STOPPED	= 0x04,
	MONITOR_STATE_FREED	= 0x08
} monitor_state_t;

/**
 * Monitor network timeout types
 */
typedef enum
{
	MONITOR_CONNECT_TIMEOUT	= 0,
	MONITOR_READ_TIMEOUT	= 1,
	MONITOR_WRITE_TIMEOUT	= 2
} monitor_timeouts_t;

#define DEFAULT_CONNECT_TIMEOUT 3
#define DEFAULT_READ_TIMEOUT 1
#define DEFAULT_WRITE_TIMEOUT 2

/**
 * Representation of the running monitor.
 */
typedef struct monitor {
	char		*name;		/**< The name of the monitor module */
	monitor_state_t state;		/**< The state of the monitor */
	MONITOR_OBJECT	*module;	/**< The "monitor object" */
	void		*handle;	/**< Handle returned from startMonitor */
	size_t		interval;	/**< The monitor interval */
	struct monitor	*next;		/**< Next monitor in the linked list */
} MONITOR;

extern MONITOR	*monitor_alloc(char *, char *);
extern void	monitor_free(MONITOR *);
extern MONITOR	*monitor_find(char *);
extern void	monitorAddServer(MONITOR *, SERVER *);
extern void	monitorAddUser(MONITOR *, char *, char *);
extern void	monitorStop(MONITOR *);
extern void	monitorStart(MONITOR *, void*);
extern void	monitorStopAll();
extern void	monitorShowAll(DCB *);
extern void	monitorShow(DCB *, MONITOR *);
extern void	monitorList(DCB *);
extern void     monitorSetInterval (MONITOR *, unsigned long);
extern void     monitorSetNetworkTimeout(MONITOR *, int, int);
extern RESULTSET *monitorGetList();
#endif
