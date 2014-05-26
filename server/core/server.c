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
 * Date		Who			Description
 * 18/06/13	Mark Riddoch		Initial implementation
 * 21/05/14	Massimiliano Pinto	Addition of node_id
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
#include <skygw_utils.h>
#include <log_manager.h>

extern int lm_enabled_logfiles_bitmask;

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
	memset(&server->stats, 0, sizeof(SERVER_STATS));
	server->status = SERVER_RUNNING;
	server->nextdb = NULL;
	server->monuser = NULL;
	server->monpw = NULL;
	server->node_id = -1;

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
 * Find an existing server
 *
 * @param	servname	The Server name or address
 * @param	port		The server port
 * @return	The server or NULL if not found
 */
SERVER *
server_find(char *servname, unsigned short port)
{
SERVER 	*server;

	spinlock_acquire(&server_spin);
	server = allServers;
	while (server)
	{
		if (strcmp(server->name, servname) == 0 && server->port == port)
			break;
		server = server->next;
	}
	spinlock_release(&server_spin);
	return server;
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
	printf("\tTotal connections:	%d\n", server->stats.n_connections);
	printf("\tCurrent connections:	%d\n", server->stats.n_current);
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
char	*stat;

	spinlock_acquire(&server_spin);
	ptr = allServers;
	while (ptr)
	{
		dcb_printf(dcb, "Server %p\n", ptr);
		dcb_printf(dcb, "\tServer:			%s\n", ptr->name);
		stat = server_status(ptr);
		dcb_printf(dcb, "\tStatus:               	%s\n", stat);
		free(stat);
		dcb_printf(dcb, "\tProtocol:		%s\n", ptr->protocol);
		dcb_printf(dcb, "\tPort:			%d\n", ptr->port);
		dcb_printf(dcb, "\tNode Id:			%d\n", ptr->node_id);
		dcb_printf(dcb, "\tNumber of connections:	%d\n", ptr->stats.n_connections);
		dcb_printf(dcb, "\tCurrent no. of connections:	%d\n", ptr->stats.n_current);
		ptr = ptr->next;
	}
	spinlock_release(&server_spin);
}

/**
 * Print server details to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active servers within the gateway
 */
void
dprintServer(DCB *dcb, SERVER *server)
{
char	*stat;

	dcb_printf(dcb, "Server %p\n", server);
	dcb_printf(dcb, "\tServer:			%s\n", server->name);
	stat = server_status(server);
	dcb_printf(dcb, "\tStatus:               	%s\n", stat);
	free(stat);
	dcb_printf(dcb, "\tProtocol:		%s\n", server->protocol);
	dcb_printf(dcb, "\tPort:			%d\n", server->port);
	dcb_printf(dcb, "\tNode Id:			%d\n", server->node_id);
	dcb_printf(dcb, "\tNumber of connections:	%d\n", server->stats.n_connections);
	dcb_printf(dcb, "\tCurrent No. of connections:	%d\n", server->stats.n_current);
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
	if (server->status & SERVER_MASTER)
		strcat(status, "Master, ");
	if (server->status & SERVER_SLAVE)
		strcat(status, "Slave, ");
	if (server->status & SERVER_JOINED)
		strcat(status, "Synced, ");
	if (server->status & SERVER_RUNNING)
		strcat(status, "Running");
	else
		strcat(status, "Down");
	return status;
}

/**
 * Set a status bit in the server
 *
 * @param server	The server to update
 * @param bit		The bit to set for the server
 */
void
server_set_status(SERVER *server, int bit)
{
	server->status |= bit;
}

/**
 * Clear a status bit in the server
 *
 * @param server	The server to update
 * @param bit		The bit to clear for the server
 */
void
server_clear_status(SERVER *server, int bit)
{
	server->status &= ~bit;
}

/**
 * Add a user name and password to use for monitoring the
 * state of the server.
 *
 * @param server	The server to update
 * @param user		The user name to use
 * @param passwd	The password of the user
 */
void
serverAddMonUser(SERVER *server, char *user, char *passwd)
{
	server->monuser = strdup(user);
	server->monpw = strdup(passwd);
}

/**
 * Check and update a server definition following a configuration
 * update. Changes will not affect any current connections to this
 * server, however all new connections will use the new settings.
 *
 * If the new settings are different from those already applied to the
 * server then a message will be written to the log.
 *
 * @param server	The server to update
 * @param protocol	The new protocol for the server
 * @param user		The monitor user for the server
 * @param passwd	The password to use for the monitor user
 */
void
server_update(SERVER *server, char *protocol, char *user, char *passwd)
{
	if (!strcmp(server->protocol, protocol))
	{
                LOGIF(LM, (skygw_log_write(
                        LOGFILE_MESSAGE,
                        "Update server protocol for server %s to protocol %s.",
                        server->name,
                        protocol)));
		free(server->protocol);
		server->protocol = strdup(protocol);
	}

        if (user != NULL && passwd != NULL) {
                if (strcmp(server->monuser, user) == 0 ||
                    strcmp(server->monpw, passwd) == 0)
                {
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "Update server monitor credentials for server %s",
				server->name)));
                        free(server->monuser);
                        free(server->monpw);
                        serverAddMonUser(server, user, passwd);
                }
	}
}

