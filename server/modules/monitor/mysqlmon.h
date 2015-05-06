#ifndef _MYSQLMON_H
#define _MYSQLMON_H
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
#include	<server.h>
#include	<spinlock.h>
#include	<mysql.h>

/**
 * @file mysqlmon.h - The MySQL monitor functionality within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 08/07/13	Mark Riddoch		Initial implementation
 * 26/05/14	Massimiliano	Pinto	Default values for MONITOR_INTERVAL
 * 28/05/14	Massimiliano	Pinto	Addition of new fields in MYSQL_MONITOR struct
 * 24/06/14	Massimiliano	Pinto	Addition of master field in MYSQL_MONITOR struct and MONITOR_MAX_NUM_SLAVES
 * 28/08/14	Massimiliano	Pinto	Addition of detectStaleMaster
 * 30/10/14	Massimiliano	Pinto	Addition of disableMasterFailback
 * 07/11/14	Massimiliano	Pinto	Addition of NetworkTimeout: connect, read, write
 * 20/04/15	Guillaume Lefranc	Addition of availableWhenDonor
 * 22/04/15     Martin Brampton         Addition of disableMasterRoleSetting
 *
 * @endverbatim
 */

/**
 * The linked list of servers that are being monitored by the MySQL 
 * Monitor module.
 */
typedef struct monitor_servers {
	SERVER		*server;	/**< The server being monitored */
	MYSQL		*con;		/**< The MySQL connection */
	int		mon_err_count;
	unsigned int	mon_prev_status;
	unsigned int	pending_status; /**< Pending Status flag bitmap */	
	struct monitor_servers
			*next;		/**< The next server in the list */
} MONITOR_SERVERS;

/**
 * The handle for an instance of a MySQL Monitor module
 */
typedef struct {
	SPINLOCK  lock;			/**< The monitor spinlock */
	pthread_t tid;			/**< id of monitor thread */ 
	int    	  shutdown;		/**< Flag to shutdown the monitor thread */
	int       status;		/**< Monitor status */
	char      *defaultUser;		/**< Default username for monitoring */
	char      *defaultPasswd;	/**< Default password for monitoring */
	unsigned long   interval;	/**< Monitor sampling interval */
	unsigned long         id;	/**< Monitor ID */
	int	replicationHeartbeat;	/**< Monitor flag for MySQL replication heartbeat */
	int	detectStaleMaster;	/**< Monitor flag for MySQL replication Stale Master detection */
	int	disableMasterFailback;	/**< Monitor flag for Galera Cluster Master failback */
	int	availableWhenDonor;	/**< Monitor flag for Galera Cluster Donor availability */
        int     disableMasterRoleSetting; /**< Monitor flag to disable setting master role */
	MONITOR_SERVERS *master;	/**< Master server for MySQL Master/Slave replication */
	MONITOR_SERVERS	*databases;     /**< Linked list of servers to monitor */
	int	connect_timeout;	/**< Connect timeout in seconds for mysql_real_connect */
	int	read_timeout;		/**< Timeout in seconds to read from the server.
					 * There are retries and the total effective timeout value is three times the option value.
					 */
	int	write_timeout;		/**< Timeout in seconds for each attempt to write to the server.
					 * There are retries and the total effective timeout value is two times the option value.
					 */
} MYSQL_MONITOR;

#define MONITOR_RUNNING		1
#define MONITOR_STOPPING	2
#define MONITOR_STOPPED		3

#define MONITOR_INTERVAL 10000 // in milliseconds
#define MONITOR_DEFAULT_ID 1UL // unsigned long value
#define MONITOR_MAX_NUM_SLAVES 20 //number of MySQL slave servers associated to a MySQL master server

#endif
