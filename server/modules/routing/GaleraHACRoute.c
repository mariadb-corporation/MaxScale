/*
 * This file is distributed as part of MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
 */

/**
 * @file GaleraHACRoute.c - A connection load balancer for use in a Galera
 * HA environment
 *
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 14/02/2014	Mark Riddoch		Initial implementation as part of
 *					preparing the tutorial
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
#include <dcb.h>
#include <spinlock.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>

extern int lm_enabled_logfiles_bitmask;

static char *version_str = "V1.0.0";

/* The router entry points */
static	ROUTER	*GHACreateInstance(SERVICE *service, char **options);
static	void	*GHANewSession(ROUTER *instance, SESSION *session);
static	void 	GHACloseSession(ROUTER *instance, void *router_session);
static	void 	GHAFreeSession(ROUTER *instance, void *router_session);
static	int	GHARouteQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static	void	GHADiagnostics(ROUTER *instance, DCB *dcb);

static  void    GHAClientReply(
        ROUTER  *instance,
        void    *router_session,
        GWBUF   *queue,
        DCB     *backend_dcb);

static  void    GHAHandleError(
        ROUTER  *instance,
        void    *router_session,
        char    *message,
        DCB     *backend_dcb,
        int     action);

/** The module object definition */
static ROUTER_OBJECT MyObject = {
    GHACreateInstance,
    GHANewSession,
    GHACloseSession,
    GHAFreeSession,
    GHARouteQuery,
    GHADiagnostics,
    GHAClientReply,
    GHAHandleError
};

static bool rses_begin_router_action(
        ROUTER_CLIENT_SES* rses);

static void rses_exit_router_action(
        ROUTER_CLIENT_SES* rses);

static SPINLOCK	instlock;
static ROUTER_INSTANCE *instances;

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
        LOGIF(LM, (skygw_log_write(
                           "Initialise readconnroute router module %s.\n", version_str)));
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
GHACreateInstance(SERVICE *service, char **options)
{
ROUTER_INSTANCE	*inst;
SERVER_REF		*server;
int		i, n;

        if ((inst = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
                return NULL;
        }

	inst->service = service;
	spinlock_init(&inst->lock);

	/*
	 * We need an array of the backend servers in the instance structure so
	 * that we can maintain a count of the number of connections to each
	 * backend server.
	 */
	for (server = service->dbref, n = 0; server; server = server->next)
		n++;

	inst->servers = (BACKEND **)calloc(n + 1, sizeof(BACKEND *));
	if (!inst->servers)
	{
		free(inst);
		return NULL;
	}

	for (server = service->dbref, n = 0; server; server = server->next)
	{
		if ((inst->servers[n] = malloc(sizeof(BACKEND))) == NULL)
		{
			for (i = 0; i < n; i++)
				free(inst->servers[i]);
			free(inst->servers);
			free(inst);
			return NULL;
		}
		inst->servers[n]->server = server->server;
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
			else if (!strcasecmp(options[i], "synced"))
			{
				inst->bitmask |= (SERVER_JOINED);
				inst->bitvalue |= SERVER_JOINED;
			}
			else
			{
                            LOGIF(LE, (skygw_log_write(
                                               LOGFILE_ERROR,
                                               "Warning : Unsupported router "
                                               "option %s for readconnroute.",
                                               options[i])));
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
GHANewSession(ROUTER *instance, SESSION *session)
{
ROUTER_INSTANCE	        *inst = (ROUTER_INSTANCE *)instance;
ROUTER_CLIENT_SES       *client_rses;
BACKEND                 *candidate = NULL;
BACKEND                 *master = NULL;
int                     i;

        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [newSession] new router session with session "
                "%p, and inst %p.",
                pthread_self(),
                session,
                inst)));


	client_rses = (ROUTER_CLIENT_SES *)calloc(1, sizeof(ROUTER_CLIENT_SES));

        if (client_rses == NULL) {
                return NULL;
	}

#if defined(SS_DEBUG)
        client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
        client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

	/**
	 * Find a backend server to connect to. This is the extent of the
	 * load balancing algorithm we need to implement for this simple
	 * connection router.
	 * 
	 * The simple Galera HA connection router assumes the first node
	 * in the lsit that is part of the cluster is the master and the
	 * remainder are slaves.
	 *
	 * We loop over all the servers, the first one we find that is a
	 * member of the cluster we designate as the master. We then
	 * look at the remainder of the servers and find the one with
	 * least connections and make this our candiate slave server.
	 */

	for (i = 0; inst->servers[i]; i++)
	{
		if(inst->servers[i])
		{
			LOGIF(LD, (skygw_log_write(
					LOGFILE_DEBUG,
					"%lu [newSession] Examine server in port %d with "
					"%d connections. Status is %d, "
					"inst->bitvalue is %d",
					pthread_self(),
					inst->servers[i]->server->port,
					inst->servers[i]->current_connection_count,
					inst->servers[i]->server->status,
					inst->bitmask)));
		}
		if (inst->servers[i] &&
       		             SERVER_IS_RUNNING(inst->servers[i]->server) &&
	                    (inst->servers[i]->server->status & SERVER_SYNCED))
		{
			if (master == NULL)
				master = inst->servers[i];
			else
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
	}
	if (candidate == NULL)	/* Only have the master at best */
		candidate = master;

	/*
	 * master is our master server to connect to and candidate is the best
	 * slave to connect to. Now we simply look to see if this router
	 * instance should connect to a master or a slave and set the final
	 * value of candidate to either the master or candidate slave.
	 */
	if (inst->bitvalue & SERVER_MASTER)
	{
		candidate = master;
	}

	/* no candidate server here, clean and return NULL */
	if (!candidate) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to create new routing session. "
                        "Couldn't find eligible candidate server. Freeing "
                        "allocated resources.")));
		free(client_rses);
		return NULL;
	}

	/*
	 * We now have the server with the least connections.
	 * Bump the connection count for this server
	 */
	atomic_add(&candidate->current_connection_count, 1);
	client_rses->backend = candidate;
        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [newSession] Selected server in port %d. "
                "Connections : %d\n",
                pthread_self(),
                candidate->server->port,
                candidate->current_connection_count)));
        /*
	 * Open a backend connection, putting the DCB for this
	 * connection in the client_rses->backend_dcb
	 */
        client_rses->backend_dcb = dcb_connect(candidate->server,
                                      session,
                                      candidate->server->protocol);
        if (client_rses->backend_dcb == NULL)
	{
                atomic_add(&candidate->current_connection_count, -1);
		free(client_rses);
		return NULL;
	}
	inst->stats.n_sessions++;

	/**
         * Add this session to the list of active sessions.
         */
	spinlock_acquire(&inst->lock);
	client_rses->next = inst->connections;
	inst->connections = client_rses;
	spinlock_release(&inst->lock);

        CHK_CLIENT_RSES(client_rses);
                
	return (void *)client_rses;
}

/**
 * @node Unlink from backend server, unlink from router's connection list,
 * and free memory of a router client session.  
 *
 * Parameters:
 * @param router - <usage>
 *          <description>
 *
 * @param router_cli_ses - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details (write detailed description here)
 *
 */
static void GHAFreeSession(
        ROUTER* router_instance,
        void*   router_client_ses)
{
        ROUTER_INSTANCE*   router = (ROUTER_INSTANCE *)router_instance;
        ROUTER_CLIENT_SES* router_cli_ses =
                (ROUTER_CLIENT_SES *)router_client_ses;
        int prev_val;
        
        prev_val = atomic_add(&router_cli_ses->backend->current_connection_count, -1);
        ss_dassert(prev_val > 0);
        
	atomic_add(&router_cli_ses->backend->server->stats.n_current, -1);
	spinlock_acquire(&router->lock);
        
	if (router->connections == router_cli_ses) {
		router->connections = router_cli_ses->next;
        } else {
		ROUTER_CLIENT_SES *ptr = router->connections;
                
		while (ptr != NULL && ptr->next != router_cli_ses) {
			ptr = ptr->next;
                }
                
		if (ptr != NULL) {
			ptr->next = router_cli_ses->next;
                }
	}
	spinlock_release(&router->lock);

        LOGIF(LD, (skygw_log_write_flush(
                LOGFILE_DEBUG,
                "%lu [freeSession] Unlinked router_client_session %p from "
                "router %p and from server on port %d. Connections : %d. ",
                pthread_self(),
                router_cli_ses,
                router,
                router_cli_ses->backend->server->port,
                prev_val-1)));

        free(router_cli_ses);
}


/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static	void 	
GHACloseSession(ROUTER *instance, void *router_session)
{
ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
DCB*              backend_dcb;

        CHK_CLIENT_RSES(router_cli_ses);
        /**
         * Lock router client session for secure read and update.
         */
        if (rses_begin_router_action(router_cli_ses))
        {
                backend_dcb = router_cli_ses->backend_dcb;
                router_cli_ses->backend_dcb = NULL;
                router_cli_ses->rses_closed = true;
                /** Unlock */
                rses_exit_router_action(router_cli_ses);
                
                /**
                 * Close the backend server connection
                 */
                if (backend_dcb != NULL) {
                        CHK_DCB(backend_dcb);
                        dcb_close(backend_dcb);
                }
        }
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
GHARouteQuery(ROUTER *instance, void *router_session, GWBUF *queue)
{
        ROUTER_INSTANCE	  *inst = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        uint8_t           *payload = GWBUF_DATA(queue);
        int               mysql_command;
        int               rc;
        DCB*              backend_dcb;
        bool              rses_is_closed;
       
	inst->stats.n_queries++;
	mysql_command = MYSQL_GET_COMMAND(payload);

        /** Dirty read for quick check if router is closed. */
        if (router_cli_ses->rses_closed)
        {
                rses_is_closed = true;
        }
        else
        {
                /**
                 * Lock router client session for secure read of DCBs
                 */
                rses_is_closed = !(rses_begin_router_action(router_cli_ses));
        }

        if (!rses_is_closed)
        {
                backend_dcb = router_cli_ses->backend_dcb;           
                /** unlock */
                rses_exit_router_action(router_cli_ses);
        }

        if (rses_is_closed ||  backend_dcb == NULL)
        {
                LOGIF(LE, (skygw_log_write(
                        LOGFILE_ERROR,
                        "Error: Failed to route MySQL command %d to backend "
                        "server.",
                        mysql_command)));
                goto return_rc;
        }
        
	switch(mysql_command) {
        case MYSQL_COM_CHANGE_USER:
                rc = backend_dcb->func.auth(
                        backend_dcb,
                        NULL,
                        backend_dcb->session,
                        queue);
		break;
        default:
                rc = backend_dcb->func.write(backend_dcb, queue);
                break;
        }
        
        CHK_PROTOCOL(((MySQLProtocol*)backend_dcb->protocol));
        LOGIF(LD, (skygw_log_write(
                LOGFILE_DEBUG,
                "%lu [readconnroute:routeQuery] Routed command %d to dcb %p "
                "with return value %d.",
                pthread_self(),
                mysql_command,
                backend_dcb,
                rc)));
return_rc:
        return rc;
}

/**
 * Display router diagnostics
 *
 * @param instance	Instance of the router
 * @param dcb		DCB to send diagnostics to
 */
static	void
GHADiagnostics(ROUTER *router, DCB *dcb)
{
ROUTER_INSTANCE	  *router_inst = (ROUTER_INSTANCE *)router;
ROUTER_CLIENT_SES *session;
int		  i = 0;

	spinlock_acquire(&router_inst->lock);
	session = router_inst->connections;
	while (session)
	{
		i++;
		session = session->next;
	}
	spinlock_release(&router_inst->lock);
	
	dcb_printf(dcb, "\tNumber of router sessions:   	%d\n",
                   router_inst->stats.n_sessions);
	dcb_printf(dcb, "\tCurrent no. of router sessions:	%d\n", i);
	dcb_printf(dcb, "\tNumber of queries forwarded:   	%d\n",
                   router_inst->stats.n_queries);
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
GHAClientReply(
        ROUTER *instance,
        void   *router_session,
        GWBUF  *queue,
        DCB    *backend_dcb)
{
	DCB *client = NULL;

	client = backend_dcb->session->client;

	ss_dassert(client != NULL);

	client->func.write(client, queue);
}

/**
 * Error handling routine
 *
 * The routine will handle error occurred in backend.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action     	The action: REPLY, REPLY_AND_CLOSE, NEW_CONNECTION
 *
 */
static  void
GHAHandleError(
        ROUTER *instance,
        void   *router_session,
        char  *message,
        DCB    *backend_dcb,
        int     action)
{
	DCB		*client = NULL;
	ROUTER_OBJECT   *router = NULL;
	SESSION         *session = backend_dcb->session;
	client = backend_dcb->session->client;

	ss_dassert(client != NULL);
}

/** to be inline'd */
/** 
 * @node Acquires lock to router client session if it is not closed.
 *
 * Parameters:
 * @param rses - in, use
 *          
 *
 * @return true if router session was not closed. If return value is true
 * it means that router is locked, and must be unlocked later. False, if
 * router was closed before lock was acquired.
 *
 * 
 * @details (write detailed description here)
 *
 */
static bool rses_begin_router_action(
        ROUTER_CLIENT_SES* rses)
{
        bool succp = false;
        
        CHK_CLIENT_RSES(rses);

        if (rses->rses_closed) {
                goto return_succp;
        }       
        spinlock_acquire(&rses->rses_lock);
        if (rses->rses_closed) {
                spinlock_release(&rses->rses_lock);
                goto return_succp;
        }
        succp = true;
        
return_succp:
        return succp;
}

/** to be inline'd */
/** 
 * @node Releases router client session lock.
 *
 * Parameters:
 * @param rses - <usage>
 *          <description>
 *
 * @return void
 *
 * 
 * @details (write detailed description here)
 *
 */
static void rses_exit_router_action(
        ROUTER_CLIENT_SES* rses)
{
        CHK_CLIENT_RSES(rses);
        spinlock_release(&rses->rses_lock);
}
