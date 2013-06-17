#ifndef _SERVICE_H
#define _SERVICE_H
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
 * @file sevice.h
 *
 * The service level definitions within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
struct	server;
struct	router;

typedef struct servprotocol {
	char		*protocol;	/**< Protocol module to load */
	short		port;		/**< Port to listen on */
	struct	servprotocol
			*next;		/**< Next service protocol */
} SERV_PROTOCOL;

typedef struct service {
	char		*name;		/**< The service name */
	SERV_PROTOCOL	*servers;	/**< Linked list of ports and protocols
					 * that this service will listen on.
					 */
	char		*routerModule;	/**< Name of router module to use */
	struct router	*router;	/**< The router we are using */
	struct server	*databases;	/**< The set of servers in the backend */
} SERVICE;

#endif
