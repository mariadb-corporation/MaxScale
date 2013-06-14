#ifndef _READCONNECTION_H
#define _READCONNECTION_H
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

/*
 * The read connection balancing query module heder file
 *
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 *
 */
#include <dcb.h>

/*
 * Internal structure used to define the set of backend servers we are routing
 * connections to.
 */
typedef struct backend {
	char		*hostname;	/* Server hostname */
	unsigned short	port;		/* Port on which the server listens */
	char		*protocol;	/* Protocol to use to connect to the server */
	int		count;		/* Number of connections to the server */
} BACKEND;

/*
 * The client session structure used within this router.
 */
typedef struct client_session {
	BACKEND		*backend;	/* Backend used by the client session */
	DCB		*dcb;		/* DCB Connection to the backend */
	struct client_session
			*next;
} CLIENT_SESSION;

/*
 * The per instance data for the router.
 */
typedef struct instance {
	SERVICE		*service;	/* Pointer to the service using this router */
	CLIENT_SESSION	*connections;	/* Link list of all the client connections */
	SPINLOCK	lock;		/* Spinlock for the instance data */
	BACKEND		**servers;	/* The set of backend servers for this instance */
	struct instance	*next;
} INSTANCE;
#endif
