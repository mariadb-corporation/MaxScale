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

typedef struct client_session CLIENT_SESSION;
typedef struct instance       INSTANCE;
/**
 * Internal structure used to define the set of backend servers we are routing
 * connections to. This provides the storage for routing module specific data
 * that is required for each of the backend servers.
 */
typedef struct backend {
        SERVER* server;	/**< The server itself */
        int     count;  /**< Number of connections to the server */
} BACKEND;

/**
 * The client session structure used within this router.
 */
struct client_session {
        BACKEND*        slave;      /**< Slave used by the client session */
        BACKEND*        master;     /**< Master used by the client session */
        DCB*            slaveconn;  /**< Slave connection */
        DCB*            masterconn; /**< Master connection */
        CLIENT_SESSION* next;
};

/**
 * The statistics for this router instance
 */
typedef struct {
	int		n_sessions;	/**< Number sessions created */
	int		n_queries;	/**< Number of queries forwarded */
	int		n_master;	/**< Number of statements sent to master */
	int		n_slave;	/**< Number of statements sent to slave */
	int		n_all;		/**< Number of statements sent to all */
} ROUTER_STATS;


/**
 * The per instance data for the router.
 */
struct instance {
	SERVICE*        service;	 /**< Pointer to the service using this router */
	CLIENT_SESSION* connections; /**< Link list of all the client connections */
	SPINLOCK        lock;	 /**< Spinlock for the instance data */
	BACKEND**       servers; /**< The set of backend servers for this instance */
	BACKEND*        master;  /**< NULL if not known, pointer otherwise */
	ROUTER_STATS    stats;   /**< Statistics for this router */
    INSTANCE*       next;
};


#endif
