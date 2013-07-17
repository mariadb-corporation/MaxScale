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
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <router.h>
#include <readwritesplit.h>

#include <stdlib.h>
#include <mysql.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <dcb.h>
#include <spinlock.h>

/**
 * @file readwritesplit.c	The entry points for the read/write query splitting
 * router module.
 *
 * This file contains the entry points that comprise the API to the read write
 * query splitting router.
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01/07/2013	Vilho Raatikka		Initial implementation
 * 15/07/2013	Massimiliano Pinto	Added clientReply
 *					from master only in case of session change
 *
 * @endverbatim
 */

static char *version_str = "V1.0.1";

static	ROUTER* createInstance(SERVICE *service, char **options);
static	void*   newSession(ROUTER *instance, SESSION *session);
static	void    closeSession(ROUTER *instance, void *session);
static	int     routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static	void    diagnostic(ROUTER *instance, DCB *dcb);
static  void	clientReply(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb);

static ROUTER_OBJECT MyObject =
{ createInstance,
  newSession,
  closeSession,
  routeQuery,
  diagnostic,
  clientReply };

static SPINLOCK	 instlock;
static INSTANCE* instances;

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
        skygw_log_write_flush(NULL,
                              LOGFILE_MESSAGE,
                              "Initialize read/write split router module.\n");
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
ROUTER_OBJECT* GetModuleObject() {
        skygw_log_write(NULL,
                        LOGFILE_TRACE,
                        "Returning readwritesplit router module object.");
        return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 *
 * The job of ths entry point is to create the service wide data needed
 * for the query router. This is information needed to route queries that
 * is not related to any individual client session, exmaples of data that
 * might be stored in the ROUTER object for a particular query router are
 * connections counts, last used connection etc so that balancing may
 * take place.
 * 
 * @param service	The service this router is being create for
 * @param options	The options for this query router
 *
 * @return The instance data for this new instance
 */
static ROUTER* createInstance(
        SERVICE* service,
        char**   options)
{
        INSTANCE* inst;
        SERVER*   server;
        int       n;
        int       i;
        
        if ((inst = calloc(1, sizeof(INSTANCE))) == NULL) {
            return NULL; 
        } 
        inst->service = service;
        spinlock_init(&inst->lock);
        inst->connections = NULL;
        
        /** Calculate number of servers */
        for (server = service->databases, n = 0; server; server = server->nextdb) {
            n++;
        }
        inst->servers = (BACKEND **)calloc(n + 1, sizeof(BACKEND *));
        
        if (!inst->servers) {
            free(inst);
            return NULL;
        }
        /**
         * We need an array of the backend servers in the instance structure so
         * that we can maintain a count of the number of connections to each
         * backend server.
         */
        for (server = service->databases, n = 0; server; server = server->nextdb) {
            
            if ((inst->servers[n] = malloc(sizeof(BACKEND))) == NULL) {
                for (i = 0; i < n; i++) {
                    free(inst->servers[i]);
                }
                free(inst->servers);
                free(inst);
                return NULL;
            }
            inst->servers[n]->server = server;
            inst->servers[n]->count = 0;
            n++;
        }
        inst->servers[n] = NULL;
        
        /**
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
 * The session is used to store all the data required for a particular
 * client connection.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static void* newSession(
        ROUTER*  instance,
        SESSION* session)
{
        BACKEND*           candidate = NULL;
        CLIENT_SESSION*    client;
        INSTANCE*          inst = (INSTANCE *)instance;
        int                i;
        
        if ((client = (CLIENT_SESSION *)malloc(sizeof(CLIENT_SESSION))) == NULL)
	{
            return NULL;
        }

        /**
         * Find a backend server to connect to. This is the extent of the
         * load balancing algorithm we need to implement for this simple
         * connection router.
         */
        for (i = 0; inst->servers[i]; i++)
	{
            
            if (inst->servers[i] && SERVER_IS_SLAVE(inst->servers[i]->server))
            {
                candidate = inst->servers[i];
                break;
            }
        }

        /**
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
            
            if (inst->servers[i]
                && SERVER_IS_RUNNING(inst->servers[i]->server))
            {
                if (SERVER_IS_SLAVE(inst->servers[i]->server))
                {
                    if (inst->servers[i]->count < candidate->count) {
                        candidate = inst->servers[i];
                    } else if (inst->servers[i]->count == candidate->count &&
                               inst->servers[i]->server->stats.n_connections
                               < candidate->server->stats.n_connections)
                    {
                        candidate = inst->servers[i];
                    }
                } else if (SERVER_IS_MASTER(inst->servers[i]->server)) {
                    /** master is found */
                    inst->master = inst->servers[i];
                }
            }
        } /* for */

        if (inst->master == NULL) {
            inst->master = inst->servers[i-1];
        }
        /**
         * We now have a master and a slave server with the least connections.
         * Bump the connection counts for these servers.
         */
        atomic_add(&candidate->count, 1);
        client->slave  = candidate;
        atomic_add(&inst->master->count, 1);
        client->master = inst->master;
        ss_dassert(client->master->server != candidate->server);
        
        /**
         * Open the slave connection.
         */
	if ((client->slaveconn = dcb_connect(candidate->server, session,
				     candidate->server->protocol)) == NULL)
	{
		atomic_add(&candidate->count, -1);
		free(client);
		return NULL;
	}
	/**
	 * Open the master connection.
	 */
	if ((client->masterconn = dcb_connect(client->master->server, session,
				 client->master->server->protocol)) == NULL)
	{
		atomic_add(&client->master->count, -1);
		free(client);
		return NULL;
	}
	inst->stats.n_sessions += 1;

	/* Add this session to end of the list of active sessions */
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
 * @param instance	The router instance data
 * @param session	The session being closed
 */
static void closeSession(
        ROUTER* instance,
        void*   router_session)
{
        INSTANCE*       inst = (INSTANCE *)instance;
        CLIENT_SESSION* session = (CLIENT_SESSION *)router_session;
        
        /**
         * Close the connection to the backend servers
         */
        session->slaveconn->func.close(session->slaveconn);
        session->masterconn->func.close(session->masterconn);
        atomic_add(&session->slave->count, -1);
        atomic_add(&session->master->count, -1);
        atomic_add(&session->slave->server->stats.n_current, -1);
        atomic_add(&session->master->server->stats.n_current, -1);
        
        spinlock_acquire(&inst->lock);
        if (inst->connections == session) {
            inst->connections = session->next;
        } else {
            CLIENT_SESSION* ptr = inst->connections;

            while (ptr && ptr->next != session) {
                ptr = ptr->next;
            }
            
            if (ptr) {
                ptr->next = session->next;
            }
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
 * The main routing entry, this is called with every packet that is
 * received and has to be forwarded to the backend database.
 *
 * The routeQuery will make the routing decision based on the contents
 * of the instance, session and the query itself in the queue. The
 * data in the queue may not represent a complete query, it represents
 * the data that has been received. The query router itself is responsible
 * for buffering the partial query, a later call to the query router will
 * contain the remainder, or part thereof of the query.
 *
 * @param instance	The query router instance
 * @param session	The session associated with the client
 * @param queue		Gateway buffer queue with the packets received
 *
 * @return The number of queries forwarded
 */
static int routeQuery(
        ROUTER* instance,
        void*   router_session,
        GWBUF*  queue)
{
        skygw_query_type_t qtype    = QUERY_TYPE_UNKNOWN;
        char*              querystr = NULL;
        char*              startpos;
        size_t             len;
        unsigned char      packet_type, *packet;
        int                ret = 0;
        
        INSTANCE*       inst = (INSTANCE *)instance;
        CLIENT_SESSION* session = (CLIENT_SESSION *)router_session;
        inst->stats.n_queries++;

	packet = GWBUF_DATA(queue);
        packet_type = packet[4];
        startpos = (char *)&packet[5];
        len      = packet[0];
        len     += 255*packet[1];
        len     += 255*255*packet[2];
        
        switch(packet_type) {
            case COM_INIT_DB:     /**< 2 DDL must go to the master */
            case COM_REFRESH:     /**< 7 - I guess this is session but not sure */
            case COM_DEBUG:       /**< 0d all servers dump debug info to stdout */
            case COM_PING:        /**< 0e all servers are pinged */
            case COM_CHANGE_USER: /**< 11 all servers change it accordingly */
                qtype = QUERY_TYPE_SESSION_WRITE;
                break;

            case COM_CREATE_DB:   /**< 5 DDL must go to the master */
            case COM_DROP_DB:     /**< 6 DDL must go to the master */
                qtype = QUERY_TYPE_WRITE;
                break;

            case COM_QUERY:
                querystr = (char *)malloc(len);
                memcpy(querystr, startpos, len-1);
                memset(&querystr[len-1], 0, 1);
                qtype = skygw_query_classifier_get_type(querystr, 0);
                break;

            default:
            case COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
            case COM_STATISTICS:     /**< 9 ? */
            case COM_PROCESS_INFO:   /**< 0a ? */
            case COM_CONNECT:        /**< 0b ? */
            case COM_PROCESS_KILL:   /**< 0c ? */
            case COM_TIME:           /**< 0f should this be run in gateway ? */
            case COM_DELAYED_INSERT: /**< 10 ? */
            case COM_DAEMON:         /**< 1d ? */
                break;
        }

#if defined(SS_DEBUG_)
        skygw_log_write(NULL, LOGFILE_TRACE, "String\t\"%s\"", querystr);
        skygw_log_write(NULL,
                        LOGFILE_TRACE,
                        "Packet type\t%s",
                        STRPACKETTYPE(packet_type));
#endif 

        switch (qtype) {
            case QUERY_TYPE_WRITE:
#if defined(SS_DEBUG_)
                skygw_log_write(NULL,
                                LOGFILE_TRACE,
                                "Query type\t%s, routing to Master.",
                                STRQTYPE(qtype));
#endif
                ret = session->masterconn->func.write(session->masterconn, queue);
		atomic_add(&inst->stats.n_master, 1);
                goto return_ret;
                break;

            case QUERY_TYPE_READ:
#if defined(SS_DEBUG_)
                skygw_log_write(NULL,
                                LOGFILE_TRACE,
                                "Query type\t%s, routing to Slave.",
                                STRQTYPE(qtype));
#endif
                ret = session->slaveconn->func.write(session->slaveconn, queue);
		atomic_add(&inst->stats.n_slave, 1);
                goto return_ret;
                break;

                
            case QUERY_TYPE_SESSION_WRITE:
#if defined(SS_DEBUG_)
                skygw_log_write(NULL,
                                LOGFILE_TRACE,
                                "Query type\t%s, routing to All servers.",
                                STRQTYPE(qtype));
#endif
                /**
                 * TODO! Connection to all servers must be established, and
                 * the command must be executed in them.
                 */

		if (packet_type != COM_CHANGE_USER)
		{
			GWBUF *cq = gwbuf_clone(queue);
	                ret = session->masterconn->func.session(session->masterconn, (void *)queue);
			session->slaveconn->func.session(session->slaveconn, (void *)cq);
		} else {
			GWBUF *cq = gwbuf_clone(queue);
			session->masterconn->func.auth(session->masterconn, NULL, session->masterconn->session, queue);
			session->slaveconn->func.auth(session->slaveconn, NULL, session->masterconn->session, cq);
		}
		atomic_add(&inst->stats.n_all, 1);
                goto return_ret;
                break;
                
            default:
#if defined(SS_DEBUG_)
                skygw_log_write(NULL,
                                LOGFILE_TRACE,
                                "Query type\t%s, routing to Master by default.",
                                STRQTYPE(qtype));
#endif
                /** Is this really ok? */
                ret = session->masterconn->func.write(session->masterconn, queue);
		atomic_add(&inst->stats.n_master, 1);
                goto return_ret;
                break;
        }

return_ret:
        free(querystr);
        return ret;
}

/**
 * Diagnostics routine
 *
 * Print query router statistics to the DCB passed in
 *
 * @param	instance	The router instance
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(ROUTER *instance, DCB *dcb)
{
CLIENT_SESSION	*session;
INSTANCE	*inst = (INSTANCE *)instance;
int		i = 0;

	spinlock_acquire(&inst->lock);
	session = inst->connections;
	while (session)
	{
		i++;
		session = session->next;
	}
	spinlock_release(&inst->lock);
	
	dcb_printf(dcb, "\tNumber of router sessions:           	%d\n", inst->stats.n_sessions);
	dcb_printf(dcb, "\tCurrent no. of router sessions:      	%d\n", i);
	dcb_printf(dcb, "\tNumber of queries forwarded:          	%d\n", inst->stats.n_queries);
	dcb_printf(dcb, "\tNumber of queries forwarded to master:	%d\n", inst->stats.n_master);
	dcb_printf(dcb, "\tNumber of queries forwarded to slave: 	%d\n", inst->stats.n_slave);
	dcb_printf(dcb, "\tNumber of queries forwarded to all:   	%d\n", inst->stats.n_all);

}

/**
 * Client Reply routine
 *
 * The routine will reply to client for session change with master server data
 *
 * @param	instance	The router instance
 * @param	router_session	The router session 
 * @param	backend_dcb	The backend DCB
 * @param	queue		The GWBUF with reply data
 */
static	void
clientReply(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb)
{
INSTANCE*       inst = NULL;
DCB		*master = NULL;
DCB             *client = NULL;
CLIENT_SESSION* session = NULL;

	inst = (INSTANCE *)instance;	
	session = (CLIENT_SESSION *)router_session;
	master = session->masterconn;
	client = backend_dcb->session->client;

	if (backend_dcb->command == ROUTER_CHANGE_SESSION) {
		/* if backend_dcb is the master we can reply to the client */
		if (backend_dcb == master) {
			master->session->client->func.write(master->session->client, queue);
		} else {
			/* just consume the gwbuf without writing to the client */
			gwbuf_consume(queue, gwbuf_length(queue));
		}
	} else {
		/* normal flow */
		client->func.write(client, queue);
	}
}
///
