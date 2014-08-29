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
 * 04/09/2013	Massimiliano Pinto	Added client NULL check in clientReply
 * 22/10/2013	Massimiliano Pinto	errorReply called from backend, for client error reply
 *					or take different actions such as open a new backend connection
 * 20/02/2014	Massimiliano Pinto	If router_options=slave, route traffic to master if no slaves available
 * 06/03/2014	Massimiliano Pinto	Server connection counter is now updated in closeSession
 * 24/06/2014	Massimiliano Pinto	New rules for selecting the Master server
 * 27/06/2014	Mark Riddoch		Addition of server weighting
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
#include <modinfo.h>

#include <skygw_types.h>
#include <skygw_utils.h>
#include <log_manager.h>

#include <mysql_client_server_protocol.h>

extern int lm_enabled_logfiles_bitmask;

MODULE_INFO 	info = {
	MODULE_API_ROUTER,
	MODULE_BETA_RELEASE,
	ROUTER_VERSION,
	"A connection based router to load balance based on connections"
};

static char *version_str = "V1.1.0";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	void 	freeSession(ROUTER *instance, void *router_session);
static	int	routeQuery(ROUTER *instance, void *router_session, GWBUF *queue);
static	void	diagnostics(ROUTER *instance, DCB *dcb);
static  void    clientReply(
        ROUTER  *instance,
        void    *router_session,
        GWBUF   *queue,
        DCB     *backend_dcb);
static  void             handleError(
        ROUTER           *instance,
        void             *router_session,
        GWBUF            *errbuf,
        DCB              *backend_dcb,
        error_action_t   action,
        bool             *succp);
static  uint8_t getCapabilities (ROUTER* inst, void* router_session);


/** The module object definition */
static ROUTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostics,
    clientReply,
    handleError,
    getCapabilities
};

static bool rses_begin_locked_router_action(
        ROUTER_CLIENT_SES* rses);

static void rses_end_locked_router_action(
        ROUTER_CLIENT_SES* rses);

static BACKEND *get_root_master(
	BACKEND **servers);

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
                           LOGFILE_MESSAGE,
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
createInstance(SERVICE *service, char **options)
{
ROUTER_INSTANCE	*inst;
SERVER		*server;
int		i, n;
BACKEND		*backend;
char		*weightby;

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
		inst->servers[n]->weight = 1000;
		n++;
	}
	inst->servers[n] = NULL;

	if ((weightby = serviceGetWeightingParameter(service)) != NULL)
	{
		int total = 0;
		for (n = 0; inst->servers[n]; n++)
		{
			backend = inst->servers[n];
			total += atoi(serverGetParameter(backend->server,
						weightby));
		}
		if (total == 0)
		{
			LOGIF(LE, (skygw_log_write(LOGFILE_ERROR,
				"WARNING: Weighting Parameter for service '%s' "
				"will be ignored as no servers have values "
				"for the parameter '%s'.\n",
				service->name, weightby)));
		}
		else
		{
			for (n = 0; inst->servers[n]; n++)
			{
				int perc;
				backend = inst->servers[n];
				perc = (atoi(serverGetParameter(backend->server,
						weightby)) * 1000) / total;
				if (perc == 0)
					perc = 1;
				backend->weight = perc;
				if (perc == 0)
				{
					LOGIF(LE, (skygw_log_write(
							LOGFILE_ERROR,
						"Server '%s' has no value "
						"for weighting parameter '%s', "
						"no queries will be routed to "
						"this server.\n",
						server->unique_name,
						weightby)));
				}
		
			}
		}
	}

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
			else if (!strcasecmp(options[i], "ndb"))
			{
				inst->bitmask |= (SERVER_NDB);
				inst->bitvalue |= SERVER_NDB;
			}
			else
			{
                            LOGIF(LM, (skygw_log_write(
                                          LOGFILE_MESSAGE,
                                           "* Warning : Unsupported router "
                                           "option \'%s\' for readconnroute. "
                                           "Expected router options are "
                                           "[slave|master|synced|ndb]",
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
newSession(ROUTER *instance, SESSION *session)
{
ROUTER_INSTANCE	        *inst = (ROUTER_INSTANCE *)instance;
ROUTER_CLIENT_SES       *client_rses;
BACKEND                 *candidate = NULL;
int                     i;
BACKEND *master_host = NULL;

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
         * Find the Master host from available servers
	 */
        master_host = get_root_master(inst->servers);

	/**
	 * Find a backend server to connect to. This is the extent of the
	 * load balancing algorithm we need to implement for this simple
	 * connection router.
	 */

	/*
	 * Loop over all the servers and find any that have fewer connections
         * than the candidate server.
	 *
	 * If a server has less connections than the current candidate we mark this
	 * as the new candidate to connect to.
	 *
	 * If a server has the same number of connections currently as the candidate
	 * and has had less connections over time than the candidate it will also
	 * become the new candidate. This has the effect of spreading the
         * connections over different servers during periods of very low load.
	 */
	for (i = 0; inst->servers[i]; i++) {
		if(inst->servers[i]) {
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

		if (SERVER_IN_MAINT(inst->servers[i]->server))
			continue;

		/* Check server status bits against bitvalue from router_options */
		if (inst->servers[i] &&
			SERVER_IS_RUNNING(inst->servers[i]->server) &&
			(inst->servers[i]->server->status & inst->bitmask & inst->bitvalue))
                {
			if (master_host) {
				if (inst->servers[i] == master_host && (inst->bitvalue & SERVER_SLAVE)) {
					/* skip root Master here, as it could also be slave of an external server
					 * that is not in the configuration.
					 * Intermediate masters (Relay Servers) are also slave and will be selected
					 * as Slave(s)
			 		 */

					continue;
				}
				if (inst->servers[i] == master_host && (inst->bitvalue & SERVER_MASTER)) {
					/* If option is "master" return only the root Master as there
					 * could be intermediate masters (Relay Servers)
					 * and they must not be selected.
			 	 	 */

					candidate = master_host;
					break;
				}
			} else {
					/* master_host is NULL, no master server.
					 * If requested router_option is 'master'
					 * candidate wll be NULL.
					 */
					if (inst->bitvalue & SERVER_MASTER) {
                                                candidate = NULL;
                                                break;
					}
			}

			/* If no candidate set, set first running server as
			our initial candidate server */
			if (candidate == NULL)
                        {
				candidate = inst->servers[i];
			}
                        else if ((inst->servers[i]->current_connection_count 
					* 1000) / inst->servers[i]->weight <
                                   (candidate->current_connection_count *
					1000) / candidate->weight)
                        {
				/* This running server has fewer
				connections, set it as a new candidate */
				candidate = inst->servers[i];
			}
                        else if ((inst->servers[i]->current_connection_count 
					* 1000) / inst->servers[i]->weight ==
                                   (candidate->current_connection_count *
					1000) / candidate->weight &&
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

	/* There is no candidate server here!
	 * With router_option=slave a master_host could be set, so route traffic there.
	 * Otherwise, just clean up and return NULL
	 */
	if (!candidate) {
		if (master_host) {
			candidate = master_host;
		} else {
                	LOGIF(LE, (skygw_log_write_flush(
                      	  LOGFILE_ERROR,
                      	  "Error : Failed to create new routing session. "
                      	  "Couldn't find eligible candidate server. Freeing "
                       	 "allocated resources.")));
			free(client_rses);
			return NULL;
		}
	}

	client_rses->rses_capabilities = RCAP_TYPE_PACKET_INPUT;
        
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
static void freeSession(
        ROUTER* router_instance,
        void*   router_client_ses)
{
        ROUTER_INSTANCE*   router = (ROUTER_INSTANCE *)router_instance;
        ROUTER_CLIENT_SES* router_cli_ses =
                (ROUTER_CLIENT_SES *)router_client_ses;
        int prev_val;
        
        prev_val = atomic_add(&router_cli_ses->backend->current_connection_count, -1);
        ss_dassert(prev_val > 0);
        
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
closeSession(ROUTER *instance, void *router_session)
{
ROUTER_CLIENT_SES *router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
DCB*              backend_dcb;

        CHK_CLIENT_RSES(router_cli_ses);
        /**
         * Lock router client session for secure read and update.
         */
        if (rses_begin_locked_router_action(router_cli_ses))
        {
		/* decrease server current connection counter */
		atomic_add(&router_cli_ses->backend->server->stats.n_current, -1);

                backend_dcb = router_cli_ses->backend_dcb;
                router_cli_ses->backend_dcb = NULL;
                router_cli_ses->rses_closed = true;
                /** Unlock */
                rses_end_locked_router_action(router_cli_ses);
                
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
routeQuery(ROUTER *instance, void *router_session, GWBUF *queue)
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
                rses_is_closed = !(rses_begin_locked_router_action(router_cli_ses));
        }

        if (!rses_is_closed)
        {
                backend_dcb = router_cli_ses->backend_dcb;           
                /** unlock */
                rses_end_locked_router_action(router_cli_ses);
        }

        if (rses_is_closed ||  backend_dcb == NULL)
        {
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "Error : Failed to route MySQL command %d to backend "
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
diagnostics(ROUTER *router, DCB *dcb)
{
ROUTER_INSTANCE	  *router_inst = (ROUTER_INSTANCE *)router;
ROUTER_CLIENT_SES *session;
int		  i = 0;
BACKEND		  *backend;
char              *weightby;

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
	if ((weightby = serviceGetWeightingParameter(router_inst->service))
							!= NULL)
	{
		dcb_printf(dcb, "\tConnection distribution based on %s "
				"server parameter.\n",
				weightby);
		dcb_printf(dcb,
			"\t\tServer               Target %% Connections\n");
		for (i = 0; router_inst->servers[i]; i++)
		{
			backend = router_inst->servers[i];
			dcb_printf(dcb, "\t\t%-20s %3.1f%%     %d\n",
				backend->server->unique_name,
				(float)backend->weight / 10,
				backend->current_connection_count);
		}
		
	}
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
clientReply(
        ROUTER *instance,
        void   *router_session,
        GWBUF  *queue,
        DCB    *backend_dcb)
{
	DCB *client = NULL;

	client = backend_dcb->session->client;

	ss_dassert(client != NULL);

	SESSION_ROUTE_REPLY(backend_dcb->session, queue);
}

/**
 * Error Handler routine
 *
 * The routine will handle errors that occurred in backend writes.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action     	The action: REPLY, REPLY_AND_CLOSE, NEW_CONNECTION
 *
 */
static  void
handleError(
        ROUTER           *instance,
        void             *router_session,
        GWBUF            *errbuf,
        DCB              *backend_dcb,
        error_action_t   action,
        bool             *succp)
{
	DCB		*client = NULL;
	SESSION         *session = backend_dcb->session;
	client = session->client;

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
static bool rses_begin_locked_router_action(
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
static void rses_end_locked_router_action(
        ROUTER_CLIENT_SES* rses)
{
        CHK_CLIENT_RSES(rses);
        spinlock_release(&rses->rses_lock);
}


static uint8_t getCapabilities(
        ROUTER*  inst,
        void*    router_session)
{
        return 0;
}

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param servers	The list of servers
 * @return		The Master found
 *
 */

static BACKEND *get_root_master(BACKEND **servers) {
	int i = 0;
	BACKEND *master_host = NULL;

	for (i = 0; servers[i]; i++) {
		if (servers[i] && (servers[i]->server->status & (SERVER_MASTER|SERVER_MAINT)) == SERVER_MASTER) {
			if (master_host && servers[i]->server->depth < master_host->server->depth) {
				master_host = servers[i];
			} else {
				if (master_host == NULL) {
					master_host = servers[i];
				}
			}
		}
	}
	return master_host;
}
