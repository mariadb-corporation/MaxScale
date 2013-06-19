#ifndef _SERVER_H
#define _SERVER_H
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
 * @file service.h
 *
 * The server level definitions within the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The server statistics structure
 *
 */
typedef struct {
	int		n_connections;	/**< Number of connections */
} SERVER_STATS;

/**
 * The SERVER structure defines a backend server. Each server has a name
 * or IP address for the server, a port that the server listens on and
 * the name of a protocol module that is loaded to implement the protocol
 * between the gateway and the server.
 */
typedef struct server {
	char		*name;		/**< Server name/IP address*/
	unsigned short	port;		/**< Port to listen on */
	char		*protocol;	/**< Protocol module to use */
	SERVER_STATS	stats;		/**< The server statistics */
	struct	server	*next;		/**< Next server */
	struct	server	*nextdb;	/**< Next server in lsit attached to a service */
} SERVER;

extern SERVER	*server_alloc(char *, char *, unsigned short);
extern int	server_free(SERVER *);
extern void	printServer(SERVER *);
extern void	printAllServers();
extern void	dprintAllServers();
#endif
