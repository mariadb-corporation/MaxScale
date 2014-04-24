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
 * 17/07/2014	Massimiliano Pinto	Server connection counter is updated in closeSession
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

static int backend_cmp(
        const void* be_1,
        const void* be_2);

static bool select_connect_backend_servers(
        BACKEND**          p_master,
        BACKEND**          b,
        int                router_nservers,
        int                max_nslaves,
        SESSION*           session,
        ROUTER_INSTANCE*   router);

static bool get_dcb(
        DCB**              dcb,
        ROUTER_CLIENT_SES* rses,
        backend_type_t     btype);


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

static bool execute_sescmd_in_backend(
        BACKEND* backend);

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

#if 0 /*< disabled for now due multiple slaves changes */
static void tracelog_routed_query(
        ROUTER_CLIENT_SES* rses,
        char*              funcname,
        DCB*               dcb,
        GWBUF*             buf);
#endif

static bool route_session_write(
        ROUTER_CLIENT_SES* router_client_ses,
        GWBUF*             querybuf,
        ROUTER_INSTANCE*   inst,
        unsigned char      packet_type,
        skygw_query_type_t qtype);

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
        ROUTER_INSTANCE*    router;
        SERVER*             server;
        int                 nservers;
        int                 i;
        CONFIG_PARAMETER*   param;
        config_param_type_t paramtype;
        
        if ((router = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL) {
                return NULL; 
        } 
        router->service = service;
        spinlock_init(&router->lock);
        
        /** Calculate number of servers */
        server = service->databases;
        nservers = 0;
        
        while (server != NULL)
        {
                nservers++;
                server=server->nextdb;
        }
        router->servers = (BACKEND **)calloc(nservers + 1, sizeof(BACKEND *));
        
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
        nservers= 0;
        
        while (server != NULL) {
                if ((router->servers[nservers] = malloc(sizeof(BACKEND))) == NULL)
                {
                        /** clean up */
                        for (i = 0; i < nservers; i++) {
                                free(router->servers[i]);
                        }
                        free(router->servers);
                        free(router);
                        return NULL;
                }
                router->servers[nservers]->backend_server = server;
                router->servers[nservers]->backend_conn_count = 0;
                router->servers[nservers]->be_valid = false;
                router->servers[nservers]->be_dcb = NULL;
#if defined(SS_DEBUG)
                router->servers[nservers]->be_chk_top = CHK_NUM_BACKEND;
                router->servers[nservers]->be_chk_tail = CHK_NUM_BACKEND;
#endif
                nservers += 1;
                server = server->nextdb;
        }        
        router->servers[nservers] = NULL;
        
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
                        if (!strcasecmp(options[i], "synced")) 
                        {
				router->bitmask |= (SERVER_JOINED);
				router->bitvalue |= SERVER_JOINED;
			}
			else
			{
                                LOGIF(LE, (skygw_log_write_flush(
                                                   LOGFILE_ERROR,
                                                   "Warning : Unsupported "
                                                   "router option \"%s\" "
                                                   "for readwritesplit router.",
                                                   options[i])));
			}
		}
	}
	/**
         * Copy config parameter value from service struct. This becomes the 
         * default value for every new rwsplit router session.
         */
	param = config_get_param(service->svc_config_param, "max_slave_connections");
        
        if (param != NULL)
        {
                paramtype = config_get_paramtype(param);
                
                if (paramtype == COUNT_TYPE)
                {
                        router->rwsplit_config.rw_max_slave_conn_count = 
                                config_get_valint(param, NULL, paramtype);
                } 
                else if (paramtype == PERCENT_TYPE)
                {
                        router->rwsplit_config.rw_max_slave_conn_percent = 
                                config_get_valint(param, NULL, paramtype);
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
        BACKEND**           pp_backend;
        BACKEND**           b;
        BACKEND*            master      = NULL; /*< pointer to selected master */
        ROUTER_CLIENT_SES*  client_rses = NULL;
        ROUTER_INSTANCE*    router      = (ROUTER_INSTANCE *)router_inst;
        bool                succp;
        int                 router_nservers = 0; /*< # of servers in total */
        int                 max_nslaves;      /*< max # of slaves used in this session */
        int                 conf_max_nslaves; /*< value from configuration file */
        
        b = router->servers;
        
        /** count servers */
        while (*(b++) != NULL) router_nservers++;
               
        /** Master + Slave is minimum requirement */
        if (router_nservers < 2)
        {
                /** log */
                goto return_rses;
        }
        client_rses = (ROUTER_CLIENT_SES *)calloc(1, sizeof(ROUTER_CLIENT_SES));

        if (client_rses == NULL)
        {
                ss_dassert(false);
                goto return_rses;
        }
        /** Copy config struct from router instance */
        client_rses->rses_config = router->rwsplit_config;
        
        /** 
         * Either copy direct count of slave connections or calculate the count
         * from percentage value.
         */
        if (client_rses->rses_config.rw_max_slave_conn_count > 0)
        {
                conf_max_nslaves = client_rses->rses_config.rw_max_slave_conn_count;
        }
        else
        {
                conf_max_nslaves = 
                        (router_nservers*client_rses->rses_config.rw_max_slave_conn_percent)/100;
        }              
        max_nslaves = MIN(router_nservers-1, MAX(1, conf_max_nslaves));        
        pp_backend = (BACKEND **)calloc(1, (router_nservers)*sizeof(BACKEND *));
        
        /** 
         * Copy backend pointer array from global router instance to private use.
         */
        memcpy(pp_backend, router->servers, router_nservers*sizeof(BACKEND *));
        ss_dassert(pp_backend[router_nservers] == NULL);
        
        spinlock_init(&client_rses->rses_lock);
#if defined(SS_DEBUG)
        client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
        client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif
        b = pp_backend;
        
        /** Set up ses cmd objects for each backend */
        while (*b != NULL)
        {
                /** store pointers to sescmd list to both cursors */
                (*b)->be_sescmd_cursor.scmd_cur_rses = client_rses;
                (*b)->be_sescmd_cursor.scmd_cur_active = false;
                (*b)->be_sescmd_cursor.scmd_cur_ptr_property =
                        &client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
                (*b)->be_sescmd_cursor.scmd_cur_cmd = NULL;   
#if defined(SS_DEBUG)
                (*b)->be_sescmd_cursor.scmd_cur_chk_top  = CHK_NUM_SESCMD_CUR;
                (*b)->be_sescmd_cursor.scmd_cur_chk_tail = CHK_NUM_SESCMD_CUR;
#endif
                b++;
        }
	/**
         * Find a backend servers to connect to.
         */
        succp = select_connect_backend_servers(&master,
                                        pp_backend,
                                        router_nservers,
                                        max_nslaves,
                                        session,
                                        router);

        /** Both Master and at least  1 slave must be found */
        if (!succp) {
                free(client_rses);
                client_rses = NULL;
                goto return_rses;                
        }                                        
        /** Copy backend pointers to router session. */
        client_rses->rses_master       = master;
        client_rses->rses_backend      = pp_backend;
        client_rses->rses_nbackends    = router_nservers; /*< # of backend servers */
        client_rses->rses_capabilities = RCAP_TYPE_STMT_INPUT;
        router->stats.n_sessions      += 1;
        /**
         * Version is bigger than zero once initialized.
         */
        atomic_add(&client_rses->rses_versno, 2);
        ss_dassert(client_rses->rses_versno == 2);
	/**
         * Add this session to end of the list of active sessions in router.
         */
	spinlock_acquire(&router->lock);
        client_rses->next   = router->connections;
        router->connections = client_rses;
        spinlock_release(&router->lock);

        CHK_CLIENT_RSES(client_rses);

return_rses:        
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
        BACKEND**          b;
        
        router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);        
        
        b = router_cli_ses->rses_backend;
        /**
         * Lock router client session for secure read and update.
         */
        if (!router_cli_ses->rses_closed &&
                rses_begin_locked_router_action(router_cli_ses))
        {
                DCB* dcbs[router_cli_ses->rses_nbackends];
                int  i = 0;

                /** 
                 * This sets router closed. Nobody is allowed to use router
                 * whithout checking this first.
                 */
                router_cli_ses->rses_closed = true;
                
                while (*b != NULL)
                {
                        /** decrease server current connection counters */
                        atomic_add(&(*b)->backend_server->stats.n_current, -1);
                        
                        /** Close those which had been connected */
                        if ((*b)->be_dcb != NULL)
                        {
                                CHK_DCB((*b)->be_dcb);
                                dcbs[i] = (*b)->be_dcb;
                                (*b)->be_dcb = NULL; /*< prevent new uses of DCB */
                                dcbs[i]->func.close(dcbs[i]);
                        }
                        b++;
                }
                /** Unlock */
                rses_end_locked_router_action(router_cli_ses);                
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

static bool get_dcb(
        DCB**              p_dcb,
        ROUTER_CLIENT_SES* rses,
        backend_type_t     btype)
{
        BACKEND** b;
        int       smallest_nconn = -1;
        bool      succp = false;
        
        CHK_CLIENT_RSES(rses);
        ss_dassert(*(p_dcb) == NULL);
        b = rses->rses_backend;

        if (btype == BE_SLAVE)
        {
                while (*b != NULL)
                {             
                        if ((*b)->be_dcb != NULL &&
                                SERVER_IS_SLAVE((*b)->backend_server) &&
                                (smallest_nconn == -1 || 
                                (*b)->backend_conn_count < smallest_nconn))
                        {
                                *p_dcb = (*b)->be_dcb;
                                smallest_nconn = (*b)->backend_conn_count;
                                succp = true;
                        }
                        b++;
                }
                ss_dassert(succp);
        }
        else if (btype == BE_MASTER || BE_JOINED)
        {
                while (*b != NULL)
                {
                        if ((*b)->be_dcb != NULL &&
                                (SERVER_IS_MASTER((*b)->backend_server) ||
                                SERVER_IS_JOINED((*b)->backend_server)))
                        {
                                *p_dcb = (*b)->be_dcb;
                                succp = true;
                                goto return_succp;
                        }
                        b++;
                }
        }
return_succp:
        return succp;
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
        bool               rses_is_closed = false;
        size_t             len;
        /** if false everything goes to master and session commands to slave too */
        static bool        autocommit_enabled = true; 
        /** if true everything goes to master and session commands to slave too */
        static bool        transaction_active = false;

        CHK_CLIENT_RSES(router_cli_ses);

        /** Dirty read for quick check if router is closed. */
        if (router_cli_ses->rses_closed)
        {
                rses_is_closed = true;
        }
        packet = GWBUF_DATA(querybuf);
        packet_type = packet[4];
        
        if (rses_is_closed)
        {
                LOGIF(LE, 
                        (skygw_log_write_flush(
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

        master_dcb = router_cli_ses->rses_master->be_dcb;
        CHK_DCB(master_dcb);
        
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
                         *                        querystr = master_dcb->func.getquerystr(
                         *                                        (void *) gwbuf_clone(querybuf), 
                         *                                        &querystr_is_copy);
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
        /**
         * If autocommit is disabled or transaction is explicitly started
         * transaction becomes active and master gets all statements until
         * transaction is committed and autocommit is enabled again.
         */
        if (autocommit_enabled &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
        {
                autocommit_enabled = false;
                
                if (!transaction_active)
                {
                        transaction_active = true;
                }
        } 
        else if (!transaction_active &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_BEGIN_TRX))
        {
                transaction_active = true;
        }
        /** 
         * Explicit COMMIT and ROLLBACK, implicit COMMIT.
         */
        if (autocommit_enabled &&
                transaction_active &&
                (QUERY_IS_TYPE(qtype,QUERY_TYPE_COMMIT) ||
                QUERY_IS_TYPE(qtype,QUERY_TYPE_ROLLBACK)))
        {
                transaction_active = false;
        } 
        else if (!autocommit_enabled &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT))
        {
                autocommit_enabled = true;
                transaction_active = false;
        }
        /**
         * Session update is always routed in the same way.
         */
        if (QUERY_IS_TYPE(qtype, QUERY_TYPE_SESSION_WRITE))
        {
                bool succp = route_session_write(
                                router_cli_ses, 
                                querybuf, 
                                inst, 
                                packet_type, 
                                qtype);

                if (succp)
                {
                        ret = 1;
                }
                ss_dassert(succp);
                ss_dassert(ret == 1);
                goto return_ret;
        }
        else if (QUERY_IS_TYPE(qtype, QUERY_TYPE_READ) && !transaction_active)
        {
                bool succp;
                
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "Read-only query, routing to Slave.")));
                ss_dassert(QUERY_IS_TYPE(qtype, QUERY_TYPE_READ));
                
                succp = get_dcb(&slave_dcb, router_cli_ses, BE_SLAVE);
                
                if (succp)
                {
                        if ((ret = slave_dcb->func.write(slave_dcb, querybuf)) == 1)
                        {
                                atomic_add(&inst->stats.n_slave, 1);
                        }
                        ss_dassert(ret == 1);
                }
                ss_dassert(succp);
                goto return_ret;
        }       
        else 
        {
                bool succp = true;
                
                if (LOG_IS_ENABLED(LOGFILE_TRACE))
                {
                        if (transaction_active) /*< all to master */
                        {
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Transaction is active, routing to Master.")));
                        }
                        else
                        {
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Begin transaction, write or unspecified type, "
                                        "routing to Master.")));
                        }
                }
                
                if (master_dcb == NULL)
                {
                        succp = get_dcb(&master_dcb, router_cli_ses, BE_MASTER);
                }
                if (succp)
                {
                        if ((ret = master_dcb->func.write(master_dcb, querybuf)) == 1)
                        {
                                atomic_add(&inst->stats.n_master, 1);
                        }
                }        
                ss_dassert(succp);
                ss_dassert(ret == 1);
                goto return_ret;
        }
return_ret:
        if (plainsqlbuf != NULL)
        {
                gwbuf_free(plainsqlbuf);
        }
        if (querystr != NULL)
        {
                free(querystr);
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
        DCB*               client_dcb;
        ROUTER_CLIENT_SES* router_cli_ses;
	sescmd_cursor_t*   scur = NULL;
	backend_type_t     be_type;
        BACKEND**          be;
        
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
                goto lock_failed;
	}
        be = router_cli_ses->rses_backend;
        
        while (*be !=NULL)
        {
                if ((*be)->be_dcb == backend_dcb)
                {
                        be_type = (SERVER_IS_MASTER((*be)->backend_server) ? BE_MASTER : 
                        (SERVER_IS_SLAVE((*be)->backend_server) ? BE_SLAVE :
                        (SERVER_IS_JOINED((*be)->backend_server) ? BE_JOINED : BE_UNDEFINED)));
                        break;
                }
                be++;
        }
// 	LOGIF(LT, tracelog_routed_query(router_cli_ses, 
//                                         "reply_by_statement", 
//                                         backend_dcb, 
//                                         gwbuf_clone(writebuf)));
	/** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                /** Log to debug that router was closed */
                goto lock_failed;
        }
        scur = &(*be)->be_sescmd_cursor;
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


int backend_cmp(
        const void* be_1,
        const void* be_2)
{
        BACKEND* b1 = *(BACKEND **)be_1;
        BACKEND* b2 = *(BACKEND **)be_2;
        
        return ((b1->backend_conn_count < b2->backend_conn_count) ? -1 :
                ((b1->backend_conn_count > b2->backend_conn_count) ? 1 : 0));
}

/** 
 * @node Search suitable backend servers from those of router instance.
 *
 * Parameters:
 * @param p_master - in, use, out
 *      Pointer to location where master's address is to  be stored.
 *      NULL is not allowed.
 *
 * @param b - in, use, out 
 *      Pointer to location where all backend server pointers are stored.
 *      NULL is not allowed.
 *
 * @param router_nservers - in, use
 *      Number of backend server pointers pointed to by b.
 * 
 * @param max_nslaves - in, use
 *      Upper limit for the number of slaves. Configuration parameter or default.
 *
 * @param session - in, use
 *      MaxScale session pointer used when connection to backend is established.
 *
 * @param  router - in, use
 *      Pointer to router instance. Used when server states are qualified.
 * 
 * @return true, if at least one master and one slave was found.
 *
 * 
 * @details It is assumed that there is only one master among servers of
 *      a router instance. As a result, the first master found is chosen.
 */
static bool select_connect_backend_servers(
        BACKEND**          p_master,
        BACKEND**          b,
        int                router_nservers,
        int                max_nslaves,
        SESSION*           session,
        ROUTER_INSTANCE*   router)
{        
        bool      succp = true;
        bool      master_found = false;
        bool      master_connected = false;
        int       slaves_found = 0;
        int       slaves_connected = 0;
        
        /**
         * Sort the pointer list to servers according to connection counts. As 
         * a consequence those backends having least connections are in the 
         * beginning of the list.
         */
        qsort((void *)b, (size_t)router_nservers, sizeof(void*), backend_cmp);
        
        /**
         * Choose at least 1+1 (master and slave) and at most 1+max_nslaves 
         * servers from the sorted list. First master found is selected.
         */
        while (*b != NULL && (slaves_connected < max_nslaves || !master_connected))
        {                
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [select_backend_servers] Examine server "
                        "%s:%d with %d connections. Status is %d, "
                        "router->bitvalue is %d",
                        pthread_self(),
                        (*b)->backend_server->name,
                        (*b)->backend_server->port,
                        (*b)->backend_conn_count,
                        (*b)->backend_server->status,
                        router->bitmask)));
                
                if (SERVER_IS_RUNNING((*b)->backend_server) &&
                        (((*b)->backend_server->status & router->bitmask) ==
                        router->bitvalue))
                {
                        if (slaves_found < max_nslaves &&
                                SERVER_IS_SLAVE((*b)->backend_server))
                        {
                                slaves_found += 1;
                                
                                (*b)->be_dcb = dcb_connect(
                                        (*b)->backend_server,
                                        session,
                                        (*b)->backend_server->protocol);
                                
                                if ((*b)->be_dcb != NULL) 
                                {
                                        slaves_connected += 1;
                                        /** Increase backend connection counter */
                                        atomic_add(&(*b)->backend_conn_count, 1);
                                }
                                else
                                {
                                        /* handle connect error */
                                }
                        }
                        else if (!master_connected &&
                                (SERVER_IS_MASTER((*b)->backend_server) ||
                                SERVER_IS_JOINED((*b)->backend_server)))
                        {
                                master_found = true;
                                  
                                (*b)->be_dcb = dcb_connect(
                                        (*b)->backend_server,
                                        session,
                                        (*b)->backend_server->protocol);
                                
                                if ((*b)->be_dcb != NULL) 
                                {
                                        master_connected = true;
                                        *p_master = *b;
                                        /** Increase backend connection counter */
                                        atomic_add(&(*b)->backend_conn_count, 1);
                                }
                                else
                                {
                                        /* handle connect error */
                                }
                        }
                }
                b++;
        } /*< while */
        
        if (master_connected && slaves_connected > 0 && slaves_connected <= max_nslaves)
        {
                succp = true;
        }
        else
        {
                /** disconnect and clean up */
        }
#if 0        
        if (router->bitvalue != 0 && 
                p_master != NULL && 
                local_backend[BE_JOINED] == NULL)
        {
                succp = false;
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Couldn't find a Joined Galera node from %d "
                        "candidates.",
                        i)));
                goto return_succp;
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
#endif
return_succp:
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
/*                        
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [sescmd_cursor_process_replies] cmd %p "
                                "is already replied. Discarded %d bytes from "
                                "the %s replybuffer.",
                                pthread_self(),
                                scmd,
                                packetlen+headerlen,
                                STRBETYPE(scur->scmd_cur_be_type))));
                                */
                }
                else
                {
                        /** Mark the rest session commands as replied */
                        scmd->my_sescmd_is_replied = true;
                        /*
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "%lu [sescmd_cursor_process_replies] Marked "
                                "cmd %p to as replied. Left message to %s's "
                                "buffer for reply.",
                                pthread_self(),
                                scmd,
                                STRBETYPE(scur->scmd_cur_be_type))));
                                */
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
        BACKEND* backend)
{
	DCB*             dcb;
	bool             succp = true;
	int              rc = 0;
	sescmd_cursor_t* scur;
        
        if (backend->be_dcb == NULL)
        {
                goto return_succp;
        }
        dcb = backend->be_dcb;
        
	CHK_DCB(dcb);
 	CHK_BACKEND(backend);
	
        /** 
         * Get cursor pointer and copy of command buffer to cursor.
         */
        scur = &backend->be_sescmd_cursor;

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
        /*
        LOGIF(LT, tracelog_routed_query(rses, 
                                        "execute_sescmd_in_backend", 
                                        dcb, 
                                        sescmd_cursor_clone_querybuf(scur)));
        */
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

#if 0
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
#endif
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

/**
 * Execute in backends used by current router session.
 * Save session variable commands to router session property
 * struct. Thus, they can be replayed in backends which are 
 * started and joined later.
 * 
 * Suppress redundant OK packets sent by backends.
 * 
 * The first OK packet is replied to the client.
 * Return true if succeed, false is returned if router session was closed or
 * if execute_sescmd_in_backend failed.
 */
static bool route_session_write(
        ROUTER_CLIENT_SES* router_cli_ses,
        GWBUF*             querybuf,
        ROUTER_INSTANCE*   inst,
        unsigned char      packet_type,
        skygw_query_type_t qtype)
{
        bool              succp;
        rses_property_t*  prop;
        BACKEND**         b;

        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "Session write, query type\t%s, packet type %s, "
                "routing to all servers.",
                STRQTYPE(qtype),
                STRPACKETTYPE(packet_type))));

        b = router_cli_ses->rses_backend;
        
        /**
         * COM_QUIT is one-way message. Server doesn't respond to that.
         * Therefore reply processing is unnecessary and session 
         * command property is not needed. It is just routed to both
         * backends.
         */
        if (packet_type == COM_QUIT)
        {
                int rc;
               
                succp = true;

                while (*b != NULL)
                {
                        DCB* dcb = (*b)->be_dcb;
                        
                        if (dcb != NULL)
                        {
                                rc = dcb->func.write(dcb, gwbuf_clone(querybuf));
                        
                                if (rc != 1)
                                {
                                        succp = false;
                                }
                        }
                        b++;
                }
                gwbuf_free(querybuf);
                goto return_succp;
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
                succp = false;
                goto return_succp;
        }
        /** Add sescmd property to router client session */
        rses_property_add(router_cli_ses, prop);
        
        while (*b != NULL)
        {
                succp = execute_sescmd_in_backend((*b));

                if (!succp)
                {
                        /** Unlock router session */
                        rses_end_locked_router_action(router_cli_ses);
                        goto return_succp;
                }
                b++;
        }
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
        atomic_add(&inst->stats.n_all, 1);
        
return_succp:
        return succp;
}
