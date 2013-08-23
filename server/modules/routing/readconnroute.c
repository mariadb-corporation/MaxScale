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
 * The router may also have options associated to it that will limit the
 * choice of backend server. Currently two options are supported, the "master"
 * option will cause the router to only connect to servers marked as masters
 * and the "slave" option will limit connections to routers that are marked
 * as slaves. If neither option is specified the router will connect to either
 * masters or slaves.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/06/2013	Mark Riddoch		Initial implementation
 * 25/06/2013	Mark Riddoch		Addition of checks for current server state
 * 26/06/2013	Mark Riddoch		Use server with least connections since
 * 					startup if the number of current
 * 					connections is the same for two servers
 * 					Addition of master and slave options
 * 27/06/2013	Vilho Raatikka		Added skygw_log_write command as an example
 *					and necessary headers.
 * 17/07/2013	Massimiliano Pinto	Added clientReply routine:
 *					called by backend server to send data to client
 *					Included mysql_client_server_protocol.h
 *					with macros and MySQL commands with MYSQL_ prefix
 *					avoiding any conflict with the standard ones
 *					in mysql.h
 * 22/07/2013	Mark Riddoch		Addition of joined router option for Galera
 * 					clusters
 * 31/07/2013	Massimiliano Pinto	Added a check for candidate server, if NULL return
 * 12/08/2013	Mark Riddoch		Log unsupported router options
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

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>

static char *version_str = "V1.0.2";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	int	routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static	void	diagnostics(ROUTER *instance, DCB *dcb);
static  void    clientReply(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb);

/** The module object definition */
static ROUTER_OBJECT MyObject = { createInstance, newSession, closeSession, routeQuery, diagnostics, clientReply };

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
        skygw_log_write(
                        LOGFILE_MESSAGE,
                        "Initialise readconnroute router module %s.\n", version_str);
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
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 * @param options	An array of options for this query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
INSTANCE	*inst;
SERVER		*server;
int		i, n;

	if ((inst = malloc(sizeof(INSTANCE))) == NULL)
		return NULL;

	memset(&inst->stats, 0, sizeof(ROUTER_STATS));

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
		inst->servers[n]->current_connection_count = 0;
		n++;
	}
	inst->servers[n] = NULL;

	/*
	 * Process the options
	 */
	inst->bitmask = 0;
	inst->bitvalue = 0;
	if (options)
	{
		for (i = 0; options[i]; i++)
		{
			if (!strcasecmp(options[i], "master"))
			{
				inst->bitmask |= (SERVER_MASTER|SERVER_SLAVE);
				inst->bitvalue |= SERVER_MASTER;
			}
			else if (!strcasecmp(options[i], "slave"))
			{
				inst->bitmask |= (SERVER_MASTER|SERVER_SLAVE);
				inst->bitvalue |= SERVER_SLAVE;
			}
			else if (!strcasecmp(options[i], "joined"))
			{
				inst->bitmask |= (SERVER_JOINED);
				inst->bitvalue |= SERVER_JOINED;
			}
			else
			{
                                skygw_log_write(LOGFILE_ERROR,
                                                "Unsupported router option %s for "
                                                "readconnroute\n",
                                                options[i]);
			}
		}
	}

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

        skygw_log_write_flush(
                LOGFILE_TRACE,
                "%lu [newSession] new router session with session "
                "%p, and inst %p.",
                pthread_self(),
                session,
                inst);


	if ((client = (CLIENT_SESSION *)malloc(sizeof(CLIENT_SESSION))) == NULL) {
		return NULL;
	}
	/*
	 * Find a backend server to connect to. This is the extent of the
	 * load balancing algorithm we need to implement for this simple
	 * connection router.
	 */

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
	for (i = 0; inst->servers[i]; i++) {
		if(inst->servers[i]) {
			skygw_log_write(
				LOGFILE_TRACE,
				"Examine server in port %d with %d connections. "
                                "Status is %d, "
				"inst->bitvalue is %d",
				inst->servers[i]->server->port,
				inst->servers[i]->current_connection_count,
				inst->servers[i]->server->status,
				inst->bitmask);
		}

		if (inst->servers[i] &&
                    SERVER_IS_RUNNING(inst->servers[i]->server) &&
                    (inst->servers[i]->server->status & inst->bitmask) == inst->bitvalue)
                {
			/* If no candidate set, set first running server as
			our initial candidate server */
			if (candidate == NULL)
                        {
				candidate = inst->servers[i];
			}
                        else if (inst->servers[i]->current_connection_count <
                                   candidate->current_connection_count)
                        {
				/* This running server has fewer
				connections, set it as a new candidate */
				candidate = inst->servers[i];
			}
                        else if (inst->servers[i]->current_connection_count ==
                                 candidate->current_connection_count &&
                                 inst->servers[i]->server->stats.n_connections <
                                 candidate->server->stats.n_connections)
                        {
				/* This running server has the same number
				of connections currently as the candidate
				but has had fewer connections over time
				than candidate, set this server to candidate*/
				candidate = inst->servers[i];
			}
		}
	}

	/* no candidate server here, clean and return NULL */
	if (!candidate) {
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "%lu [newSession] Couldn't find eligible candidate "
                        "server. Exiting.",
                        pthread_self());
		free(client);
		return NULL;
	}

	/*
	 * We now have the server with the least connections.
	 * Bump the connection count for this server
	 */
	atomic_add(&candidate->current_connection_count, 1);

	client->backend = candidate;

        skygw_log_write(
                LOGFILE_TRACE,
                "%lu [newSession] Selected server in port %d. "
                "Connections : %d\n",
                pthread_self(),
                candidate->server->port,
                candidate->current_connection_count);
        /*
	 * Open a backend connection, putting the DCB for this
	 * connection in the client->dcb
	 */

	if ((client->dcb = dcb_connect(candidate->server, session,
					candidate->server->protocol)) == NULL)
	{
		atomic_add(&candidate->current_connection_count, -1);
                skygw_log_write(
                        LOGFILE_ERROR,
                        "%lu [newSession] Failed to establish connection to "
                        "server in port %d. Exiting.",
                        pthread_self(),
                        candidate->server->port);
		free(client);
		return NULL;
	}

	inst->stats.n_sessions++;

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
bool succp = FALSE;

	/*
	 * Close the connection to the backend
	 */
        skygw_log_write_flush(
                LOGFILE_TRACE,
                "%lu [closeSession] closing session with router_session "
                "%p, and inst %p.",
                pthread_self(),
                session,
                inst);
	succp = session->dcb->func.close(session->dcb);
        if (succp) {
                session->dcb = NULL;
        }
	atomic_add(&session->backend->current_connection_count, -1);
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
INSTANCE	*inst = (INSTANCE *)instance;
CLIENT_SESSION	*session = (CLIENT_SESSION *)router_session;
uint8_t *payload = GWBUF_DATA(queue);
int mysql_command = -1;

	inst->stats.n_queries++;

	mysql_command = MYSQL_GET_COMMAND(payload);

	switch(mysql_command) {
		case MYSQL_COM_CHANGE_USER:
			return session->dcb->func.auth(session->dcb, NULL, session->dcb->session, queue);
		default:
			return session->dcb->func.write(session->dcb, queue);
	}
}

/**
 * Display router diagnostics
 *
 * @param instance	Instance of the router
 * @param dcb		DCB to send diagnostics to
 */
static	void
diagnostics(ROUTER *instance, DCB *dcb)
{
INSTANCE	*inst = (INSTANCE *)instance;
CLIENT_SESSION	*session;
int		i = 0;

	spinlock_acquire(&inst->lock);
	session = inst->connections;
	while (session)
	{
		i++;
		session = session->next;
	}
	spinlock_release(&inst->lock);
	
	dcb_printf(dcb, "\tNumber of router sessions:   	%d\n", inst->stats.n_sessions);
	dcb_printf(dcb, "\tCurrent no. of router sessions:	%d\n", i);
	dcb_printf(dcb, "\tNumber of queries forwarded:   	%d\n", inst->stats.n_queries);
}

/**
 * Client Reply routine
 *
 * The routine will reply to client data from backend server
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       backend_dcb     The backend DCB
 * @param       queue           The GWBUF with reply data
 */
static  void
clientReply(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb)
{
	INSTANCE*       inst = NULL;
	DCB             *client = NULL;
	CLIENT_SESSION* session = NULL;

	inst = (INSTANCE *)instance;
	session = (CLIENT_SESSION *)router_session;

	client = backend_dcb->session->client;

	client->func.write(client, queue);
}
///
