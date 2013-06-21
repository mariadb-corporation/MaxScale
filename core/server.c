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
 * @file server.c  - A representation of a backend server within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 18/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <session.h>
#include <server.h>
#include <spinlock.h>
#include <dcb.h>

static SPINLOCK	server_spin = SPINLOCK_INIT;
static SERVER	*allServers = NULL;

/**
 * Allocate a new server withn the gateway
 *
 *
 * @param servname	The server name
 * @param protocol	The protocol to use to connect to the server
 * @param port		The port to connect to
 *
 * @return		The newly created server or NULL if an error occured
 */
SERVER *
server_alloc(char *servname, char *protocol, unsigned short port)
{
SERVER 	*server;

	if ((server = (SERVER *)malloc(sizeof(SERVER))) == NULL)
		return NULL;
	server->name = strdup(servname);
	server->protocol = strdup(protocol);
	server->port = port;
	memset(&server->stats, sizeof(SERVER_STATS), 0);
	server->status = 0;
	server->nextdb = NULL;

	spinlock_acquire(&server_spin);
	server->next = allServers;
	allServers = server;
	spinlock_release(&server_spin);

	return server;
}


/**
 * Deallocate the specified server
 *
 * @param server	The service to deallocate
 * @return	Returns true if the server was freed
 */
int
server_free(SERVER *server)
{
SERVER *ptr;

	/* First of all remove from the linked list */
	spinlock_acquire(&server_spin);
	if (allServers == server)
	{
		allServers = server->next;
	}
	else
	{
		ptr = allServers;
		while (ptr && ptr->next != server)
		{
			ptr = ptr->next;
		}
		if (ptr)
			ptr->next = server->next;
	}
	spinlock_release(&server_spin);

	/* Clean up session and free the memory */
	free(server->name);
	free(server->protocol);
	free(server);
	return 1;
}

/**
 * Print details of an individual server
 *
 * @param server	Server to print
 */
void
printServer(SERVER *server)
{
	printf("Server %p\n", server);
	printf("\tServer:			%s\n", server->name);
	printf("\tProtocol:		%s\n", server->protocol);
	printf("\tPort:			%d\n", server->port);
}

/**
 * Print all servers
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
printAllServers()
{
SERVER	*ptr;

	spinlock_acquire(&server_spin);
	ptr = allServers;
	while (ptr)
	{
		printServer(ptr);
		ptr = ptr->next;
	}
	spinlock_release(&server_spin);
}

/**
 * Print all servers to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
dprintAllServers(DCB *dcb)
{
SERVER	*ptr;

	spinlock_acquire(&server_spin);
	ptr = allServers;
	while (ptr)
	{
		dcb_printf(dcb, "Server %p\n", ptr);
		dcb_printf(dcb, "\tServer:			%s\n", ptr->name);
		dcb_printf(dcb, "\tProtocol:		%s\n", ptr->protocol);
		dcb_printf(dcb, "\tPort:			%d\n", ptr->port);
		dcb_printf(dcb, "\tNumber of connections:	%d\n", ptr->stats.n_connections);
		ptr = ptr->next;
	}
	spinlock_release(&server_spin);
}

/**
 * Convert a set of  server status flags to a string, the returned
 * string has been malloc'd and must be free'd by the caller
 *
 * @param server The server to return the status of
 * @return A string representation of the status flags
 */
char *
server_status(SERVER *server)
{
char	*status = NULL;

	if ((status = (char *)malloc(200)) == NULL)
		return NULL;
	status[0] = 0;
	if (server->status & SERVER_RUNNING)
		strcat(status, "Running, ");
	else
		strcat(status, "Down, ");
	if (server->status & SERVER_MASTER)
		strcat(status, "Master");
	else
		strcat(status, "Slave");
	return status;
}
