#ifndef _RWSPLITROUTER_H
#define _RWSPLITROUTER_H
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
 * @file router.h - The read write split router module heder file
 *
 * @verbatim
 * Revision History
 *
 * Tässä mitään historioita..
 *
 * @endverbatim
 */

#include <dcb.h>

/**
 * Internal structure used to define the set of backend servers we are routing
 * connections to. This provides the storage for routing module specific data
 * that is required for each of the backend servers.
 */
typedef struct backend {
        SERVER* backend_server;	     /**< The server itself */
        int     backend_conn_count;  /**< Number of connections to the server */
} BACKEND;

/**
 * The client session structure used within this router.
 */
typedef struct router_client_session {
        BACKEND*        be_slave;   /**< Slave backend used by client session */
        BACKEND*        be_master;  /**< Master backend used by client session */
        DCB*            slave_dcb;  /**< Slave connection */
        DCB*            master_dcb; /**< Master connection */
        struct router_client_session* next;
} ROUTER_CLIENT_SES;

/**
 * The statistics for this router instance
 */
typedef struct {
	int		n_sessions;	/**< Number sessions created */
	int		n_queries;	/**< Number of queries forwarded */
	int		n_master;	/**< Number of stmts sent to master */
	int		n_slave;	/**< Number of stmts sent to slave */
	int		n_all;		/**< Number of stmts sent to all */
} ROUTER_STATS;


/**
 * The per instance data for the router.
 */
typedef struct router_instance {
	SERVICE*                service;     /**< Pointer to service */
	ROUTER_CLIENT_SES*      connections; /**< List of client connections */
	SPINLOCK                lock;	     /**< Lock for the instance data */
	BACKEND**               servers;     /**< Backend servers */
	BACKEND*                master;      /**< NULL or pointer */
        unsigned int	        bitmask;     /**< Bitmask to apply to server->status */
	unsigned int	        bitvalue;    /**< Required value of server->status */
	ROUTER_STATS            stats;       /**< Statistics for this router */
        struct router_instance* next;        /**< Next router on the list */
} ROUTER_INSTANCE;


#endif
