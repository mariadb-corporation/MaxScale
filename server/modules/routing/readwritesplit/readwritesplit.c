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

int bref_cmp(
        const void* bref1,
        const void* bref2);

static bool select_connect_backend_servers(
        backend_ref_t**    p_master_ref,
        backend_ref_t*     backend_ref,
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
        backend_ref_t* backend_ref);

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

static void tracelog_routed_query(
        ROUTER_CLIENT_SES* rses,
        char*              funcname,
        backend_ref_t*     bref,
        GWBUF*             buf);

static bool route_session_write(
        ROUTER_CLIENT_SES* router_client_ses,
        GWBUF*             querybuf,
        ROUTER_INSTANCE*   inst,
        unsigned char      packet_type,
        skygw_query_type_t qtype);

static void refreshInstance(
        ROUTER_INSTANCE*  router,
        CONFIG_PARAMETER* param);

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

static void refreshInstance(
        ROUTER_INSTANCE*  router,
        CONFIG_PARAMETER* param)
{
        config_param_type_t paramtype;
        
        paramtype = config_get_paramtype(param);
        
        if (paramtype == COUNT_TYPE)
        {
                if (strncmp(param->name, "max_slave_connections", MAX_PARAM_LEN) == 0)
                {
                        router->rwsplit_config.rw_max_slave_conn_percent = 0;
                        router->rwsplit_config.rw_max_slave_conn_count = 
                                config_get_valint(param, NULL, paramtype);
                }
        } 
        else if (paramtype == PERCENT_TYPE)
        {
                if (strncmp(param->name, "max_slave_connections", MAX_PARAM_LEN) == 0)
                {
                        router->rwsplit_config.rw_max_slave_conn_count = 0;
                        router->rwsplit_config.rw_max_slave_conn_percent = 
                        config_get_valint(param, NULL, paramtype);
                }
        }
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
        	LOGIF(LM, (skygw_log_write(
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
         * Copy all config parameters from service to router instance.
         * Finally, copy version number to indicate that configs match.
         */
	param = config_get_param(service->svc_config_param, "max_slave_connections");
        
        if (param != NULL)
        {
                refreshInstance(router, param);
                router->rwsplit_version = service->svc_config_version;
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
        backend_ref_t*      backend_ref; /*< array of backend references (DCB,BACKEND,cursor) */
        backend_ref_t*      master_ref = NULL; /*< pointer to selected master */
        BACKEND**           b;
        ROUTER_CLIENT_SES*  client_rses = NULL;
        ROUTER_INSTANCE*    router      = (ROUTER_INSTANCE *)router_inst;
        bool                succp;
        int                 router_nservers = 0; /*< # of servers in total */
        int                 max_nslaves;      /*< max # of slaves used in this session */
        int                 conf_max_nslaves; /*< value from configuration file */
        int                 i;
        const int           min_nservers = 1; /*< hard-coded for now */
        
        client_rses = (ROUTER_CLIENT_SES *)calloc(1, sizeof(ROUTER_CLIENT_SES));
        
        if (client_rses == NULL)
        {
                ss_dassert(false);
                goto return_rses;
        }
#if defined(SS_DEBUG)
        client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
        client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif        
        /** 
         * If service config has been changed, reload config from service to 
         * router instance first.
         */
        spinlock_acquire(&router->lock);
        if (router->service->svc_config_version > router->rwsplit_version)
        {
                CONFIG_PARAMETER* param = router->service->svc_config_param;
                
                while (param != NULL)
                {
                        refreshInstance(router, param);
                        param = param->next;
                }
                router->rwsplit_version = router->service->svc_config_version;  
        }
        /** Copy config struct from router instance */
        client_rses->rses_config = router->rwsplit_config;
        spinlock_release(&router->lock);
        /** 
         * Set defaults to session variables. 
         */
        client_rses->rses_autocommit_enabled = true;
        client_rses->rses_transaction_active = false;
        
        /** count servers */
        b = router->servers;
        while (*(b++) != NULL) router_nservers++;
        
        /** With too few servers session is not created */
        if (router_nservers < min_nservers || 
                MAX(client_rses->rses_config.rw_max_slave_conn_count, 
                    (router_nservers*client_rses->rses_config.rw_max_slave_conn_percent)/100)
                        < min_nservers)
        {
                if (router_nservers < min_nservers)
                {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Unable to start %s service. There are "
                                "too few backend servers available. Found %d "
                                "when %d is required.",
                                router->service->name,
                                router_nservers,
                                min_nservers)));
                }
                else
                {
                        double pct = client_rses->rses_config.rw_max_slave_conn_percent/100;
                        double nservers = (double)router_nservers*pct;
                        
                        if (client_rses->rses_config.rw_max_slave_conn_count < 
                                min_nservers)
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Unable to start %s service. There are "
                                        "too few backend servers configured in "
                                        "MaxScale.cnf. Found %d when %d is required.",
                                        router->service->name,
                                        client_rses->rses_config.rw_max_slave_conn_count,
                                        min_nservers)));
                        }
                        if (nservers < min_nservers)
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Unable to start %s service. There are "
                                        "too few backend servers configured in "
                                        "MaxScale.cnf. Found %d%% when at least %.0f%% "
                                        "would be required.",
                                        router->service->name,
                                        client_rses->rses_config.rw_max_slave_conn_percent,
                                        min_nservers/(((double)router_nservers)/100))));
                        }
                }
                free(client_rses);
                client_rses = NULL;
                goto return_rses;
        }
        /**
         * Create backend reference objects for this session.
         */
        backend_ref = (backend_ref_t *)calloc (1, router_nservers*sizeof(backend_ref_t));
        
        if (backend_ref == NULL)
        {
                /** log this */                        
                free(client_rses);
                client_rses = NULL;
                goto return_rses;
        }        
        /** 
         * Initialize backend references with BACKEND ptr.
         * Initialize session command cursors for each backend reference.
         */
        for (i=0; i< router_nservers; i++)
        {
#if defined(SS_DEBUG)
                backend_ref[i].bref_chk_top = CHK_NUM_BACKEND_REF;
                backend_ref[i].bref_chk_tail = CHK_NUM_BACKEND_REF;
                backend_ref[i].bref_sescmd_cur.scmd_cur_chk_top  = CHK_NUM_SESCMD_CUR;
                backend_ref[i].bref_sescmd_cur.scmd_cur_chk_tail = CHK_NUM_SESCMD_CUR;
#endif
                backend_ref[i].bref_backend = router->servers[i];
                /** store pointers to sescmd list to both cursors */
                backend_ref[i].bref_sescmd_cur.scmd_cur_rses = client_rses;
                backend_ref[i].bref_sescmd_cur.scmd_cur_active = false;
                backend_ref[i].bref_sescmd_cur.scmd_cur_ptr_property =
                        &client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
                backend_ref[i].bref_sescmd_cur.scmd_cur_cmd = NULL;   
        }        
        /** 
         * Find out the number of read backend servers.
         * Depending on the configuration value type, either copy direct count 
         * of slave connections or calculate the count from percentage value.
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
                
        spinlock_init(&client_rses->rses_lock);
        client_rses->rses_backend_ref = backend_ref;
        
        /**
         * Find a backend servers to connect to.
         */
        succp = select_connect_backend_servers(&master_ref,
                                               backend_ref,
                                               router_nservers,
                                               max_nslaves,
                                               session,
                                               router);
        
        /** Both Master and at least  1 slave must be found */
        if (!succp) {
                free(client_rses->rses_backend_ref);
                free(client_rses);
                client_rses = NULL;
                goto return_rses;                
        }                                        
        /** Copy backend pointers to router session. */
        client_rses->rses_master_ref   = master_ref;
        client_rses->rses_backend_ref  = backend_ref;
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

return_rses:    
#if defined(SS_DEBUG)
        if (client_rses != NULL)
        {
                CHK_CLIENT_RSES(client_rses);
        }
#endif
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
        backend_ref_t*     backend_ref;
        
        router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);        
        
        backend_ref = router_cli_ses->rses_backend_ref;
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

                for (i=0; i<router_cli_ses->rses_nbackends; i++)
                {
                        /** decrease server current connection counters */
                        atomic_add(&backend_ref[i].bref_backend->backend_server->stats.n_current, -1);
                        
                        /** Close those which had been connected */
                        if (backend_ref[i].bref_dcb != NULL)
                        {
                                CHK_DCB(backend_ref[i].bref_dcb);
                                dcbs[i] = backend_ref[i].bref_dcb;
                                backend_ref[i].bref_dcb = 
                                        (DCB *)0xdeadbeef; /*< prevent new uses of DCB */
                                dcbs[i]->func.close(dcbs[i]);
                        }
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
        backend_ref_t*     backend_ref;
        
        router_cli_ses = (ROUTER_CLIENT_SES *)router_client_session;
        router         = (ROUTER_INSTANCE *)router_instance;
        backend_ref    = router_cli_ses->rses_backend_ref;
        
        for (i=0; i<router_cli_ses->rses_nbackends; i++)
        {
                atomic_add(&backend_ref[i].bref_backend->backend_conn_count, -1);
        }
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
        free(router_cli_ses->rses_backend_ref);
	free(router_cli_ses);
        return;
}


static bool get_dcb(
        DCB**              p_dcb,
        ROUTER_CLIENT_SES* rses,
        backend_type_t     btype)
{
        backend_ref_t* backend_ref;
        int            smallest_nconn = -1;
        int            i;
        bool           succp = false;
        
        CHK_CLIENT_RSES(rses);
        ss_dassert(p_dcb != NULL && *(p_dcb) == NULL);
        
        if (p_dcb == NULL)
        {
                goto return_succp;
        }
        backend_ref = rses->rses_backend_ref;

        if (btype == BE_SLAVE)
        {
                for (i=0; i<rses->rses_nbackends; i++)
                {
                        BACKEND* b = backend_ref[i].bref_backend;
                        
                        if (backend_ref[i].bref_dcb != NULL &&
                                SERVER_IS_SLAVE(b->backend_server) &&
                                (smallest_nconn == -1 || 
                                b->backend_conn_count < smallest_nconn))
                        {
                                *p_dcb = backend_ref[i].bref_dcb;
                                smallest_nconn = b->backend_conn_count;
                                succp = true;
                        }
                }
                ss_dassert(succp);
        }
        else if (btype == BE_MASTER || BE_JOINED)
        {
                for (i=0; i<rses->rses_nbackends; i++)
                {
                        BACKEND* b = backend_ref[i].bref_backend;

                        if (backend_ref[i].bref_dcb != NULL &&
                                (SERVER_IS_MASTER(b->backend_server) ||
                                SERVER_IS_JOINED(b->backend_server)))
                        {
                                *p_dcb = backend_ref[i].bref_dcb;
                                succp = true;
                                goto return_succp;
                        }
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
        skygw_query_type_t qtype          = QUERY_TYPE_UNKNOWN;
        GWBUF*             plainsqlbuf    = NULL;
        char*              querystr       = NULL;
        char*              startpos;
        unsigned char      packet_type;
        uint8_t*           packet;
        int                ret = 0;
        DCB*               master_dcb     = NULL;
        DCB*               slave_dcb      = NULL;
        ROUTER_INSTANCE*   inst = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        bool               rses_is_closed = false;
        size_t             len;

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

        master_dcb = router_cli_ses->rses_master_ref->bref_dcb;
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
#if defined(AUTOCOMMIT_OPT)
        if ((QUERY_IS_TYPE(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT) &&
                !router_cli_ses->rses_autocommit_enabled) ||
                (QUERY_IS_TYPE(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) &&
                router_cli_ses->rses_autocommit_enabled))
        {
                /** reply directly to client */
        }
#endif
        /**
         * If autocommit is disabled or transaction is explicitly started
         * transaction becomes active and master gets all statements until
         * transaction is committed and autocommit is enabled again.
         */
        if (router_cli_ses->rses_autocommit_enabled &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
        {
                router_cli_ses->rses_autocommit_enabled = false;
                
                if (!router_cli_ses->rses_transaction_active)
                {
                        router_cli_ses->rses_transaction_active = true;
                }
        } 
        else if (!router_cli_ses->rses_transaction_active &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_BEGIN_TRX))
        {
                router_cli_ses->rses_transaction_active = true;
        }
        /** 
         * Explicit COMMIT and ROLLBACK, implicit COMMIT.
         */
        if (router_cli_ses->rses_autocommit_enabled &&
                router_cli_ses->rses_transaction_active &&
                (QUERY_IS_TYPE(qtype,QUERY_TYPE_COMMIT) ||
                QUERY_IS_TYPE(qtype,QUERY_TYPE_ROLLBACK)))
        {
                router_cli_ses->rses_transaction_active = false;
        } 
        else if (!router_cli_ses->rses_autocommit_enabled &&
                QUERY_IS_TYPE(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT))
        {
                router_cli_ses->rses_autocommit_enabled = true;
                router_cli_ses->rses_transaction_active = false;
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
        else if (QUERY_IS_TYPE(qtype, QUERY_TYPE_READ) && 
                !router_cli_ses->rses_transaction_active)
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
                        if (router_cli_ses->rses_transaction_active) /*< all to master */
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
        backend_ref_t*     backend_ref;
        int                i;
        
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
	/** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                /** Log to debug that router was closed */
                goto lock_failed;
        }
        backend_ref = router_cli_ses->rses_backend_ref;

        /** find backend_dcb's corresponding BACKEND */
        i = 0;
        while (i<router_cli_ses->rses_nbackends &&
                backend_ref[i].bref_dcb != backend_dcb)
        {
                i++;
        }
        ss_dassert(backend_ref[i].bref_dcb == backend_dcb);
        
        LOGIF(LT, tracelog_routed_query(router_cli_ses, 
                                        "reply_by_statement", 
                                        &backend_ref[i],
                                        gwbuf_clone(writebuf)));

        scur = &backend_ref[i].bref_sescmd_cur;
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


int bref_cmp(
        const void* bref1,
        const void* bref2)
{
        BACKEND* b1 = ((backend_ref_t *)bref1)->bref_backend;
        BACKEND* b2 = ((backend_ref_t *)bref2)->bref_backend;

        return ((b1->backend_conn_count < b2->backend_conn_count) ? -1 :
                ((b1->backend_conn_count > b2->backend_conn_count) ? 1 : 0));
}

/** 
 * @node Search suitable backend servers from those of router instance.
 *
 * Parameters:
 * @param p_master_ref - in, use, out
 *      Pointer to location where master's backend reference is to  be stored.
 *      NULL is not allowed.
 *
 * @param backend_ref - in, use, out 
 *      Pointer to backend server reference object array.
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
        backend_ref_t**    p_master_ref,
        backend_ref_t*     backend_ref,
        int                router_nservers,
        int                max_nslaves,
        SESSION*           session,
        ROUTER_INSTANCE*   router)
{        
        bool            succp = true;
        bool            master_found = false;
        bool            master_connected = false;
        int             slaves_found = 0;
        int             slaves_connected = 0;
        int             i;
        const int       min_nslaves = 0; /*< not configurable at the time */
        bool            is_synced_master;
        
        if (p_master_ref == NULL || backend_ref == NULL)
        {
                ss_dassert(FALSE);
                succp = false;
                goto return_succp;
        }
        
        if (router->bitvalue != 0) /*< 'synced' is the only bitvalue in rwsplit */
        {
                is_synced_master = true;
        } 
        else
        {
                is_synced_master = false;
        }                        
        
        /**
         * Sort the pointer list to servers according to connection counts. As 
         * a consequence those backends having least connections are in the 
         * beginning of the list.
         */
        qsort((void *)backend_ref, (size_t)router_nservers, sizeof(backend_ref_t), bref_cmp);
        
        /**
         * Choose at least 1+1 (master and slave) and at most 1+max_nslaves 
         * servers from the sorted list. First master found is selected.
         */
        for (i=0; 
             i<router_nservers && (slaves_connected < max_nslaves || !master_connected);
             i++)
        {
                BACKEND* b = backend_ref[i].bref_backend;
                
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "%lu [select_backend_servers] Examine server "
                        "%s:%d with %d connections. Status is %d, "
                        "router->bitvalue is %d",
                        pthread_self(),
                        b->backend_server->name,
                        b->backend_server->port,
                        b->backend_conn_count,
                        b->backend_server->status,
                                router->bitmask)));
                
                if (SERVER_IS_RUNNING(b->backend_server) &&
                        ((b->backend_server->status & router->bitmask) ==
                        router->bitvalue))
                {
                        if (slaves_found < max_nslaves &&
                                SERVER_IS_SLAVE(b->backend_server))
                        {
                                slaves_found += 1;
                                backend_ref[i].bref_dcb = dcb_connect(
                                        b->backend_server,
                                        session,
                                        b->backend_server->protocol);
                                
                                if (backend_ref[i].bref_dcb != NULL) 
                                {
                                        slaves_connected += 1;
                                        /** Increase backend connection counter */
                                        atomic_add(&b->backend_conn_count, 1);
                                }
                                else
                                {
                                        /* handle connect error */
                                }
                        }
                        else if (!master_connected &&
                                (SERVER_IS_MASTER(b->backend_server) ||
                                SERVER_IS_JOINED(b->backend_server)))
                        {
                                master_found = true;
                                  
                                backend_ref[i].bref_dcb = dcb_connect(
                                        b->backend_server,
                                        session,
                                        b->backend_server->protocol);
                                
                                if (backend_ref[i].bref_dcb != NULL) 
                                {
                                        master_connected = true;
                                        *p_master_ref = &backend_ref[i];
                                        /** Increase backend connection counter */
                                        atomic_add(&b->backend_conn_count, 1);
                                }
                                else
                                {
                                        /* handle connect error */
                                }
                        }
                }
        } /*< for */
        
        /**
         * Successful cases
         */
        if (master_connected && 
                slaves_connected >= min_nslaves && 
                slaves_connected <= max_nslaves)
        {
                succp = true;
                
                if (slaves_connected == 0 && slaves_found > 0)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR,
                                "Warning : Couldn't connect to any of the %d "
                                "slaves. Routing to %s only.",
                                slaves_found,
                                (is_synced_master ? "Galera nodes" : "Master"))));
                        
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "* Warning : Couldn't connect to any of the %d "
                                "slaves. Routing to %s only.",
                                slaves_found,
                                (is_synced_master ? "Galera nodes" : "Master"))));
                }
                else if (slaves_found == 0)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR,
                                "Warning : Couldn't find any slaves from existing "
                                "%d servers. Routing to %s only.",
                                router_nservers,
                                (is_synced_master ? "Galera nodes" : "Master"))));
                        
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "* Warning : Couldn't find any slaves from existing "
                                "%d servers. Routing to %s only.",
                                router_nservers,
                                (is_synced_master ? "Galera nodes" : "Master"))));                        
                }
                else if (slaves_connected < max_nslaves)
                {
                        LOGIF(LT, (skygw_log_write_flush(
                                LOGFILE_TRACE,
                                "Note : Couldn't connect to maximum number of "
                                "slaves. Connected successfully to %d slaves "
                                "of %d of them.",
                                slaves_connected,
                                slaves_found)));
                }
                
                if (LOG_IS_ENABLED(LT))
                {
                        for (i=0; i<router_nservers; i++)
                        {
                                BACKEND* b = backend_ref[i].bref_backend;
                                
                                if (backend_ref[i].bref_dcb != NULL)
                                {
                                        backend_type_t btype = BACKEND_TYPE(b);
                                        
                                        LOGIF(LT, (skygw_log_write(
                                                LOGFILE_TRACE,
                                                "Selected %s in \t%s:%d",
                                                (btype == BE_MASTER ? "master" : 
                                                (btype == BE_SLAVE ? "slave" : 
                                                (btype == BE_JOINED ? "galera node" :
                                                "unknown node type"))),
                                                b->backend_server->name,
                                                b->backend_server->port)));
                                }
                        } /* for */
                }
        }
        /**
         * Failure cases
         */
        else
        {                         
                if (!master_found)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR,
                                "Error : Couldn't find suitable %s from %d "
                                "candidates.",
                                (is_synced_master ? "Galera node" : "Master"),
                                router_nservers)));
                        
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "* Error : Couldn't find suitable %s from %d "
                                "candidates.",
                                (is_synced_master ? "Galera node" : "Master"),
                                router_nservers)));
 
                        LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "Error : Couldn't find suitable %s from %d "
                                "candidates.",
                                (is_synced_master ? "Galera node" : "Master"),
                                router_nservers)));
                }
                else if (!master_connected)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR,
                                "Error : Couldn't connect to any %s although "
                                "there exists at least one %s node in the "
                                "cluster.",
                                (is_synced_master ? "Galera node" : "Master"),
                                (is_synced_master ? "Galera node" : "Master"))));
                        
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "* Error : Couldn't connect to any %s although "
                                "there exists at least one %s node in the "
                                "cluster.",
                                (is_synced_master ? "Galera node" : "Master"),
                                (is_synced_master ? "Galera node" : "Master"))));

                        LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "Error : Couldn't connect to any %s although "
                                "there exists at least one %s node in the "
                                "cluster.",
                                (is_synced_master ? "Galera node" : "Master"),
                                (is_synced_master ? "Galera node" : "Master"))));
                }

                if (slaves_connected < min_nslaves)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR,
                                "Error : Couldn't establish required amount of "
                                "slave connections for router session.")));
                        
                        LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "*Error : Couldn't establish required amount of "
                                "slave connections for router session.")));
                }
                
                /** Clean up connections */
                for (i=0; i<router_nservers; i++)
                {
                        if (backend_ref[i].bref_dcb != NULL)
                        {
                                /** disconnect opened connections */
                                backend_ref[i].bref_dcb->func.close(backend_ref[i].bref_dcb);
                                atomic_add(&backend_ref[i].bref_backend->backend_conn_count, -1);
                        }
                }
                master_connected = false;
                slaves_connected = 0;
        }
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
        backend_ref_t* backend_ref)
{
	DCB*             dcb;
	bool             succp = true;
	int              rc = 0;
	sescmd_cursor_t* scur;
        
        if (backend_ref->bref_dcb == NULL)
        {
                goto return_succp;
        }
        dcb = backend_ref->bref_dcb;
        
	CHK_DCB(dcb);
 	CHK_BACKEND_REF(backend_ref);
	
        /** 
         * Get cursor pointer and copy of command buffer to cursor.
         */
        scur = &backend_ref->bref_sescmd_cur;

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
        
        LOGIF(LT, tracelog_routed_query(scur->scmd_cur_rses, 
                                        "execute_sescmd_in_backend", 
                                        backend_ref, 
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
        backend_ref_t*     bref,
        GWBUF*             buf)
{
        uint8_t*       packet = GWBUF_DATA(buf);
        unsigned char  packet_type = packet[4];
        size_t         len;
        size_t         buflen = GWBUF_LENGTH(buf);
        char*          querystr;
        char*          startpos = (char *)&packet[5];
        BACKEND*       b;
        backend_type_t be_type;
        DCB*           dcb;
        
        CHK_BACKEND_REF(bref);
        b = bref->bref_backend;
        CHK_BACKEND(b);
        dcb = bref->bref_dcb;
        CHK_DCB(dcb);
        
        be_type = BACKEND_TYPE(b);

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
                                b->backend_server->name,
                                b->backend_server->port, 
                                STRBETYPE(be_type),
                                dcb)));
                        free(querystr);
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
        backend_ref_t*    backend_ref;
        int               i;
  
        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "Session write, query type\t%s, packet type %s, "
                "routing to all servers.",
                STRQTYPE(qtype),
                STRPACKETTYPE(packet_type))));

        backend_ref = router_cli_ses->rses_backend_ref;
        
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

                for (i=0; i<router_cli_ses->rses_nbackends; i++)
                {
                        DCB* dcb = backend_ref[i].bref_dcb;
                        
                        if (dcb != NULL)
                        {
                                rc = dcb->func.write(dcb, gwbuf_clone(querybuf));
                        
                                if (rc != 1)
                                {
                                        succp = false;
                                }
                        }
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
 
        for (i=0; i<router_cli_ses->rses_nbackends; i++)
        {
                succp = execute_sescmd_in_backend(&backend_ref[i]);

                if (!succp)
                {
                        /** Unlock router session */
                        rses_end_locked_router_action(router_cli_ses);
                        goto return_succp;
                }
        }
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
        atomic_add(&inst->stats.n_all, 1);
        
return_succp:
        return succp;
}

