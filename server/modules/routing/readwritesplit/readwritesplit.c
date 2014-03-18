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
#include <stdint.h>

#include <router.h>
#include <readwritesplit.h>

#include <mysql.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <dcb.h>
#include <spinlock.h>

extern int lm_enabled_logfiles_bitmask;

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
static  uint8_t getCapabilities (ROUTER* inst, void* router_session);


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
	NULL,
        getCapabilities
};
static bool rses_begin_locked_router_action(
        ROUTER_CLIENT_SES* rses);

static void rses_end_locked_router_action(
        ROUTER_CLIENT_SES* rses);

static void mysql_sescmd_done(
	mysql_sescmd_t* sescmd);

static mysql_sescmd_t* mysql_sescmd_init (
	rses_property_t*   rses_prop,
	GWBUF*             sescmd_buf,
        unsigned char      packet_type,
	ROUTER_CLIENT_SES* rses);

static rses_property_t* mysql_sescmd_get_property(
	mysql_sescmd_t* scmd);

static rses_property_t* rses_property_init(
	rses_property_type_t prop_type);

static void rses_property_add(
	ROUTER_CLIENT_SES* rses,
	rses_property_t*   prop);

static void rses_property_done(
	rses_property_t* prop);

static mysql_sescmd_t* rses_property_get_sescmd(
        rses_property_t* prop);

static sescmd_cursor_t* rses_get_sescmd_cursor(
	ROUTER_CLIENT_SES* rses,
	backend_type_t     be_type);

static bool execute_sescmd_in_backend(
	ROUTER_CLIENT_SES* rses,
	backend_type_t     be_type);

static void sescmd_cursor_set_active(
        sescmd_cursor_t* sescmd_cursor,
        bool             value);

static bool sescmd_cursor_is_active(
	sescmd_cursor_t* sescmd_cursor);

static GWBUF* sescmd_cursor_clone_querybuf(
	sescmd_cursor_t* scur);

static mysql_sescmd_t* sescmd_cursor_get_command(
	sescmd_cursor_t* scur);

static bool sescmd_cursor_next(
	sescmd_cursor_t* scur);

static GWBUF* sescmd_cursor_process_replies(
        DCB*             client_dcb,
        GWBUF*           replybuf,
        sescmd_cursor_t* scur);

static bool cont_exec_sescmd_in_backend(
        ROUTER_CLIENT_SES* rses,
        backend_type_t     be_type);

static void tracelog_routed_query(
        ROUTER_CLIENT_SES* rses,
        char*              funcname,
        DCB*               dcb,
        GWBUF*             buf);

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
        LOGIF(LM, (skygw_log_write_flush(
                           LOGFILE_MESSAGE,
                           "Initializing statemend-based read/write split router module.")));
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
        	LOGIF(LM, (skygw_log_write_flush(
                                   LOGFILE_MESSAGE,
                                   "Router options supplied to read/write statement router "
                                   "module but none are supported. The options will be "
                                   "ignored.")));
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
                                LOGIF(LE, (skygw_log_write_flush(
                                                   LOGFILE_ERROR,
                                                   "Warning : Unsupported router option %s "
                                                   "for readwritesplitrouter.",
                                                   options[i])));
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
	BACKEND*            local_backend[BE_COUNT];	
        ROUTER_CLIENT_SES*  client_rses;
        ROUTER_INSTANCE*    router = (ROUTER_INSTANCE *)router_inst;
        bool                succp;

        client_rses =
                (ROUTER_CLIENT_SES *)calloc(1, sizeof(ROUTER_CLIENT_SES));

        if (client_rses == NULL)
        {
                ss_dassert(false);
                return NULL;
        }
        memset(local_backend, 0, BE_COUNT*sizeof(void*));
        spinlock_init(&client_rses->rses_lock);
#if defined(SS_DEBUG)
        client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
        client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif
	/** store pointers to sescmd list to both cursors */
        client_rses->rses_cursor[BE_MASTER].scmd_cur_rses = client_rses;
	client_rses->rses_cursor[BE_MASTER].scmd_cur_active = false;
	client_rses->rses_cursor[BE_MASTER].scmd_cur_ptr_property = 
		&client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
        client_rses->rses_cursor[BE_MASTER].scmd_cur_cmd = NULL;
        client_rses->rses_cursor[BE_MASTER].scmd_cur_be_type = BE_MASTER;
                
        client_rses->rses_cursor[BE_SLAVE].scmd_cur_rses = client_rses;
        client_rses->rses_cursor[BE_SLAVE].scmd_cur_active = false;
	client_rses->rses_cursor[BE_SLAVE].scmd_cur_ptr_property = 
		&client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
        client_rses->rses_cursor[BE_SLAVE].scmd_cur_cmd = NULL;
        client_rses->rses_cursor[BE_SLAVE].scmd_cur_be_type = BE_SLAVE;
        
	/**
         * Find a backend server to connect to. This is the extent of the
         * load balancing algorithm we need to implement for this simple
         * connection router.
         */
        succp = search_backend_servers(&local_backend[BE_MASTER], 
				       &local_backend[BE_SLAVE], 
				       router);

        /** Both Master and Slave must be found */
        if (!succp) {
                free(client_rses);
                return NULL;
        }
        /**
         * Open the slave connection.
         */
        client_rses->rses_dcb[BE_SLAVE] = dcb_connect(
                local_backend[BE_SLAVE]->backend_server,
                session,
                local_backend[BE_SLAVE]->backend_server->protocol);
        
	if (client_rses->rses_dcb[BE_SLAVE] == NULL) {
                ss_dassert(session->refcount == 1);
		free(client_rses);
		return NULL;
	}
	/**
	 * Open the master connection.
	 */
        client_rses->rses_dcb[BE_MASTER] = dcb_connect(
                local_backend[BE_MASTER]->backend_server,
                session,
                local_backend[BE_MASTER]->backend_server->protocol);
        if (client_rses->rses_dcb[BE_MASTER] == NULL)
	{
                /** Close slave connection first. */
                client_rses->rses_dcb[BE_SLAVE]->func.close(client_rses->rses_dcb[BE_SLAVE]);
		free(client_rses);
		return NULL;
	}
        /**
         * We now have a master and a slave server with the least connections.
         * Bump the connection counts for these servers.
         */
        atomic_add(&local_backend[BE_SLAVE]->backend_conn_count, 1);
        atomic_add(&local_backend[BE_MASTER]->backend_conn_count, 1);
        
        client_rses->rses_backend[BE_SLAVE] = local_backend[BE_SLAVE];
        client_rses->rses_backend[BE_MASTER] = local_backend[BE_MASTER];
        router->stats.n_sessions += 1;

        client_rses->rses_capabilities = RCAP_TYPE_STMT_INPUT;
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
        if (rses_begin_locked_router_action(router_cli_ses))
        {
                slave_dcb = router_cli_ses->rses_dcb[BE_SLAVE];
                router_cli_ses->rses_dcb[BE_SLAVE] = NULL;
                master_dcb = router_cli_ses->rses_dcb[BE_MASTER];
                router_cli_ses->rses_dcb[BE_MASTER] = NULL;
                
                router_cli_ses->rses_closed = true;
                /** Unlock */
                rses_end_locked_router_action(router_cli_ses);
                
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
	int                i;
        
        router_cli_ses = (ROUTER_CLIENT_SES *)router_client_session;
        router = (ROUTER_INSTANCE *)router_instance;

        atomic_add(&router_cli_ses->rses_backend[BE_SLAVE]->backend_conn_count, -1);
        atomic_add(&router_cli_ses->rses_backend[BE_MASTER]->backend_conn_count, -1);
        atomic_add(&router_cli_ses->rses_backend[BE_SLAVE]->backend_server->stats.n_current, -1);
        atomic_add(&router_cli_ses->rses_backend[BE_MASTER]->backend_server->stats.n_current, -1);

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
        
	/** 
	 * For each property type, walk through the list, finalize properties 
	 * and free the allocated memory. 
	 */
	for (i=RSES_PROP_TYPE_FIRST; i<RSES_PROP_TYPE_COUNT; i++)
	{
		rses_property_t* p = router_cli_ses->rses_properties[i];
		rses_property_t* q = p;
		
		while (p != NULL)
		{
			q = p->rses_prop_next;
			rses_property_done(p);
			p = q;
		}
	}
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
        GWBUF*             plainsqlbuf = NULL;
        char*              querystr = NULL;
        char*              startpos;
        unsigned char      packet_type;
        uint8_t*           packet;
        int                ret = 0;
        DCB*               master_dcb = NULL;
        DCB*               slave_dcb  = NULL;
        ROUTER_INSTANCE*   inst = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        bool               rses_is_closed;
	rses_property_t*   prop;
        size_t             len;

        CHK_CLIENT_RSES(router_cli_ses);
        

        /** Dirty read for quick check if router is closed. */
        if (router_cli_ses->rses_closed)
        {
                rses_is_closed = true;
        }
        else
        {
                /*< Lock router client session for secure read of DCBs */
                rses_is_closed = 
                !(rses_begin_locked_router_action(router_cli_ses));
        }
        
        if (!rses_is_closed)
        {
                master_dcb = router_cli_ses->rses_dcb[BE_MASTER];
                slave_dcb = router_cli_ses->rses_dcb[BE_SLAVE];
                /** unlock */
                rses_end_locked_router_action(router_cli_ses);
        }
        
        packet = GWBUF_DATA(querybuf);
        packet_type = packet[4];
        
        if (rses_is_closed || (master_dcb == NULL && slave_dcb == NULL))
        {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error: Failed to route %s:%s:\"%s\" to "
                        "backend server. %s.",
                        STRPACKETTYPE(packet_type),
                                                 STRQTYPE(qtype),
                                                 (querystr == NULL ? "(empty)" : querystr),
                                                 (rses_is_closed ? "Router was closed" :
                                                 "Router has no backend servers where to "
                                                 "route to"))));
                goto return_ret;
        }
        inst->stats.n_queries++;
        startpos = (char *)&packet[5];
        
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
                        plainsqlbuf = gwbuf_clone_transform(querybuf, 
                                                            GWBUF_TYPE_PLAINSQL);
                        len = GWBUF_LENGTH(plainsqlbuf);
                        /** unnecessary if buffer includes additional terminating null */
                        querystr = (char *)malloc(len+1);
                        memcpy(querystr, startpos, len);
                        memset(&querystr[len], 0, 1);
                        //                         querystr = (char *)GWBUF_DATA(plainsqlbuf);
                        /*
                        querystr = master_dcb->func.getquerystr(
                                        (void *) gwbuf_clone(querybuf), 
                                        &querystr_is_copy);
                        */
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
        
        LOGIF(LT, (skygw_log_write(LOGFILE_TRACE,
                                "String\t\"%s\"",
                                querystr == NULL ? "(empty)" : querystr)));
        LOGIF(LT, (skygw_log_write(LOGFILE_TRACE,
                                "Packet type\t%s",
                                STRPACKETTYPE(packet_type))));
        
        switch (qtype) {
        case QUERY_TYPE_WRITE:
                LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to Master.",
                                pthread_self(),
                                STRQTYPE(qtype))));
                
                LOGIF(LT, tracelog_routed_query(router_cli_ses, 
                                                "routeQuery", 
                                                master_dcb, 
                                                gwbuf_clone(querybuf)));
                
                ret = master_dcb->func.write(master_dcb, querybuf);
                atomic_add(&inst->stats.n_master, 1);
                
                goto return_ret;
                break;
                
        case QUERY_TYPE_READ:
                LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to Slave.",
                                pthread_self(),
                                STRQTYPE(qtype))));                
                LOGIF(LT, tracelog_routed_query(router_cli_ses, 
                                                "routeQuery", 
                                                slave_dcb, 
                                                gwbuf_clone(querybuf)));
                ret = slave_dcb->func.write(slave_dcb, querybuf);
                LOGIF(LT, (skygw_log_write_flush(
                        LOGFILE_TRACE,
                        "%lu [routeQuery:rwsplit] Routed.",
                        pthread_self())));                
                
                
                atomic_add(&inst->stats.n_slave, 1);
                goto return_ret;
                break;

        case QUERY_TYPE_SESSION_WRITE:
                /**
                 * Execute in backends used by current router session.
                 * Save session variable commands to router session property
                 * struct. Thus, they can be replayed in backends which are 
                 * started and joined later.
                 * 
                 * Suppress redundant OK packets sent by backends.
                 * 
                 * DOES THIS ALL APPLY TO COM_QUIT AS WELL??
                 *
                 * The first OK packet is replied to the client.
                 * 
                 */
                LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] DCB M:%p s:%p, "
                                "Query type\t%s, "
                                "packet type %s, routing to all servers.",
                                pthread_self(),
                                master_dcb,
                                slave_dcb,
                                STRQTYPE(qtype),
                                STRPACKETTYPE(packet_type))));
                /**
                 * COM_QUIT is one-way message. Server doesn't respond to that.
                 * Therefore reply processing is unnecessary and session 
                 * command property is not needed. It is just routed to both
                 * backends.
                 */
                if (packet_type == COM_QUIT)
                {
                        int rc;
                        int rc2;

                        rc = master_dcb->func.write(master_dcb, gwbuf_clone(querybuf));
                        rc2 = slave_dcb->func.write(slave_dcb, gwbuf_clone(querybuf));

                        if (rc == 1 && rc == rc2)
                        {
                                ret = 1;
                        }
                        goto return_ret;
                }
                prop = rses_property_init(RSES_PROP_TYPE_SESCMD);
                /** 
                 * Additional reference is created to querybuf to 
                 * prevent it from being released before properties
                 * are cleaned up as a part of router sessionclean-up.
                 */
                mysql_sescmd_init(prop, querybuf, packet_type, router_cli_ses);
                
                /** Lock router session */
                if (!rses_begin_locked_router_action(router_cli_ses))
                {
                        rses_property_done(prop);
                        goto return_ret;
                }
                /** Add sescmd property to router client session */
                rses_property_add(router_cli_ses, prop);
                
                /** Execute session command in master */
                if (execute_sescmd_in_backend(router_cli_ses, BE_MASTER))
                {
                        ret = 1;                                
                }
                else
                {
                        /** Log error */
                }
                /** Execute session command in slave */
                if (execute_sescmd_in_backend(router_cli_ses, BE_SLAVE))
                {
                        ret = 1;
                }
                else
                {
                        /** Log error */
                }
                
                /** Unlock router session */
                rses_end_locked_router_action(router_cli_ses);
                
                atomic_add(&inst->stats.n_all, 1);
                goto return_ret;
                break;

        default:
                LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "%lu [routeQuery:rwsplit] Query type\t%s, "
                                "routing to Master by default.",
                                pthread_self(),
                                STRQTYPE(qtype))));
                
                /**
                * Is this really ok?
                * What is not known is routed to master.
                */
                LOGIF(LT, tracelog_routed_query(router_cli_ses, 
                                                "routeQuery", 
                                                master_dcb, 
                                                gwbuf_clone(querybuf)));
                
                ret = master_dcb->func.write(master_dcb, querybuf);
                atomic_add(&inst->stats.n_master, 1);
                goto return_ret;
                break;
        } /*< switch by query type */      

return_ret:
        if (plainsqlbuf != NULL)
        {
                gwbuf_free(plainsqlbuf);
        }
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
	if (!succp)
	{
		/** log that router session was closed */
	}
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
	DCB*               slave_dcb;
        DCB*               client_dcb;
        ROUTER_CLIENT_SES* router_cli_ses;
	sescmd_cursor_t*   scur = NULL;
	backend_type_t     be_type = BE_UNDEFINED;
        
	router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);

        /**
         * Lock router client session for secure read of router session members.
         * Note that this could be done without lock by using version #
         */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                while ((writebuf = gwbuf_consume(
                                        writebuf, 
                                        GWBUF_LENGTH(writebuf))) != NULL);
                goto lock_failed;
	}
	master_dcb = router_cli_ses->rses_dcb[BE_MASTER];
	slave_dcb = router_cli_ses->rses_dcb[BE_SLAVE];
        
        /** Holding lock ensures that router session remains open */
        ss_dassert(backend_dcb->session != NULL);
	client_dcb = backend_dcb->session->client;

        /** Unlock */
        rses_end_locked_router_action(router_cli_ses);
        
	/**
         * 1. Check if backend received reply to sescmd.
         * 2. Check sescmd's state whether OK_PACKET has been
         *    sent to client already and if not, lock property cursor,
         *    reply to client, and move property cursor forward. Finally
         *    release the lock.
         * 3. If reply for this sescmd is sent, lock property cursor
         *    and 
         */
	if (client_dcb == NULL)
	{
                while ((writebuf = gwbuf_consume(
                        writebuf, 
                        GWBUF_LENGTH(writebuf))) != NULL);
		/** Log that client was closed before reply */
		return;
	}

	if (backend_dcb == master_dcb)
	{
		be_type = BE_MASTER;
	} 
	else if (backend_dcb == slave_dcb)
	{
		be_type = BE_SLAVE;
	}
	LOGIF(LT, tracelog_routed_query(router_cli_ses, 
                                        "reply_by_statement", 
                                        backend_dcb, 
                                        gwbuf_clone(writebuf)));
	/** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                /** Log to debug that router was closed */
                goto lock_failed;
        }

        scur = rses_get_sescmd_cursor(router_cli_ses, be_type);	        
	/**
         * Active cursor means that reply is from session command 
         * execution. Majority of the time there are no session commands 
         * being executed.
         */
	if (sescmd_cursor_is_active(scur))
	{
                writebuf = sescmd_cursor_process_replies(client_dcb, 
                                                         writebuf, 
                                                         scur);
                
	}
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
        if (writebuf != NULL && client_dcb != NULL)
        {
                /** Write reply to client DCB */
                client_dcb->func.write(client_dcb, writebuf);

                LOGIF(LT, (skygw_log_write_flush(
                        LOGFILE_TRACE,
                        "%lu [clientReply:rwsplit] client dcb %p, "
                        "backend dcb %p. End of normal reply.",
                        pthread_self(),
                        client_dcb,
                        backend_dcb)));
        }
        
lock_failed:
        return;
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
        BACKEND* local_backend[BE_COUNT] = {NULL,NULL};
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
                        LOGIF(LT, (skygw_log_write(
                                           LOGFILE_TRACE,
                                           "%lu [search_backend_servers] Examine server "
                                           "%s:%d with %d connections. Status is %d, "
                                           "router->bitvalue is %d",
                                           pthread_self(),
                                           be->backend_server->name,
                                           be->backend_server->port,
                                           be->backend_conn_count,
                                           be->backend_server->status,
                                           router->bitmask)));
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
                                if (local_backend[BE_SLAVE] == NULL)
                                {
                                        local_backend[BE_SLAVE] = be;
                                }
                                else if (be->backend_conn_count <
                                         local_backend[BE_SLAVE]->backend_conn_count)
                                {
                                        /**
                                         * This running server has fewer
                                         * connections, set it as a new
                                         * candidate.
                                         */
                                        local_backend[BE_SLAVE] = be;
                                }
                                else if (be->backend_conn_count ==
                                         local_backend[BE_SLAVE]->backend_conn_count &&
                                         be->backend_server->stats.n_connections <
                                         local_backend[BE_SLAVE]->backend_server->stats.n_connections)
                                {
                                        /**
                                         * This running server has the same
                                         * number of connections currently
                                         * as the candidate but has had
                                         * fewer connections over time
                                         * than candidate, set this server
                                         * to candidate.
                                         */
                                        local_backend[BE_SLAVE] = be;
                                }
                        }
                        else if (p_master != NULL &&
                                 local_backend[BE_MASTER] == NULL &&
                                 SERVER_IS_MASTER(be->backend_server))
                        {
                                local_backend[BE_MASTER] = be;
                        }
		}
	}
        
        if (p_slave != NULL && local_backend[BE_SLAVE] == NULL) {
                succp = false;
                LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Error : Couldn't find suitable Slave from %d "
                                   "candidates.",
                                   i)));
        }
        
        if (p_master != NULL && local_backend[BE_MASTER] == NULL) {
                succp = false;
                LOGIF(LE, (skygw_log_write_flush(
                                   LOGFILE_ERROR,
                                   "Error : Couldn't find suitable Master from %d "
                                   "candidates.",
                                   i)));
        }

        if (local_backend[BE_SLAVE] != NULL) {
                *p_slave = local_backend[BE_SLAVE];
                LOGIF(LT, (skygw_log_write(
                                   LOGFILE_TRACE,
                                   "%lu [readwritesplit:search_backend_servers] Selected "
                                   "Slave %s:%d from %d candidates.",
                                   pthread_self(),
                                   local_backend[BE_SLAVE]->backend_server->name,
                                   local_backend[BE_SLAVE]->backend_server->port,
                                   i)));
        }
        if (local_backend[BE_MASTER] != NULL) {
                *p_master = local_backend[BE_MASTER];
                LOGIF(LT, (skygw_log_write(
                                   LOGFILE_TRACE,
                                   "%lu [readwritesplit:search_backend_servers] Selected "
                                   "Master %s:%d "
                                   "from %d candidates.",
                                   pthread_self(),
                                   local_backend[BE_MASTER]->backend_server->name,
                                   local_backend[BE_MASTER]->backend_server->port,
                                   i)));
        }
        return succp;
}


/** 
 * Create a generic router session property strcture.
 */
static rses_property_t* rses_property_init(
	rses_property_type_t prop_type)
{
	rses_property_t* prop;
	
	prop = (rses_property_t*)calloc(1, sizeof(rses_property_t));
	if (prop == NULL)
	{
		goto return_prop;
	}
	prop->rses_prop_type = prop_type;
#if defined(SS_DEBUG)
	prop->rses_prop_chk_top = CHK_NUM_ROUTER_PROPERTY;
	prop->rses_prop_chk_tail = CHK_NUM_ROUTER_PROPERTY;
#endif
	
return_prop:
	CHK_RSES_PROP(prop);
	return prop;
}

/**
 * Property is freed at the end of router client session.
 */
static void rses_property_done(
	rses_property_t* prop)
{
	CHK_RSES_PROP(prop);
	
	switch (prop->rses_prop_type) {
	case RSES_PROP_TYPE_SESCMD:
		mysql_sescmd_done(&prop->rses_prop_data.sescmd);
		break;
	default:
		LOGIF(LD, (skygw_log_write_flush(
                                   LOGFILE_DEBUG,
                                   "%lu [rses_property_done] Unknown property type %d "
                                   "in property %p",
                                   pthread_self(),
                                   prop->rses_prop_type,
                                   prop)));
		
		ss_dassert(false);
		break;
	}
	free(prop);
}

/**
 * Add property to the router_client_ses structure's rses_properties
 * array. The slot is determined by the type of property.
 * In each slot there is a list of properties of similar type.
 * 
 * Router client session must be locked.
 */
static void rses_property_add(
        ROUTER_CLIENT_SES* rses,
        rses_property_t*   prop)
{
        rses_property_t* p;
        
        CHK_CLIENT_RSES(rses);
        CHK_RSES_PROP(prop);
        ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));
        
        prop->rses_prop_rsession = rses;
        p = rses->rses_properties[prop->rses_prop_type];
        
        if (p == NULL)
        {
                rses->rses_properties[prop->rses_prop_type] = prop;
        }
        else
        {
                while (p->rses_prop_next != NULL)
                {
                        p = p->rses_prop_next;
                }
                p->rses_prop_next = prop;
        }
}

/** 
 * Router session must be locked.
 * Return session command pointer if succeed, NULL if failed.
 */
static mysql_sescmd_t* rses_property_get_sescmd(
        rses_property_t* prop)
{
        mysql_sescmd_t* sescmd;
        
        CHK_RSES_PROP(prop);
        ss_dassert(prop->rses_prop_rsession == NULL ||
                SPINLOCK_IS_LOCKED(&prop->rses_prop_rsession->rses_lock));
        
        sescmd = &prop->rses_prop_data.sescmd;
        
        if (sescmd != NULL)
        {
                CHK_MYSQL_SESCMD(sescmd);
        }
        return sescmd;
}
       
/**
static void rses_begin_locked_property_action(
        rses_property_t* prop)
{
        CHK_RSES_PROP(prop);
        spinlock_acquire(&prop->rses_prop_lock);
}

static void rses_end_locked_property_action(
        rses_property_t* prop)
{
        CHK_RSES_PROP(prop);
        spinlock_release(&prop->rses_prop_lock);
}
*/

/**
 * Create session command property.
 */
static mysql_sescmd_t* mysql_sescmd_init (
        rses_property_t*   rses_prop,
        GWBUF*             sescmd_buf,
        unsigned char      packet_type,
        ROUTER_CLIENT_SES* rses)
{
        mysql_sescmd_t* sescmd;
        
        CHK_RSES_PROP(rses_prop);
        /** Can't call rses_property_get_sescmd with uninitialized sescmd */
        sescmd = &rses_prop->rses_prop_data.sescmd;
        sescmd->my_sescmd_prop = rses_prop; /*< reference to owning property */
#if defined(SS_DEBUG)
        sescmd->my_sescmd_chk_top  = CHK_NUM_MY_SESCMD;
        sescmd->my_sescmd_chk_tail = CHK_NUM_MY_SESCMD;
#endif
        /** Set session command buffer */
        sescmd->my_sescmd_buf  = sescmd_buf;
        sescmd->my_sescmd_packet_type = packet_type;
        
        return sescmd;
}


static void mysql_sescmd_done(
	mysql_sescmd_t* sescmd)
{
	CHK_RSES_PROP(sescmd->my_sescmd_prop);
	gwbuf_free(sescmd->my_sescmd_buf);
        memset(sescmd, 0, sizeof(mysql_sescmd_t));
}

/**
 * All cases where backend message starts at least with one response to session
 * command are handled here.
 * Read session commands from property list. If command is already replied,
 * discard packet. Else send reply to client. In both cases move cursor forward
 * until all session command replies are handled. 
 * 
 * Cases that are expected to happen and which are handled:
 * s = response not yet replied to client, S = already replied response,
 * q = query
 * 1. q+        for example : select * from mysql.user
 * 2. s+        for example : set autocommit=1
 * 3. S+        
 * 4. sq+
 * 5. Sq+
 * 6. Ss+
 * 7. Ss+q+
 * 8. S+q+
 * 9. s+q+
 */
static GWBUF* sescmd_cursor_process_replies(
        DCB*             client_dcb,
        GWBUF*           replybuf,
        sescmd_cursor_t* scur)
{
        const size_t    headerlen = 4; /*< mysql packet header */
        uint8_t*        packet;
        size_t          packetlen;
        mysql_sescmd_t* scmd;        
        
        ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
        
        scmd = sescmd_cursor_get_command(scur);
               
        CHK_DCB(client_dcb);
        CHK_GWBUF(replybuf);
        
        /** 
         * Walk through packets in the message and the list of session 
         *commands. 
         */
        while (scmd != NULL && replybuf != NULL)
        {                
                if (scmd->my_sescmd_is_replied)
                {
                        /** 
                         * Discard heading packets if their related command is 
                         * already replied. 
                         */
                        CHK_GWBUF(replybuf);
                        packet = (uint8_t *)GWBUF_DATA(replybuf);
                        packetlen = packet[0]+packet[1]*256+packet[2]*256*256;
                        replybuf = gwbuf_consume(replybuf, packetlen+headerlen);
                        
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [sescmd_cursor_process_replies] cmd %p "
                                "is already replied. Discarded %d bytes from "
                                "the %s replybuffer.",
                                pthread_self(),
                                scmd,
                                packetlen+headerlen,
                                STRBETYPE(scur->scmd_cur_be_type))));
                }
                else
                {
                        /** Mark the rest session commands as replied */
                        scmd->my_sescmd_is_replied = true;
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [sescmd_cursor_process_replies] Marked "
                                "cmd %p to as replied. Left message to %s's "
                                "buffer for reply.",
                                pthread_self(),
                                scmd,
                                STRBETYPE(scur->scmd_cur_be_type))));
                }
                
                if (sescmd_cursor_next(scur))
                {
                        scmd = sescmd_cursor_get_command(scur);
                }
                else
                {
                        scmd = NULL;
                        /** All session commands are replied */
                        scur->scmd_cur_active = false;
                }
        }       
        ss_dassert(replybuf == NULL || *scur->scmd_cur_ptr_property == NULL);
        
        return replybuf;
}



/**
 * Get the address of current session command.
 * 
 * Router session must be locked */
static mysql_sescmd_t* sescmd_cursor_get_command(
	sescmd_cursor_t* scur)
{
        mysql_sescmd_t* scmd;
        
        ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
        scur->scmd_cur_cmd = rses_property_get_sescmd(*scur->scmd_cur_ptr_property);
        
        CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
        
        scmd = scur->scmd_cur_cmd;
      
	return scmd;
}

/** router must be locked */
static sescmd_cursor_t* rses_get_sescmd_cursor(
	ROUTER_CLIENT_SES* rses,
	backend_type_t     be_type)
{
	CHK_CLIENT_RSES(rses);
        ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));
        
	return &rses->rses_cursor[be_type];
}

/** router must be locked */
static bool sescmd_cursor_is_active(
	sescmd_cursor_t* sescmd_cursor)
{
	bool succp;
        ss_dassert(SPINLOCK_IS_LOCKED(&sescmd_cursor->scmd_cur_rses->rses_lock));

        succp = sescmd_cursor->scmd_cur_active;
	return succp;
}

/** router must be locked */
static void sescmd_cursor_set_active(
        sescmd_cursor_t* sescmd_cursor,
        bool             value)
{
        ss_dassert(SPINLOCK_IS_LOCKED(&sescmd_cursor->scmd_cur_rses->rses_lock));
        /** avoid calling unnecessarily */
        ss_dassert(sescmd_cursor->scmd_cur_active != value);
        sescmd_cursor->scmd_cur_active = value;
}

/** 
 * Clone session command's command buffer. 
 * Router session must be locked 
 */
static GWBUF* sescmd_cursor_clone_querybuf(
	sescmd_cursor_t* scur)
{
	GWBUF* buf;
	ss_dassert(scur->scmd_cur_cmd != NULL);
	
	buf = gwbuf_clone(scur->scmd_cur_cmd->my_sescmd_buf);
	
	CHK_GWBUF(buf);
	return buf;
}

/**
 * If session command cursor is passive, sends the command to backend for
 * execution. 
 *  
 * Returns true if command was sent or added successfully to the queue.
 * Returns false if command sending failed or if there are no pending session
 * 	commands.
 * 
 * Router session must be locked.
 */ 
static bool execute_sescmd_in_backend(
	ROUTER_CLIENT_SES* rses,
	backend_type_t     be_type)
{
	DCB*             dcb;
	bool             succp = true;
	int              rc = 0;
	sescmd_cursor_t* scur;
        
	dcb = rses->rses_dcb[be_type];
	
	CHK_DCB(dcb);
	CHK_CLIENT_RSES(rses);
	ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));
	
        /** 
         * Get cursor pointer and copy of command buffer to cursor.
         */
	scur = rses_get_sescmd_cursor(rses, be_type);

        /** Return if there are no pending ses commands */
	if (sescmd_cursor_get_command(scur) == NULL)
	{
		succp = false;
                goto return_succp;
	}

	if (!sescmd_cursor_is_active(scur))
        {
                /** Cursor is left active when function returns. */
                sescmd_cursor_set_active(scur, true);
        }
        LOGIF(LT, tracelog_routed_query(rses, 
                                        "execute_sescmd_in_backend", 
                                        dcb, 
                                        sescmd_cursor_clone_querybuf(scur)));
        
        switch (scur->scmd_cur_cmd->my_sescmd_packet_type) {
                case COM_CHANGE_USER:
                        rc = dcb->func.auth(
                                dcb, 
                                NULL, 
                                dcb->session, 
                                sescmd_cursor_clone_querybuf(scur));
                        break;
             
                case COM_QUIT:
                case COM_QUERY:
                case COM_INIT_DB:
                default:
                        rc = dcb->func.write(
                                dcb, 
                                sescmd_cursor_clone_querybuf(scur));
                        break;
        }
        LOGIF(LT, (skygw_log_write_flush(
                LOGFILE_TRACE,
                "%lu [execute_sescmd_in_backend] Routed %s cmd %p.",
                pthread_self(),
                STRPACKETTYPE(scur->scmd_cur_cmd->my_sescmd_packet_type),
                scur->scmd_cur_cmd)));     

        if (rc != 1)
        {
                succp = false;
        }
return_succp:
	return succp;
}

/**
 * Moves cursor to next property and copied address of its sescmd to cursor.
 * Current propery must be non-null.
 * If current property is the last on the list, *scur->scmd_ptr_property == NULL
 * 
 * Router session must be locked 
 */
static bool sescmd_cursor_next(
	sescmd_cursor_t* scur)
{
	bool             succp = false;
	rses_property_t* prop_curr;
	rses_property_t* prop_next;

        ss_dassert(scur != NULL);
        ss_dassert(*(scur->scmd_cur_ptr_property) != NULL);
        ss_dassert(SPINLOCK_IS_LOCKED(
                &(*(scur->scmd_cur_ptr_property))->rses_prop_rsession->rses_lock));

        /** Illegal situation */
	if (scur == NULL ||
           *scur->scmd_cur_ptr_property == NULL ||
            scur->scmd_cur_cmd == NULL)
	{
		/** Log error */
		goto return_succp;
	}
	prop_curr = *(scur->scmd_cur_ptr_property);

        CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
        ss_dassert(prop_curr == mysql_sescmd_get_property(scur->scmd_cur_cmd));
        CHK_RSES_PROP(prop_curr);

        /** Copy address of pointer to next property */
        scur->scmd_cur_ptr_property = &(prop_curr->rses_prop_next);
        prop_next = *scur->scmd_cur_ptr_property;
        ss_dassert(prop_next == *(scur->scmd_cur_ptr_property));
        
        
	/** If there is a next property move forward */
	if (prop_next != NULL)
	{
                CHK_RSES_PROP(prop_next);
                CHK_RSES_PROP((*(scur->scmd_cur_ptr_property)));

                /** Get pointer to next property's sescmd */
                scur->scmd_cur_cmd = rses_property_get_sescmd(prop_next);

                ss_dassert(prop_next == scur->scmd_cur_cmd->my_sescmd_prop);                
                CHK_MYSQL_SESCMD(scur->scmd_cur_cmd);
                CHK_RSES_PROP(scur->scmd_cur_cmd->my_sescmd_prop);
	}
	else
	{
		/** No more properties, can't proceed. */
		goto return_succp;
	}

	if (scur->scmd_cur_cmd != NULL)
	{
                succp = true;
        }
        else
        {
                ss_dassert(false); /*< Log error, sescmd shouldn't be NULL */
        }
return_succp:
	return succp;
}

static rses_property_t* mysql_sescmd_get_property(
	mysql_sescmd_t* scmd)
{
	CHK_MYSQL_SESCMD(scmd);
	return scmd->my_sescmd_prop;
}


static void tracelog_routed_query(
        ROUTER_CLIENT_SES* rses,
        char*              funcname,
        DCB*               dcb,
        GWBUF*             buf)
{
        uint8_t*       packet = GWBUF_DATA(buf);
        unsigned char  packet_type = packet[4];
        size_t         len;
        size_t         buflen = GWBUF_LENGTH(buf);
        char*          querystr;
        char*          startpos = (char *)&packet[5];
        backend_type_t be_type;
                
        if (rses->rses_dcb[BE_MASTER] == dcb)
        {
                be_type = BE_MASTER;
        } 
        else if (rses->rses_dcb[BE_SLAVE] == dcb)
        {
                be_type = BE_SLAVE;
        }
        else
        {
                be_type = BE_UNDEFINED;
        }
        if (GWBUF_TYPE(buf) == GWBUF_TYPE_MYSQL)
        {
                len  = packet[0];
                len += 256*packet[1];
                len += 256*256*packet[2];
                
                if (packet_type == '\x03') 
                {
                        querystr = (char *)malloc(len);
                        memcpy(querystr, startpos, len-1);
                        querystr[len-1] = '\0';
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [%s] %d bytes long buf, \"%s\" -> %s:%d %s dcb %p",
                                pthread_self(),
                                funcname,
                                buflen,
                                querystr,
                                (be_type == BE_MASTER ? 
                                rses->rses_backend[BE_MASTER]->backend_server->name : 
                                        (be_type == BE_SLAVE ? 
                                                rses->rses_backend[BE_SLAVE]->backend_server->name :
                                                "Target DCB is neither of the backends. This is error")),
                                (be_type == BE_MASTER ? 
                                rses->rses_backend[BE_MASTER]->backend_server->port : 
                                        (be_type == BE_SLAVE ? 
                                        rses->rses_backend[BE_SLAVE]->backend_server->port :
                                                -1)),
                                STRBETYPE(be_type),
                                dcb)));
                }
        }
        gwbuf_free(buf);
}

/**
 * Return rc, rc < 0 if router session is closed. rc == 0 if there are no 
 * capabilities specified, rc > 0 when there are capabilities.
 */ 
static uint8_t getCapabilities (
        ROUTER* inst,
        void*   router_session)
{
        ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES *)router_session;
        uint8_t            rc;
        
        if (!rses_begin_locked_router_action(rses))
        {
                rc = 0xff;
                goto return_rc;
        }
        rc = rses->rses_capabilities;
        
        rses_end_locked_router_action(rses);
        
return_rc:
        return rc;
}