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
#include <dcb.h>

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
 * 21/06/13	Mark Riddoch	Addition of server status flags
 *
 * @endverbatim
 */

/**
 * The server statistics structure
 *
 */
typedef struct {
	int		n_connections;	/**< Number of connections */
	int		n_current;	/**< Current connections */
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
	unsigned int	status;		/**< Status flag bitmap for the server */
	SERVER_STATS	stats;		/**< The server statistics */
	struct	server	*next;		/**< Next server */
	struct	server	*nextdb;	/**< Next server in list attached to a service */
} SERVER;

/**
 * Status bits in the server->status member.
 *
 * These are a bitmap of attributes that may be applied to a server
 */
#define	SERVER_RUNNING	0x0001		/**<< The server is up and running */
#define SERVER_MASTER	0x0002		/**<< The server is a master, i.e. can handle writes */

/**
 * Is the server running - the macro returns true if the server is marked as running
 * regardless of it's state as a master or slave
 */
#define	SERVER_IS_RUNNING(server)	((server)->status & SERVER_RUNNING)
/**
 * Is the server marked as down - the macro returns true if the server is beleived
 * to be inoperable.
 */
#define	SERVER_IS_DOWN(server)		(((server)->status & SERVER_RUNNING) == 0)
/**
 * Is the server a master? The server must be both running and marked as master
 * in order for the macro to return true
 */
#define	SERVER_IS_MASTER(server) \
			(((server)->status & (SERVER_RUNNING|SERVER_MASTER)) == (SERVER_RUNNING|SERVER_MASTER))
/**
 * Is the server a slave? The server must be both running and marked as a slave
 * in order for the macro to return true
 */
#define	SERVER_IS_SLAVE(server)	\
			(((server)->status & (SERVER_RUNNING|SERVER_MASTER)) == SERVER_RUNNING)


extern SERVER	*server_alloc(char *, char *, unsigned short);
extern int	server_free(SERVER *);
extern void	printServer(SERVER *);
extern void	printAllServers();
extern void	dprintAllServers(DCB *);
extern void	dprintServer(DCB *, SERVER *);
extern char	*server_status(SERVER *);
extern void	server_set_status(SERVER *, int);
extern void	server_clear_status(SERVER *, int);
#endif
