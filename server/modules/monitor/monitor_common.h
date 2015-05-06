#ifndef _MONITOR_COMMON_HG
#define _MONITOR_COMMON_HG
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
#include	<mysql.h>

/**
 * @file monitor_common.h - The generic monitor structures all monitors use
 *
 * Revision History
 *
 * Date      Who             Description
 * 07/05/15  Markus Makela   Initial Implementation of galeramon.h
 * @endverbatim
 */

#define MONITOR_RUNNING		1
#define MONITOR_STOPPING	2
#define MONITOR_STOPPED		3

#define MONITOR_INTERVAL 10000 // in milliseconds
#define MONITOR_DEFAULT_ID 1UL // unsigned long value
#define MONITOR_MAX_NUM_SLAVES 20 //number of MySQL slave servers associated to a MySQL master server


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

#endif