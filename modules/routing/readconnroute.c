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
 * @file readconnroute.c - Read Connection Load Balancing Query Router
 *
 * This is the implementation of a simple query router that balances
 * read connections. It assumes the service is configured with a set
 * of slaves and that the application clients already split read and write
 * queries. It offers a service to balance the client read connections
 * over this set of slave servers. It does this once only, at the time
 * the connection is made. It chooses the server that currently has the least
 * number of connections by keeping a count for each server of how
 * many connections the query router has made to the server.
 *
 * When two servers have the same number of current connections the one with
 * the least number of connections since startup will be used.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/06/13	Mark Riddoch	Initial implementation
 * 25/06/13	Mark Riddoch	Addition of checks for current server state
 * 26/06/13	Mark Riddoch	Use server with least connections since
 * 				startup if the number of current
 * 				connections is the same for two servers
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <atomic.h>
#include <spinlock.h>
#include <readconnection.h>
#include <dcb.h>
#include <spinlock.h>

static char *version_str = "V1.0.0";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	int	routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);

/** The module object definition */
static ROUTER_OBJECT MyObject = { createInstance, newSession, closeSession, routeQuery };

static SPINLOCK	instlock;
static INSTANCE *instances;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	fprintf(stderr, "Initial test router module.\n");
	spinlock_init(&instlock);
	instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
	fprintf(stderr, "Returing test router module object.\n");
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service)
{
INSTANCE	*inst;
SERVER		*server;
int		i, n;

	if ((inst = malloc(sizeof(INSTANCE))) == NULL)
		return NULL;

	inst->service = service;
	spinlock_init(&inst->lock);
	inst->connections = NULL;

	/*
	 * We need an array of the backend servers in the instance structure so
	 * that we can maintain a count of the number of connections to each
	 * backend server.
	 */
	for (server = service->databases, n = 0; server; server = server->nextdb)
		n++;

	inst->servers = (BACKEND **)calloc(n + 1, sizeof(BACKEND *));
	if (!inst->servers)
	{
		free(inst);
		return NULL;
	}

	for (server = service->databases, n = 0; server; server = server->nextdb)
	{
		if ((inst->servers[n] = malloc(sizeof(BACKEND))) == NULL)
		{
			for (i = 0; i < n; i++)
				free(inst->servers[i]);
			free(inst->servers);
			free(inst);
			return NULL;
		}
		inst->servers[n]->server = server;
		inst->servers[n]->count = 0;
		n++;
	}
	inst->servers[n] = NULL;

	/*
	 * We have completed the creation of the instance data, so now
	 * insert this router instance into the linked list of routers
	 * that have been created with this module.
	 */
	spinlock_acquire(&instlock);
	inst->next = instances;
	instances = inst;
	spinlock_release(&instlock);

	return (ROUTER *)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
INSTANCE	*inst = (INSTANCE *)instance;
CLIENT_SESSION	*client;
BACKEND		*candidate = NULL;
int		i;

	if ((client = (CLIENT_SESSION *)malloc(sizeof(CLIENT_SESSION))) == NULL)
	{
		return NULL;
	}
	/*
	 * Find a backend server to connect to. This is the extent of the
	 * load balancing algorithm we need to implement for this simple
	 * connection router.
	 */

	/* First find a running server to set as our initial candidate server */
	for (i = 0; inst->servers[i]; i++)
	{
		if (inst->servers[i] && SERVER_IS_RUNNING(inst->servers[i]->server))
		{
			candidate = inst->servers[i];
			break;
		}
	}

	/*
	 * Loop over all the servers and find any that have fewer connections than our
	 * candidate server.
	 *
	 * If a server has less connections than the current candidate we mark this
	 * as the new candidate to connect to.
	 *
	 * If a server has the same number of connections currently as the candidate
	 * and has had less connections over time than the candidate it will also
	 * become the new candidate. This has the effect of spreading the connections
	 * over different servers during periods of very low load.
	 */
	for (i = 1; inst->servers[i]; i++)
	{
		if (inst->servers[i] && SERVER_IS_RUNNING(inst->servers[i]->server))
		{
			if (inst->servers[i]->count < candidate->count)
			{
				candidate = inst->servers[i];
			}
			else if (inst->servers[i]->count == candidate->count &&
					inst->servers[i]->server->stats.n_connections
						 < candidate->server->stats.n_connections)
			{
				candidate = inst->servers[i];
			}
		}
	}

	/*
	 * We now have the server with the least connections.
	 * Bump the connection count for this server
	 */
	atomic_add(&candidate->count, 1);

	client->backend = candidate;

	/*
	 * Open a backend connection, putting the DCB for this
	 * connection in the client->dcb
	 */

	if ((client->dcb = dcb_connect(candidate->server, session,
					candidate->server->protocol)) == NULL)
	{
		atomic_add(&candidate->count, -1);
		free(client);
		return NULL;
	}

	/* Add this session to the list of active sessions */
	spinlock_acquire(&inst->lock);
	client->next = inst->connections;
	inst->connections = client;
	spinlock_release(&inst->lock);
	return (void *)client;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *router_session)
{
INSTANCE	*inst = (INSTANCE *)instance;
CLIENT_SESSION	*session = (CLIENT_SESSION *)router_session;

	/*
	 * Close the connection to the backend
	 */
	session->dcb->func.close(session->dcb);
	atomic_add(&session->backend->count, -1);
	atomic_add(&session->backend->server->stats.n_current, -1);

	spinlock_acquire(&inst->lock);
	if (inst->connections == session)
		inst->connections = session->next;
	else
	{
		CLIENT_SESSION *ptr = inst->connections;
		while (ptr && ptr->next != session)
			ptr = ptr->next;
		if (ptr)
			ptr->next = session->next;
	}
	spinlock_release(&inst->lock);

	/*
	 * We are no longer in the linked list, free
	 * all the memory and other resources associated
	 * to the client session.
	 */
	free(session);
}

/**
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance		The router instance
 * @param router_session	The router session returned from the newSession call
 * @param queue			The queue of data buffers to route
 * @return The number of bytes sent
 */
static	int	
routeQuery(ROUTER *instance, void *router_session, GWBUF *queue)
{
CLIENT_SESSION	*session = (CLIENT_SESSION *)router_session;

	return session->dcb->func.write(session->dcb, queue);
}
