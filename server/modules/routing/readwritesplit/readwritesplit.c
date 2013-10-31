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
#include <stdlib.h>

#include <router.h>
#include <readwritesplit.h>

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
 * 17/07/2013	Massimiliano Pinto	clientReply is now used by mysql_backend
 *					for all reply situations
 * 18/07/2013	Massimiliano Pinto	routeQuery now handles COM_QUIT
 *					as QUERY_TYPE_SESSION_WRITE
 *
 * @endverbatim
 */

static char *version_str = "V1.0.2";

static	ROUTER* createInstance(SERVICE *service, char **options);
static	void*   newSession(ROUTER *instance, SESSION *session);
static	void    closeSession(ROUTER *instance, void *session);
static	void    freeSession(ROUTER *instance, void *session);
static	int     routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static	void    diagnostic(ROUTER *instance, DCB *dcb);
static  void	clientReply(
        ROUTER* instance,
        void*   router_session,
        GWBUF*  queue,
        DCB*    backend_dcb);

static bool search_backend_servers(
        BACKEND**        p_master,
        BACKEND**        p_slave,
        ROUTER_INSTANCE* router);

static ROUTER_OBJECT MyObject = {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostic,
        clientReply,
	NULL
};
static bool rses_begin_router_action(
        ROUTER_CLIENT_SES* rses);

static void rses_exit_router_action(
        ROUTER_CLIENT_SES* rses);

static SPINLOCK	        instlock;
static ROUTER_INSTANCE* instances;

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
        skygw_log_write_flush(
                LOGFILE_MESSAGE,
                "Initializing statemend-based read/write split router module.");
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
ROUTER_OBJECT* GetModuleObject()
{
        return &MyObject;
}

/**
 * Create an instance of read/write statemtn router within the MaxScale.
 *
 * 
 * @param service	The service this router is being create for
 * @param options	The options for this query router
 *
 * @return NULL in failure, pointer to router in success.
 */
static ROUTER* createInstance(
        SERVICE* service,
        char**   options)
{
        ROUTER_INSTANCE* router;
        SERVER*          server;
        int              n;
        int              i;
        
        if ((router = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
            return NULL; 
        } 
        router->service = service;
        spinlock_init(&router->lock);
        
        /** Calculate number of servers */
        server = service->databases;
        
        for (n=0; server != NULL; server=server->nextdb) {
            n++;
        }
        router->servers = (BACKEND **)calloc(n + 1, sizeof(BACKEND *));
        
        if (router->servers == NULL)
        {
            free(router);
            return NULL;
        }

	if (options != NULL)
	{
        	skygw_log_write_flush(
                        LOGFILE_MESSAGE,
                        "Router options supplied to read/write statement router "
                        "module but none are supported. The options will be "
                        "ignored.");
	}

        /**
         * Create an array of the backend servers in the router structure to
         * maintain a count of the number of connections to each
         * backend server.
         */
        server = service->databases;
        n = 0;
        while (server != NULL) {
                if ((router->servers[n] = malloc(sizeof(BACKEND))) == NULL)
                {
                        for (i = 0; i < n; i++) {
                                free(router->servers[i]);
                        }
                        free(router->servers);
                        free(router);
                        return NULL;
                }
                router->servers[n]->backend_server = server;
                router->servers[n]->backend_conn_count = 0;
                n += 1;
                server = server->nextdb;
        }        
        router->servers[n] = NULL;
        
        /**
         * vraa : is this necessary for readwritesplit ?
         * Option : where can a read go?
         * - master (only)
         * - slave (only)
         * - joined (to both)
         *
	 * Process the options
	 */
	router->bitmask = 0;
	router->bitvalue = 0;
	if (options)
	{
		for (i = 0; options[i]; i++)
		{
			if (!strcasecmp(options[i], "master"))
			{
				router->bitmask |= (SERVER_MASTER|SERVER_SLAVE);
				router->bitvalue |= SERVER_MASTER;
			}
			else if (!strcasecmp(options[i], "slave"))
			{
				router->bitmask |= (SERVER_MASTER|SERVER_SLAVE);
				router->bitvalue |= SERVER_SLAVE;
			}
			else if (!strcasecmp(options[i], "synced"))
			{
				router->bitmask |= (SERVER_JOINED);
				router->bitvalue |= SERVER_JOINED;
			}
			else
			{
                                skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Warning : Unsupported router option %s "
                                        "for readwritesplitrouter.",
                                        options[i]);
			}
		}
	}
        /**
         * We have completed the creation of the router data, so now
         * insert this router into the linked list of routers
         * that have been created with this module.
         */
        spinlock_acquire(&instlock);
        router->next = instances;
        instances = router;
        spinlock_release(&instlock);
        
        return (ROUTER *)router;
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
        ROUTER*  router_inst,
        SESSION* session)
{
        BACKEND*               be_slave  = NULL;
        BACKEND*               be_master = NULL;
        ROUTER_CLIENT_SES*     client_rses;
        ROUTER_INSTANCE*       router = (ROUTER_INSTANCE *)router_inst;
        bool                   succp;

        client_rses =
                (ROUTER_CLIENT_SES *)calloc(1, sizeof(ROUTER_CLIENT_SES));

        if (client_rses == NULL)
        {
                ss_dassert(false);
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
         */
        succp = search_backend_servers(&be_master, &be_slave, router);

        /** Both Master and Slave must be found */
        if (!succp) {
                free(client_rses);
                return NULL;
        }
        /**
         * Open the slave connection.
         */
        client_rses->slave_dcb = dcb_connect(be_slave->backend_server,
                                             session,
                                             be_slave->backend_server->protocol);
        
	if (client_rses->slave_dcb == NULL) {
                ss_dassert(session->refcount == 1);
		free(client_rses);
		return NULL;
	}
	/**
	 * Open the master connection.
	 */
        client_rses->master_dcb = dcb_connect(be_master->backend_server,
                                             session,
                                             be_master->backend_server->protocol);

        if (client_rses->master_dcb == NULL)
	{
                /** Close slave connection first. */
                client_rses->slave_dcb->func.close(client_rses->slave_dcb);
		free(client_rses);
		return NULL;
	}
        /**
         * We now have a master and a slave server with the least connections.
         * Bump the connection counts for these servers.
         */
        atomic_add(&be_slave->backend_conn_count, 1);
        atomic_add(&be_master->backend_conn_count, 1);
        
        client_rses->be_slave = be_slave;
        client_rses->be_master = be_master;
        router->stats.n_sessions += 1;

        /**
         * Version is bigger than zero once initialized.
         */
        atomic_add(&client_rses->rses_versno, 2);
        ss_dassert(client_rses->rses_versno == 2);
	/**
         * Add this session to end of the list of active sessions in router.
         */
	spinlock_acquire(&router->lock);
        client_rses->next = router->connections;
        router->connections = client_rses;
        spinlock_release(&router->lock);

        CHK_CLIENT_RSES(client_rses);
        
        return (void *)client_rses;
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
        ROUTER_CLIENT_SES* router_cli_ses;
        DCB* slave_dcb;
        DCB* master_dcb;

        router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);
        /**
         * Lock router client session for secure read and update.
         */
        if (rses_begin_router_action(router_cli_ses))
        {
                slave_dcb = router_cli_ses->slave_dcb;
                router_cli_ses->slave_dcb = NULL;
                master_dcb = router_cli_ses->master_dcb;
                router_cli_ses->master_dcb = NULL;
                
                router_cli_ses->rses_closed = true;
                /** Unlock */
                rses_exit_router_action(router_cli_ses);
                
                /**
                 * Close the backend server connections
                 */
                if (slave_dcb != NULL) {
                        CHK_DCB(slave_dcb);
                        slave_dcb->func.close(slave_dcb);
                }
                
                if (master_dcb != NULL) {
                        master_dcb->func.close(master_dcb);
                        CHK_DCB(master_dcb);
                }
        }
}

static void freeSession(
        ROUTER* router_instance,
        void*   router_client_session)
{
        ROUTER_CLIENT_SES* router_cli_ses;
        ROUTER_INSTANCE*   router;
        
        router_cli_ses = (ROUTER_CLIENT_SES *)router_client_session;
        router = (ROUTER_INSTANCE *)router_instance;

        atomic_add(&router_cli_ses->be_slave->backend_conn_count, -1);
        atomic_add(&router_cli_ses->be_master->backend_conn_count, -1);
        atomic_add(&router_cli_ses->be_slave->backend_server->stats.n_current, -1);
        atomic_add(&router_cli_ses->be_master->backend_server->stats.n_current, -1);

        spinlock_acquire(&router->lock);

        if (router->connections == router_cli_ses) {
            router->connections = router_cli_ses->next;
        } else {
            ROUTER_CLIENT_SES* ptr = router->connections;

            while (ptr && ptr->next != router_cli_ses) {
                ptr = ptr->next;
            }
            
            if (ptr) {
                ptr->next = router_cli_ses->next;
            }
        }
        spinlock_release(&router->lock);
        
        /*
         * We are no longer in the linked list, free
         * all the memory and other resources associated
         * to the client session.
         */
	free(router_cli_ses);
        return;
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
        GWBUF*  querybuf)
{
        skygw_query_type_t qtype    = QUERY_TYPE_UNKNOWN;
        char*              querystr = NULL;
        char*              startpos;
        size_t             len;
        unsigned char      packet_type;
        unsigned char*     packet;
        int                ret = 0;
        DCB*               master_dcb = NULL;
        DCB*               slave_dcb  = NULL;
	GWBUF*	       	   bufcopy = NULL;
        ROUTER_INSTANCE*   inst = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        bool               rses_is_closed;

        CHK_CLIENT_RSES(router_cli_ses);
                
        inst->stats.n_queries++;

	packet = GWBUF_DATA(querybuf);
        packet_type = packet[4];
        startpos = (char *)&packet[5];
        len      = packet[0];
        len     += 255*packet[1];
        len     += 255*255*packet[2];

        switch(packet_type) {
        case COM_QUIT:        /**< 1 QUIT will close all sessions */
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
                
        case COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
        case COM_STATISTICS:     /**< 9 ? */
        case COM_PROCESS_INFO:   /**< 0a ? */
        case COM_CONNECT:        /**< 0b ? */
        case COM_PROCESS_KILL:   /**< 0c ? */
        case COM_TIME:           /**< 0f should this be run in gateway ? */
        case COM_DELAYED_INSERT: /**< 10 ? */
        case COM_DAEMON:         /**< 1d ? */
        default:
                break;
        } /**< switch by packet type */

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
                master_dcb = router_cli_ses->master_dcb;
                slave_dcb = router_cli_ses->slave_dcb;                
                /** unlock */
                rses_exit_router_action(router_cli_ses);
        }

        if (rses_is_closed || (master_dcb == NULL && slave_dcb == NULL))
        {
                skygw_log_write(
                        LOGFILE_ERROR,
                        "Error: Failed to route %s:%s:\"%s\" to backend server. "
                        "%s.",
                        STRPACKETTYPE(packet_type),
                        STRQTYPE(qtype),
                        querystr,
                        (rses_is_closed ? "Router was closed" :
                         "Router has no backend servers where to route to"));
                        
                goto return_ret;
        }
        
        skygw_log_write(LOGFILE_TRACE, "String\t\"%s\"", querystr);
        skygw_log_write(LOGFILE_TRACE,
                        "Packet type\t%s",
                        STRPACKETTYPE(packet_type));
        
        switch (qtype) {
        case QUERY_TYPE_WRITE:
                skygw_log_write(LOGFILE_TRACE,
                        "%lu [routeQuery:rwsplit] Query type\t%s, routing to "
                        "Master.",
                        pthread_self(),
                        STRQTYPE(qtype));
                
                ret = master_dcb->func.write(master_dcb, querybuf);
                atomic_add(&inst->stats.n_master, 1);
                
                goto return_ret;
                break;
                
        case QUERY_TYPE_READ:
                skygw_log_write(LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to Slave.",
                                pthread_self(),
                                STRQTYPE(qtype));

                ret = slave_dcb->func.write(slave_dcb, querybuf);
                atomic_add(&inst->stats.n_slave, 1);
                
                goto return_ret;
                break;
                
                
        case QUERY_TYPE_SESSION_WRITE:
                skygw_log_write(LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to All servers.",
                                pthread_self(),
                                STRQTYPE(qtype));
                /**
                 * TODO! Connection to all servers must be established, and
                 * the command must be executed in them.
                 */

		bufcopy = gwbuf_clone(querybuf);

		switch(packet_type) {
                case COM_QUIT:
                        ret = master_dcb->func.write(master_dcb, querybuf);
                        slave_dcb->func.write(slave_dcb, bufcopy);
                        break;
                        
                case COM_CHANGE_USER:
                        master_dcb->func.auth(
                                master_dcb,
                                NULL,
                                master_dcb->session,
                                querybuf);
                        slave_dcb->func.auth(
                                slave_dcb,
                                NULL,
                                master_dcb->session,
                                bufcopy);
                        break;

                default:
                        ret = master_dcb->func.session(master_dcb, (void *)querybuf);
                        slave_dcb->func.session(slave_dcb, (void *)bufcopy);
                        break;
		} /**< switch by packet type */

		atomic_add(&inst->stats.n_all, 1);
                goto return_ret;
                break;
                
        default:
                skygw_log_write(LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to Master by default.",
                                pthread_self(),
                                STRQTYPE(qtype));
                
                /**
                 * Is this really ok?
                 * What is not known is routed to master.
                 */
                ret = master_dcb->func.write(master_dcb, querybuf);
                atomic_add(&inst->stats.n_master, 1);
                goto return_ret;
                break;
        } /**< switch by query type */

return_ret:
        free(querystr);
        return ret;
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
ROUTER_CLIENT_SES *router_cli_ses;
ROUTER_INSTANCE	  *router = (ROUTER_INSTANCE *)instance;
int		  i = 0;

	spinlock_acquire(&router->lock);
	router_cli_ses = router->connections;
	while (router_cli_ses)
	{
		i++;
		router_cli_ses = router_cli_ses->next;
	}
	spinlock_release(&router->lock);
	
	dcb_printf(dcb,
                   "\tNumber of router sessions:           	%d\n",
                   router->stats.n_sessions);
	dcb_printf(dcb,
                   "\tCurrent no. of router sessions:      	%d\n",
                   i);
	dcb_printf(dcb,
                   "\tNumber of queries forwarded:          	%d\n",
                   router->stats.n_queries);
	dcb_printf(dcb,
                   "\tNumber of queries forwarded to master:	%d\n",
                   router->stats.n_master);
	dcb_printf(dcb,
                   "\tNumber of queries forwarded to slave: 	%d\n",
                   router->stats.n_slave);
	dcb_printf(dcb,
                   "\tNumber of queries forwarded to all:   	%d\n",
                   router->stats.n_all);
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
static void clientReply(
        ROUTER* instance,
        void*   router_session,
        GWBUF*  writebuf,
        DCB*    backend_dcb)
{
        DCB*               master_dcb;
        DCB*               client_dcb;
        ROUTER_CLIENT_SES* router_cli_ses;
        
	router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);

        /**
         * Lock router client session for secure read of router session members.
         * Note that this could be done without lock by using version #
         */
        if (rses_begin_router_action(router_cli_ses))
        {
                master_dcb = router_cli_ses->master_dcb;

                /** Unlock */
                rses_exit_router_action(router_cli_ses);

                client_dcb = backend_dcb->session->client;
                
                if (backend_dcb != NULL &&
                    backend_dcb->command == ROUTER_CHANGE_SESSION)
                {
                        /* if backend_dcb is master we can reply to client */
                        if (client_dcb != NULL &&
                            backend_dcb == master_dcb)
                        {
                                client_dcb->func.write(client_dcb, writebuf);
                        } else {
                                /* consume the gwbuf without writing to client */
                                gwbuf_consume(writebuf, gwbuf_length(writebuf));
                        }
                }
                else if (client_dcb != NULL)
                {
                        /* normal flow */
                        client_dcb->func.write(client_dcb, writebuf);
                }
        }
}

/** 
 * @node Search suitable backend server from those of router instance.
 *
 * Parameters:
 * @param p_master - in, use, out
 *          Pointer to location where master's address is to  be stored.
 *          If NULL, then master is not searched.
 *
 * @param p_slave - in, use, out 
 *          Pointer to location where slave's address is to be stored.
 *          if NULL, then slave is not searched.
 *
 * @param inst - in, use
 *          Pointer to router instance
 *
 * @return true, if all what what requested found, false if the request
 *   was not satisfied or was partially satisfied.
 *
 * 
 * @details It is assumed that there is only one master among servers of
 * a router instance. As a result, thr first master is always chosen.
 */
static bool search_backend_servers(
        BACKEND**        p_master,
        BACKEND**        p_slave,
        ROUTER_INSTANCE* router)
{
        BACKEND* be_master = NULL;
        BACKEND* be_slave = NULL;
        int      i;
        bool     succp = true;

	/*
	 * Loop over all the servers and find any that have fewer connections
         * than current candidate server.
	 *
	 * If a server has less connections than the current candidate it is
         * chosen to a new candidate.
	 *
	 * If a server has the same number of connections currently as the
         * candidate and has had less connections over time than the candidate
         * it will also become the new candidate. This has the effect of
         * spreading the connections over different servers during periods of
         * very low load.
         *
         * If master is searched for, the first master found is chosen.
	 */
	for (i = 0; router->servers[i] != NULL; i++) {
                BACKEND* be = router->servers[i];
                
		if (be != NULL) {
			skygw_log_write(
				LOGFILE_TRACE,
				"%lu [search_backend_servers] Examine server %s:%d "
                                "with %d connections. Status is %d, "
				"router->bitvalue is %d",
                                pthread_self(),
                                be->backend_server->name,
				be->backend_server->port,
				be->backend_conn_count,
				be->backend_server->status,
				router->bitmask);
		}

		if (be != NULL &&
                    SERVER_IS_RUNNING(be->backend_server) &&
                    (be->backend_server->status & router->bitmask) ==
                    router->bitvalue)
                {
                        if (SERVER_IS_SLAVE(be->backend_server) &&
                            p_slave != NULL)
                        {
                                /**
                                 * If no candidate set, set first running
                                 * server as an initial candidate server.
                                 */
                                if (be_slave == NULL)
                                {
                                        be_slave = be;
                                }
                                else if (be->backend_conn_count <
                                         be_slave->backend_conn_count)
                                {
                                        /**
                                         * This running server has fewer
                                         * connections, set it as a new
                                         * candidate.
                                         */
                                        be_slave = be;
                                }
                                else if (be->backend_conn_count ==
                                         be_slave->backend_conn_count &&
                                         be->backend_server->stats.n_connections <
                                         be_slave->backend_server->stats.n_connections)
                                {
                                        /**
                                         * This running server has the same
                                         * number of connections currently
                                         * as the candidate but has had
                                         * fewer connections over time
                                         * than candidate, set this server
                                         * to candidate.
                                         */
                                        be_slave = be;
                                }
                        }
                        else if (p_master != NULL &&
                                 be_master == NULL &&
                                 SERVER_IS_MASTER(be->backend_server))
                        {
                                be_master = be;
                        }
		}
	}
        
        if (p_slave != NULL && be_slave == NULL) {
                succp = false;
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Couldn't find suitable Slave from %d candidates.",
                        i);
        }
        
        if (p_master != NULL && be_master == NULL) {
                succp = false;
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Couldn't find suitable Master from %d candidates.",
                        i);
        }

        if (be_slave != NULL) {
                *p_slave = be_slave;
                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [readwritesplit:search_backend_servers] Selected "
                        "Slave %s:%d from %d candidates.",
                        pthread_self(),
                        be_slave->backend_server->name,
                        be_slave->backend_server->port,
                        i);
        }
        if (be_master != NULL) {
                *p_master = be_master;
                skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [readwritesplit:search_backend_servers] Selected "
                        "Master %s:%d "
                        "from %d candidates.",
                        pthread_self(),
                        be_master->backend_server->name,
                        be_master->backend_server->port,
                        i);
        }
        return succp;
}
