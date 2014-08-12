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
#include <modinfo.h>
#include <mysql_client_server_protocol.h>

MODULE_INFO 	info = {
	MODULE_API_ROUTER,
	MODULE_BETA_RELEASE,
	ROUTER_VERSION,
	"A Read/Write splitting router for enhancement read scalability"
};
#if defined(SS_DEBUG)
#  include <mysql_client_server_protocol.h>
#endif


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

static  void           handleError(
        ROUTER*        instance,
        void*          router_session,
        GWBUF*         errmsgbuf,
        DCB*           backend_dcb,
        error_action_t action,
        bool*          succp);

static void print_error_packet(ROUTER_CLIENT_SES* rses, GWBUF* buf, DCB* dcb);
static int  router_get_servercount(ROUTER_INSTANCE* router);
static int  rses_get_max_slavecount(ROUTER_CLIENT_SES* rses, int router_nservers);
static int  rses_get_max_replication_lag(ROUTER_CLIENT_SES* rses);
static backend_ref_t* get_bref_from_dcb(ROUTER_CLIENT_SES* rses, DCB* dcb);

static route_target_t get_route_target (
        skygw_query_type_t qtype,
        bool               trx_active,
        HINT*              hint);


static  uint8_t getCapabilities (ROUTER* inst, void* router_session);

#if defined(NOT_USED)
static bool router_option_configured(
        ROUTER_INSTANCE* router,
        const char*      optionstr,
        void*            data);
#endif

#if defined(PREP_STMT_CACHING)
static prep_stmt_t* prep_stmt_init(prep_stmt_type_t type, void* id);
static void         prep_stmt_done(prep_stmt_t* pstmt);
#endif /*< PREP_STMT_CACHING */

int bref_cmp_global_conn(
        const void* bref1,
        const void* bref2);

int bref_cmp_router_conn(
        const void* bref1,
        const void* bref2);

int bref_cmp_behind_master(
        const void* bref1,
        const void* bref2);

int bref_cmp_current_load(
        const void* bref1,
        const void* bref2);

/**
 * The order of functions _must_ match with the order the select criteria are
 * listed in select_criteria_t definition in readwritesplit.h
 */
int (*criteria_cmpfun[LAST_CRITERIA])(const void*, const void*)=
{
        NULL,
        bref_cmp_global_conn,
        bref_cmp_router_conn,
        bref_cmp_behind_master,
        bref_cmp_current_load
};

static bool select_connect_backend_servers(
        backend_ref_t**    p_master_ref,
        backend_ref_t*     backend_ref,
        int                router_nservers,
        int                max_nslaves,
        int                max_rlag,
        select_criteria_t  select_criteria,
        SESSION*           session,
        ROUTER_INSTANCE*   router);

static bool get_dcb(
        DCB**              dcb,
        ROUTER_CLIENT_SES* rses,
        backend_type_t     btype,
        char*              name,
        int                max_rlag);

static void rwsplit_process_router_options(
        ROUTER_INSTANCE* router,
        char**           options);



static ROUTER_OBJECT MyObject = {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        routeQuery,
        diagnostic,
        clientReply,
	handleError,
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

static bool execute_sescmd_history(backend_ref_t* bref);

static bool execute_sescmd_in_backend(
        backend_ref_t* backend_ref);

static void sescmd_cursor_reset(sescmd_cursor_t* scur);

static bool sescmd_cursor_history_empty(sescmd_cursor_t* scur);

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

static GWBUF* sescmd_cursor_process_replies(GWBUF* replybuf, backend_ref_t* bref);

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

static void bref_clear_state(backend_ref_t* bref, bref_state_t state);
static void bref_set_state(backend_ref_t*   bref, bref_state_t state);
static sescmd_cursor_t* backend_ref_get_sescmd_cursor (backend_ref_t* bref);

static int  router_handle_state_switch(DCB* dcb, DCB_REASON reason, void* data);
static bool handle_error_new_connection(
        ROUTER_INSTANCE*   inst,
        ROUTER_CLIENT_SES* rses,
        DCB*               backend_dcb,
        GWBUF*             errmsg);
static bool handle_error_reply_client(SESSION* ses, GWBUF* errmsg);

static BACKEND* get_root_master(
        backend_ref_t* servers,
        int            router_nservers);

static bool have_enough_servers(
        ROUTER_CLIENT_SES** rses,
        const int           nsrv,
        int                 router_nsrv,
        ROUTER_INSTANCE*    router);

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
        CONFIG_PARAMETER* singleparam)
{
        CONFIG_PARAMETER*   param;
        bool                refresh_single;
        
        if (singleparam != NULL)
        {
                param = singleparam;
                refresh_single = true;
        }
        else
        {
                param = router->service->svc_config_param;
                refresh_single = false;
        }
        
        while (param != NULL)         
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
                        else if (strncmp(param->name, 
                                        "max_slave_replication_lag", 
                                        MAX_PARAM_LEN) == 0)
                        {
                                router->rwsplit_config.rw_max_slave_replication_lag = 
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
                
                if (refresh_single)
                {
                        break;
                }
                param = param->next;
        }
        
#if defined(NOT_USED) /*< can't read monitor config parameters */
        if ((*router->servers)->backend_server->rlag == -2)
        {
                rlag_enabled = false;
        }
        else
        {
                rlag_enabled = true;
        }
        /** 
         * If replication lag detection is not enabled the measure can't be
         * used in slave selection.
         */
        if (!rlag_enabled)
        {
                if (rlag_limited)
                {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Warning : Configuration Failed, max_slave_replication_lag "
                                "is set to %d,\n\t\t      but detect_replication_lag "
                                "is not enabled. Replication lag will not be checked.",
                                router->rwsplit_config.rw_max_slave_replication_lag)));
                }
            
                if (router->rwsplit_config.rw_slave_select_criteria == 
                        LEAST_BEHIND_MASTER)
                {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Warning : Configuration Failed, router option "
                                "\n\t\t      slave_selection_criteria=LEAST_BEHIND_MASTER "
                                "is specified, but detect_replication_lag "
                                "is not enabled.\n\t\t      "
                                "slave_selection_criteria=%s will be used instead.",
                                STRCRITERIA(DEFAULT_CRITERIA))));
                        
                        router->rwsplit_config.rw_slave_select_criteria =
                                DEFAULT_CRITERIA;
                }
        }
#endif /*< NOT_USED */
}

/**
 * Create an instance of read/write statement router within the MaxScale.
 *
 * 
 * @param service	The service this router is being create for
 * @param options	The options for this query router
 *
 * @return NULL in failure, pointer to router in success.
 */
static ROUTER *
createInstance(SERVICE *service, char **options)
{
        ROUTER_INSTANCE*    router;
        SERVER*             server;
        int                 nservers;
        int                 i;
        CONFIG_PARAMETER*   param;
	char		    *weightby;
        
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
                router->servers[nservers]->weight = 1000;
#if defined(SS_DEBUG)
                router->servers[nservers]->be_chk_top = CHK_NUM_BACKEND;
                router->servers[nservers]->be_chk_tail = CHK_NUM_BACKEND;
#endif
                nservers += 1;
                server = server->nextdb;
        }
        router->servers[nservers] = NULL;

	/*
	 * If server weighting has been defined calculate the percentage
	 * of load that will be sent to each server. This is only used for
	 * calculating the least connections, either globally or within a
	 * service, or the numebr of current operations on a server.
	 */
	if ((weightby = serviceGetWeightingParameter(service)) != NULL)
	{
		int 	n, total = 0;
		BACKEND	*backend;

		for (n = 0; router->servers[n]; n++)
		{
			backend = router->servers[n];
			total += atoi(serverGetParameter(
					backend->backend_server, weightby));
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
			for (n = 0; router->servers[n]; n++)
			{
				int perc;
				backend = router->servers[n];
				perc = (atoi(serverGetParameter(
						backend->backend_server,
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
        
        /** Call this before refreshInstance */
	if (options)
	{
                rwsplit_process_router_options(router, options);
	}
	/** 
         * Set default value for max_slave_connections and for slave selection
         * criteria. If parameter is set in config file max_slave_connections 
         * will be overwritten.
         */
        router->rwsplit_config.rw_max_slave_conn_count = CONFIG_MAX_SLAVE_CONN;
        
        if (router->rwsplit_config.rw_slave_select_criteria == UNDEFINED_CRITERIA)
        {
                router->rwsplit_config.rw_slave_select_criteria = DEFAULT_CRITERIA;
        }
        /**
         * Copy all config parameters from service to router instance.
         * Finally, copy version number to indicate that configs match.
         */
        param = config_get_param(service->svc_config_param, "max_slave_connections");
        
        if (param != NULL)
        {
                refreshInstance(router, param);
        }
        /** 
         * Read default value for slave replication lag upper limit and then
         * configured value if it exists.
         */
        router->rwsplit_config.rw_max_slave_replication_lag = CONFIG_MAX_SLAVE_RLAG;
        param = config_get_param(service->svc_config_param, "max_slave_replication_lag");
        
        if (param != NULL)
        {
                refreshInstance(router, param);
        }
        router->rwsplit_version = service->svc_config_version;
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
        backend_ref_t*      master_ref  = NULL; /*< pointer to selected master */
        ROUTER_CLIENT_SES*  client_rses = NULL;
        ROUTER_INSTANCE*    router      = (ROUTER_INSTANCE *)router_inst;
        bool                succp;
        int                 router_nservers = 0; /*< # of servers in total */
        int                 max_nslaves;      /*< max # of slaves used in this session */
        int                 max_slave_rlag;   /*< max allowed replication lag for any slave */
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
                /** re-read all parameters to rwsplit config structure */
                refreshInstance(router, NULL); /*< scan through all parameters */
                /** increment rwsplit router's config version number */
                router->rwsplit_version = router->service->svc_config_version;  
                /** Read options */
                rwsplit_process_router_options(router, router->service->routerOptions);
        }
        /** Copy config struct from router instance */
        client_rses->rses_config = router->rwsplit_config;
        
        spinlock_release(&router->lock);
        /** 
         * Set defaults to session variables. 
         */
        client_rses->rses_autocommit_enabled = true;
        client_rses->rses_transaction_active = false;
        
        router_nservers = router_get_servercount(router);
        
        if (!have_enough_servers(&client_rses, 
                                min_nservers, 
                                router_nservers, 
                                router))
        {
                goto return_rses;
        }
        /**
         * Create backend reference objects for this session.
         */
        backend_ref = (backend_ref_t *)calloc(1, router_nservers*sizeof(backend_ref_t));
        
        if (backend_ref == NULL)
        {
                /** log this */                        
                free(client_rses);
                free(backend_ref);
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
                backend_ref[i].bref_state = 0;
                backend_ref[i].bref_backend = router->servers[i];
                /** store pointers to sescmd list to both cursors */
                backend_ref[i].bref_sescmd_cur.scmd_cur_rses = client_rses;
                backend_ref[i].bref_sescmd_cur.scmd_cur_active = false;
                backend_ref[i].bref_sescmd_cur.scmd_cur_ptr_property =
                        &client_rses->rses_properties[RSES_PROP_TYPE_SESCMD];
                backend_ref[i].bref_sescmd_cur.scmd_cur_cmd = NULL;   
        }   
        max_nslaves    = rses_get_max_slavecount(client_rses, router_nservers);
        max_slave_rlag = rses_get_max_replication_lag(client_rses);
        
        spinlock_init(&client_rses->rses_lock);
        client_rses->rses_backend_ref = backend_ref;
        
        /**
         * Find a backend servers to connect to.
         * This command requires that rsession's lock is held.
         */
        rses_begin_locked_router_action(client_rses);

        succp = select_connect_backend_servers(&master_ref,
                                               backend_ref,
                                               router_nservers,
                                               max_nslaves,
                                               max_slave_rlag,
                                               client_rses->rses_config.rw_slave_select_criteria,
                                               session,
                                               router);

        rses_end_locked_router_action(client_rses);
        
        /** Both Master and at least  1 slave must be found */
        if (!succp) {
                free(client_rses->rses_backend_ref);
                free(client_rses);
                client_rses = NULL;
                goto return_rses;                
        }                                        
        /** Copy backend pointers to router session. */
        client_rses->rses_master_ref   = master_ref;
	/* assert with master_host */
	ss_dassert(master_ref && (master_ref->bref_backend->backend_server && SERVER_MASTER));
        client_rses->rses_capabilities = RCAP_TYPE_STMT_INPUT;
        client_rses->rses_backend_ref  = backend_ref;
        client_rses->rses_nbackends    = router_nservers; /*< # of backend servers */
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

        /** 
         * router session can be NULL if newSession failed and it is discarding
         * its connections and DCB's. 
         */
        if (router_session == NULL)
        {
                return;
        }
        router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);
        
        backend_ref = router_cli_ses->rses_backend_ref;
        /**
         * Lock router client session for secure read and update.
         */
        if (!router_cli_ses->rses_closed &&
                rses_begin_locked_router_action(router_cli_ses))
        {
                int  i = 0;
                /**
                 * session must be moved to SESSION_STATE_STOPPING state before
                 * router session is closed.
                 */
#if defined(SS_DEBUG)
                SESSION* ses = get_session_by_router_ses((void*)router_cli_ses);
                
                ss_dassert(ses != NULL);
                ss_dassert(ses->state == SESSION_STATE_STOPPING);
#endif

                /** 
                 * This sets router closed. Nobody is allowed to use router
                 * whithout checking this first.
                 */
                router_cli_ses->rses_closed = true;

                for (i=0; i<router_cli_ses->rses_nbackends; i++)
                {
                        backend_ref_t* bref = &backend_ref[i];
                        DCB* dcb = bref->bref_dcb;
             
                        /** Close those which had been connected */
                        if (BREF_IS_IN_USE(bref))
                        {
                                CHK_DCB(dcb);
                                /** Clean operation counter in bref and in SERVER */
                                while (BREF_IS_WAITING_RESULT(bref))
                                {
                                        bref_clear_state(bref, BREF_WAITING_RESULT);
                                }
                                bref_clear_state(bref, BREF_IN_USE);
                                bref_set_state(bref, BREF_CLOSED);
                                /**
                                 * closes protocol and dcb
                                 */
                                dcb_close(dcb);
                                /** decrease server current connection counters */
                                atomic_add(&bref->bref_backend->backend_server->stats.n_current, -1);
                                atomic_add(&bref->bref_backend->backend_conn_count, -1);
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
                if (!BREF_IS_IN_USE((&backend_ref[i])))
                {
                        continue;
                }
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

/**
 * Provide the router with a pointer to a suitable backend dcb. 
 * Detect failures in server statuses and reselect backends if necessary.
 * If name is specified, server name becomes primary selection criteria.
 * 
 * @param p_dcb Address of the pointer to the resulting DCB
 * @param rses  Pointer to router client session
 * @param btype Backend type
 * @param name  Name of the backend which is primarily searched. May be NULL.
 * 
 * @return True if proper DCB was found, false otherwise.
 */
static bool get_dcb(
        DCB**              p_dcb,
        ROUTER_CLIENT_SES* rses,
        backend_type_t     btype,
        char*              name,
        int                max_rlag)
{
        backend_ref_t* backend_ref;
        int            smallest_nconn = -1;
        int            i;
        bool           succp = false;
	BACKEND*       master_host;
        
        CHK_CLIENT_RSES(rses);
        ss_dassert(p_dcb != NULL && *(p_dcb) == NULL);
        
        if (p_dcb == NULL)
        {
                goto return_succp;
        }
        backend_ref = rses->rses_backend_ref;

	/** get root master from available servers */
	master_host = get_root_master(backend_ref, rses->rses_nbackends);

        if (btype == BE_SLAVE)
        {
                if (name != NULL) /*< Choose backend by name (hint) */
                {
                        for (i=0; i<rses->rses_nbackends; i++)
                        {
                                BACKEND* b = backend_ref[i].bref_backend;
                                
                                /**
                                 * To become chosen:
                                 * backend must be in use, name must match,
                                 * root master node must be found,
                                 * backend's role must be either slave, relay 
                                 * server, or master.
                                 */
                                if (BREF_IS_IN_USE((&backend_ref[i])) &&
                                        (strncasecmp(
                                                name,
                                                b->backend_server->unique_name, 
                                                MIN(strlen(b->backend_server->unique_name), PATH_MAX)) == 0) &&
                                        master_host != NULL && 
#if 0
                                        (max_rlag == MAX_RLAG_UNDEFINED ||
                                        (b->backend_server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                                        b->backend_server->rlag <= max_rlag)) &&
#endif
                                        (SERVER_IS_SLAVE(b->backend_server) || 
                                        SERVER_IS_RELAY_SERVER(b->backend_server) ||
                                        SERVER_IS_MASTER(b->backend_server)))
                                {
                                        *p_dcb = backend_ref[i].bref_dcb;
                                        succp = true; 
                                        ss_dassert(backend_ref[i].bref_dcb->state != DCB_STATE_ZOMBIE);
                                        break;
                                }
                        }
                }
                
                if (!succp) /*< No hints or finding named backend failed */
                {
                        for (i=0; i<rses->rses_nbackends; i++)
                        {
                                BACKEND* b = backend_ref[i].bref_backend;
                                /**
                                 * To become chosen:
                                 * backend must be in use, 
                                 * root master node must be found,
                                 * backend is not allowed to be the master,
                                 * backend's role can be either slave or relay
                                 * server and it must have least connections
                                 * at the moment.
                                 */
                                if (BREF_IS_IN_USE((&backend_ref[i])) &&
                                        master_host != NULL && 
                                        b->backend_server != master_host->backend_server &&
                                        (max_rlag == MAX_RLAG_UNDEFINED ||
                                        (b->backend_server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                                        b->backend_server->rlag <= max_rlag)) &&
                                        (SERVER_IS_SLAVE(b->backend_server) || 
                                        SERVER_IS_RELAY_SERVER(b->backend_server)) &&
                                        (smallest_nconn == -1 || 
                                        b->backend_conn_count < smallest_nconn))
                                {
                                        *p_dcb = backend_ref[i].bref_dcb;
                                        smallest_nconn = b->backend_conn_count;
                                        succp = true;
                                        ss_dassert(backend_ref[i].bref_dcb->state != DCB_STATE_ZOMBIE);
                                }
                        }
                }
                
                if (!succp) /*< No valid slave was found, search master next */
                {
                        btype = BE_MASTER;

                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Warning : No slaves connected nor "
                                "available. Choosing master %s:%d "
                                "instead.",
                                backend_ref->bref_backend->backend_server->name,
                                backend_ref->bref_backend->backend_server->port)));
                }
        }

        if (btype == BE_MASTER)
        {
                for (i=0; i<rses->rses_nbackends; i++)
                {
                        BACKEND* b = backend_ref[i].bref_backend;
	
                        if (BREF_IS_IN_USE((&backend_ref[i])) &&
				(master_host && (b->backend_server == master_host->backend_server)))
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
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 * 
 *  @param qtype    Type of query 
 *  @param trx_active Is transacation active or not
 *  @param hint       Pointer to list of hints attached to the query buffer
 * 
 *  @return bitfield including the routing target, or the target server name 
 *          if the query would otherwise be routed to slave.
 */
static route_target_t get_route_target (
        skygw_query_type_t qtype,
        bool               trx_active,
        HINT*              hint)
{
        route_target_t target;
        
        if (QUERY_IS_TYPE(qtype, QUERY_TYPE_SESSION_WRITE)    ||
                QUERY_IS_TYPE(qtype, QUERY_TYPE_PREPARE_STMT) ||
                QUERY_IS_TYPE(qtype, QUERY_TYPE_PREPARE_NAMED_STMT))
        {
                /** hints don't affect on routing */
                target = TARGET_ALL;
        }
        else if (QUERY_IS_TYPE(qtype, QUERY_TYPE_READ) && !trx_active)
        {
                target = TARGET_SLAVE;
                
                /** process routing hints */
                while (hint != NULL)
                {
                        if (hint->type == HINT_ROUTE_TO_MASTER)
                        {
                                target = TARGET_MASTER; /*< override */
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Hint: route to master.")));
                                break;
                        }
                        else if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
                        {
                                target |= TARGET_NAMED_SERVER; /*< add */
                        }
                        else if (hint->type == HINT_ROUTE_TO_UPTODATE_SERVER)
                        {
                                /** not implemented */
                        }
                        else if (hint->type == HINT_ROUTE_TO_ALL)
                        {
                                /** not implemented */
                        }
                        else if (hint->type == HINT_PARAMETER)
                        {
                                if (strncasecmp(
                                        (char *)hint->data, 
                                        "max_slave_replication_lag", 
                                        strlen("max_slave_replication_lag")) == 0)
                                {
                                        target |= TARGET_RLAG_MAX;
                                }
                                else
                                {
                                        LOGIF(LT, (skygw_log_write(
                                                LOGFILE_TRACE,
                                                "Error : Unknown hint parameter "
                                                "'%s' when 'max_slave_replication_lag' "
                                                "was expected.",
                                                (char *)hint->data)));
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : Unknown hint parameter "
                                                "'%s' when 'max_slave_replication_lag' "
                                                "was expected.",
                                                (char *)hint->data)));                                        
                                }
                        } 
                        else if (hint->type == HINT_ROUTE_TO_SLAVE)
                        {
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Hint: route to slave.")));                                
                        }
                        hint = hint->next;
                } /*< while (hint != NULL) */
        }
        else
        {
                /** hints don't affect on routing */
                target = TARGET_MASTER;
        }
        
        return target;
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
 * @return if succeed 1, otherwise 0
 * If routeQuery fails, it means that router session has failed.
 * In any tolerated failure, handleError is called and if necessary,
 * an error message is sent to the client.
 * 
 * For now, routeQuery don't tolerate errors, so any error will close
 * the session. vraa 14.6.14
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
        mysql_server_cmd_t packet_type;
        uint8_t*           packet;
        int                ret = 0;
        DCB*               master_dcb     = NULL;
        DCB*               target_dcb     = NULL;
        ROUTER_INSTANCE*   inst = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        bool               rses_is_closed = false;
        size_t             len;
        MYSQL*             mysql = NULL;
        route_target_t     route_target;

        CHK_CLIENT_RSES(router_cli_ses);

        /** Dirty read for quick check if router is closed. */
        if (router_cli_ses->rses_closed)
        {
                rses_is_closed = true;
        }
        
        ss_dassert(!GWBUF_IS_TYPE_UNDEFINED(querybuf));
        
        packet = GWBUF_DATA(querybuf);
        packet_type = packet[4];
        
        if (rses_is_closed)
        {
                /** 
                 * MYSQL_COM_QUIT may have sent by client and as a part of backend 
                 * closing procedure.
                 */
                if (packet_type != MYSQL_COM_QUIT)
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
                }
                goto return_ret;
        }
        inst->stats.n_queries++;
        startpos = (char *)&packet[5];

        master_dcb = router_cli_ses->rses_master_ref->bref_dcb;
        CHK_DCB(master_dcb);
        
        switch(packet_type) {
                case MYSQL_COM_QUIT:        /*< 1 QUIT will close all sessions */
                case MYSQL_COM_INIT_DB:     /*< 2 DDL must go to the master */
                case MYSQL_COM_REFRESH:     /*< 7 - I guess this is session but not sure */
                case MYSQL_COM_DEBUG:       /*< 0d all servers dump debug info to stdout */
                case MYSQL_COM_PING:        /*< 0e all servers are pinged */
                case MYSQL_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
                case MYSQL_COM_STMT_CLOSE:  /*< free prepared statement */
                case MYSQL_COM_STMT_SEND_LONG_DATA: /*< send data to column */
                case MYSQL_COM_STMT_RESET:  /*< resets the data of a prepared statement */
                        qtype = QUERY_TYPE_SESSION_WRITE;
                        break;
                        
                case MYSQL_COM_CREATE_DB:   /**< 5 DDL must go to the master */
                case MYSQL_COM_DROP_DB:     /**< 6 DDL must go to the master */
                        qtype = QUERY_TYPE_WRITE;
                        break;

                case MYSQL_COM_QUERY:
                        plainsqlbuf = gwbuf_clone_transform(querybuf, 
                                                            GWBUF_TYPE_PLAINSQL);
                        len = GWBUF_LENGTH(plainsqlbuf);
                        /** unnecessary if buffer includes additional terminating null */
                        querystr = (char *)malloc(len+1);
                        memcpy(querystr, startpos, len);
                        memset(&querystr[len], 0, 1);
                        /** 
                         * Use mysql handle to query information from parse tree.
                         * call skygw_query_classifier_free before exit!
                         */ 
                        qtype = skygw_query_classifier_get_type(querystr, 0, &mysql);
                        break;
                        
                case MYSQL_COM_STMT_PREPARE:
                        plainsqlbuf = gwbuf_clone_transform(querybuf, 
                                                            GWBUF_TYPE_PLAINSQL);
                        len = GWBUF_LENGTH(plainsqlbuf);
                        /** unnecessary if buffer includes additional terminating null */
                        querystr = (char *)malloc(len+1);
                        memcpy(querystr, startpos, len);
                        memset(&querystr[len], 0, 1);
                        qtype = skygw_query_classifier_get_type(querystr, 0, &mysql);
                        qtype |= QUERY_TYPE_PREPARE_STMT;
                        break;
                        
                case MYSQL_COM_STMT_EXECUTE:
                        /** Parsing is not needed for this type of packet */
#if defined(NOT_USED)
                        plainsqlbuf = gwbuf_clone_transform(querybuf, 
                                                            GWBUF_TYPE_PLAINSQL);
                        len = GWBUF_LENGTH(plainsqlbuf);
                        /** unnecessary if buffer includes additional terminating null */
                        querystr = (char *)malloc(len+1);
                        memcpy(querystr, startpos, len);
                        memset(&querystr[len], 0, 1);
                        qtype = skygw_query_classifier_get_type(querystr, 0, &mysql);
#endif
                        qtype = QUERY_TYPE_EXEC_STMT;
                        break;
                        
                case MYSQL_COM_SHUTDOWN:       /**< 8 where should shutdown be routed ? */
                case MYSQL_COM_STATISTICS:     /**< 9 ? */
                case MYSQL_COM_PROCESS_INFO:   /**< 0a ? */
                case MYSQL_COM_CONNECT:        /**< 0b ? */
                case MYSQL_COM_PROCESS_KILL:   /**< 0c ? */
                case MYSQL_COM_TIME:           /**< 0f should this be run in gateway ? */
                case MYSQL_COM_DELAYED_INSERT: /**< 10 ? */
                case MYSQL_COM_DAEMON:         /**< 1d ? */
                default:
                        break;
        } /**< switch by packet type */

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
         * Find out where to route the query. Result may not be clear; it is 
         * possible to have a hint for routing to a named server which can
         * be either slave or master. 
         * If query would otherwise be routed to slave then the hint determines 
         * actual target server if it exists.
         * 
         * route_target is a bitfield and may include multiple values.
         */
        route_target = get_route_target(qtype, 
                                        router_cli_ses->rses_transaction_active, 
                                        querybuf->hint);
      
        if (TARGET_IS_ALL(route_target))
        {
                /**
                 * It is not sure if the session command in question requires
                 * response. Statement is examined in route_session_write.
                 */
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
                goto return_ret;
        }
        /**
         * Handle routing to master and to slave
         */
        else
        {
                bool           succp = true;
                HINT*          hint;
                char*          named_server = NULL;
                int            rlag_max = MAX_RLAG_UNDEFINED;
                
                if (router_cli_ses->rses_transaction_active) /*< all to master */
                {
                        route_target = TARGET_MASTER; /*< override old value */
                        
                        LOGIF(LT, (skygw_log_write(
                                LOGFILE_TRACE,
                                "Transaction is active, routing to Master.")));
                }
                LOGIF(LT, (skygw_log_write(LOGFILE_TRACE, "%s", STRQTYPE(qtype))));
                
                /** Lock router session */
                if (!rses_begin_locked_router_action(router_cli_ses))
                {
                        goto return_ret;
                }
                   
                if (TARGET_IS_SLAVE(route_target))
                {
                        if (TARGET_IS_NAMED_SERVER(route_target) ||
                                TARGET_IS_RLAG_MAX(route_target))
                        {
                                hint = querybuf->hint;
                                
                                while (hint != NULL)
                                {
                                        if (hint->type == HINT_ROUTE_TO_NAMED_SERVER)
                                        {
                                                named_server = hint->data;
                                                LOGIF(LT, (skygw_log_write(
                                                        LOGFILE_TRACE,
                                                        "Hint: route to server "
                                                        "'%s'",
                                                        named_server)));
                                                
                                        }
                                        else if (hint->type == HINT_PARAMETER &&
                                                (strncasecmp(
                                                        (char *)hint->data,
                                                        "max_slave_replication_lag",
                                                        strlen("max_slave_replication_lag")) == 0))
                                        {
                                                int val = (int) strtol((char *)hint->value, 
                                                                       (char **)NULL, 10);
                                                
                                                if (val != 0 || errno == 0)
                                                {
                                                        rlag_max = val;
                                                        LOGIF(LT, (skygw_log_write(
                                                                LOGFILE_TRACE,
                                                                "Hint: "
                                                                "max_slave_replication_lag=%d",
                                                                rlag_max)));
                                                }
                                        }
                                        hint = hint->next;
                                }
                        }
                        
                        if (rlag_max == MAX_RLAG_UNDEFINED) /*< no rlag max hint, use config */
                        {
                                rlag_max = rses_get_max_replication_lag(router_cli_ses);
                        }
                        
                        succp = get_dcb(&target_dcb, 
                                        router_cli_ses, 
                                        BE_SLAVE, 
                                        named_server,
                                        rlag_max);
                }
                else if (TARGET_IS_MASTER(route_target))
                {
                        if (master_dcb == NULL)
                        {
                                succp = get_dcb(&master_dcb, 
                                                router_cli_ses, 
                                                BE_MASTER, 
                                                NULL,
                                                MAX_RLAG_UNDEFINED);
                        }
                        target_dcb = master_dcb;
                }
                
                if (succp) /*< Have DCB of the target backend */
                {                        
                        if ((ret = target_dcb->func.write(target_dcb, querybuf)) == 1)
                        {
                                backend_ref_t* bref;
                                
                                atomic_add(&inst->stats.n_slave, 1);
                                /** 
                                 * Add one query response waiter to backend reference
                                 */
                                bref = get_bref_from_dcb(router_cli_ses, target_dcb);
                                bref_set_state(bref, BREF_QUERY_ACTIVE);
                                bref_set_state(bref, BREF_WAITING_RESULT);
                        }
                        else
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Routing query \"%s\" failed.",
                                        querystr)));
                        }
                }
                rses_end_locked_router_action(router_cli_ses);
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
        if (mysql != NULL)
        {
                skygw_query_classifier_free(mysql);
        }
        return ret;
}



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
BACKEND		  *backend;
char		  *weightby;

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
	if ((weightby = serviceGetWeightingParameter(router->service)) != NULL)
        {
                dcb_printf(dcb,
		   "\tConnection distribution based on %s "
                                "server parameter.\n", weightby);
                dcb_printf(dcb,
                        "\t\tServer               Target %%    Connections  "
			"Operations\n");
                dcb_printf(dcb,
                        "\t\t                               Global  Router\n");
                for (i = 0; router->servers[i]; i++)
                {
                        backend = router->servers[i];
                        dcb_printf(dcb,
				"\t\t%-20s %3.1f%%     %-6d  %-6d  %d\n",
                                backend->backend_server->unique_name,
                                (float)backend->weight / 10,
				backend->backend_server->stats.n_current,
				backend->backend_conn_count,
				backend->backend_server->stats.n_current_ops);
                }

        }

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
static void clientReply (
        ROUTER* instance,
        void*   router_session,
        GWBUF*  writebuf,
        DCB*    backend_dcb)
{
        DCB*               client_dcb;
        ROUTER_CLIENT_SES* router_cli_ses;
	sescmd_cursor_t*   scur = NULL;
        backend_ref_t*     bref;
        
	router_cli_ses = (ROUTER_CLIENT_SES *)router_session;
        CHK_CLIENT_RSES(router_cli_ses);

        /**
         * Lock router client session for secure read of router session members.
         * Note that this could be done without lock by using version #
         */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                print_error_packet(router_cli_ses, writebuf, backend_dcb);
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
        bref = get_bref_from_dcb(router_cli_ses, backend_dcb);
        
        CHK_BACKEND_REF(bref);
        scur = &bref->bref_sescmd_cur;
        /**
         * Active cursor means that reply is from session command 
         * execution.
         */
	if (sescmd_cursor_is_active(scur))
	{
                if (LOG_IS_ENABLED(LOGFILE_ERROR) && 
                        MYSQL_IS_ERROR_PACKET(((uint8_t *)GWBUF_DATA(writebuf))))
                {
                        uint8_t* buf = 
                                (uint8_t *)GWBUF_DATA((scur->scmd_cur_cmd->my_sescmd_buf));
                        size_t   len = MYSQL_GET_PACKET_LEN(buf);
                        char*    cmdstr = (char *)malloc(len+1);

                        snprintf(cmdstr, len+1, "%s", &buf[5]);
                        
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Failed to execute %s in %s:%d.",
                                cmdstr, 
                                bref->bref_backend->backend_server->name,
                                bref->bref_backend->backend_server->port)));
                        
                        free(cmdstr);
                }
                
                if (GWBUF_IS_TYPE_SESCMD_RESPONSE(writebuf))
                {
                        /** 
                        * Discard all those responses that have already been sent to
                        * the client. Return with buffer including response that
                        * needs to be sent to client or NULL.
                        */
                        writebuf = sescmd_cursor_process_replies(writebuf, bref);
                }
                /** 
                 * If response will be sent to client, decrease waiter count.
                 * This applies to session commands only. Counter decrement
                 * for other type of queries is done outside this block.
                 */
                if (writebuf != NULL && client_dcb != NULL)
                {
                        /** Set response status as replied */
                        bref_clear_state(bref, BREF_WAITING_RESULT);
                }
	}
	/**
         * Clear BREF_QUERY_ACTIVE flag and decrease waiter counter.
         * This applies for queries  other than session commands.
         */
	else if (BREF_IS_QUERY_ACTIVE(bref))
	{
                bref_clear_state(bref, BREF_QUERY_ACTIVE);
                /** Set response status as replied */
                bref_clear_state(bref, BREF_WAITING_RESULT);
        }

        if (writebuf != NULL && client_dcb != NULL)
        {
                /** Write reply to client DCB */
		SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
        }
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
        /** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                /** Log to debug that router was closed */
                goto lock_failed;
        }
        /** There is one pending session command to be executed. */
        if (sescmd_cursor_is_active(scur)) 
        {
                bool succp;
                
                LOGIF(LT, (skygw_log_write(
                        LOGFILE_TRACE,
                        "Backend %s:%d processed reply and starts to execute "
                        "active cursor.",
                        bref->bref_backend->backend_server->name,
                        bref->bref_backend->backend_server->port)));
                
                succp = execute_sescmd_in_backend(bref);
                
                ss_dassert(succp);
        }
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
lock_failed:
        return;
}

/** Compare nunmber of connections from this router in backend servers */
int bref_cmp_router_conn(
        const void* bref1,
        const void* bref2)
{
        BACKEND* b1 = ((backend_ref_t *)bref1)->bref_backend;
        BACKEND* b2 = ((backend_ref_t *)bref2)->bref_backend;

        return ((1000 * b1->backend_conn_count) / b1->weight)
			  - ((1000 * b2->backend_conn_count) / b2->weight);
}

/** Compare nunmber of global connections in backend servers */
int bref_cmp_global_conn(
        const void* bref1,
        const void* bref2)
{
        BACKEND* b1 = ((backend_ref_t *)bref1)->bref_backend;
        BACKEND* b2 = ((backend_ref_t *)bref2)->bref_backend;
        
        return ((1000 * b1->backend_server->stats.n_current) / b1->weight)
		  - ((1000 * b2->backend_server->stats.n_current) / b2->weight);
}


/** Compare relication lag between backend servers */
int bref_cmp_behind_master(
        const void* bref1, 
        const void* bref2)
{
        BACKEND* b1 = ((backend_ref_t *)bref1)->bref_backend;
        BACKEND* b2 = ((backend_ref_t *)bref2)->bref_backend;
        
        return ((b1->backend_server->rlag < b2->backend_server->rlag) ? -1 :
        ((b1->backend_server->rlag > b2->backend_server->rlag) ? 1 : 0));
}

/** Compare nunmber of current operations in backend servers */
int bref_cmp_current_load(
        const void* bref1,
        const void* bref2)
{
        SERVER*  s1 = ((backend_ref_t *)bref1)->bref_backend->backend_server;
        SERVER*  s2 = ((backend_ref_t *)bref2)->bref_backend->backend_server;
        BACKEND* b1 = ((backend_ref_t *)bref1)->bref_backend;
        BACKEND* b2 = ((backend_ref_t *)bref2)->bref_backend;
        
        return ((1000 * s1->stats.n_current_ops) - b1->weight)
			- ((1000 * s2->stats.n_current_ops) - b2->weight);
}
        
static void bref_clear_state(
        backend_ref_t* bref,
        bref_state_t   state)
{
        if (state != BREF_WAITING_RESULT)
        {
                bref->bref_state &= ~state;
        }
        else
        {
                int prev1;
                int prev2;
                
                /** Decrease waiter count */
                prev1 = atomic_add(&bref->bref_num_result_wait, -1);
                
                if (prev1 <= 0) {
                        atomic_add(&bref->bref_num_result_wait, 1);
                }
                else
                {
                        /** Decrease global operation count */
                        prev2 = atomic_add(
                                &bref->bref_backend->backend_server->stats.n_current_ops, -1);
                        ss_dassert(prev2 > 0);
                }       
        }
}

static void bref_set_state(        
        backend_ref_t* bref,
        bref_state_t   state)
{
        if (state != BREF_WAITING_RESULT)
        {
                bref->bref_state |= state;
        }
        else
        {
                int prev1;
                int prev2;
                
                /** Increase waiter count */
                prev1 = atomic_add(&bref->bref_num_result_wait, 1);
                ss_dassert(prev1 >= 0);
                
                /** Increase global operation count */
                prev2 = atomic_add(
                        &bref->bref_backend->backend_server->stats.n_current_ops, 1);
                ss_dassert(prev2 >= 0);                
        }
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
 * @param max_slave_rlag - in, use
 *      Maximum allowed replication lag for any slave. Configuration parameter or default.
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
 *      There will possibly be more backend references than connected backends
 *      because only those in correct state are connected to.
 */
static bool select_connect_backend_servers(
        backend_ref_t**    p_master_ref,
        backend_ref_t*     backend_ref,
        int                router_nservers,
        int                max_nslaves,
        int                max_slave_rlag,
        select_criteria_t  select_criteria,
        SESSION*           session,
        ROUTER_INSTANCE*   router)
{
        bool            succp = true;
        bool            master_found;
        bool            master_connected;
        int             slaves_found = 0;
        int             slaves_connected = 0;
        int             i;
        const int       min_nslaves = 0; /*< not configurable at the time */
        bool            is_synced_master;
        int (*p)(const void *, const void *);
	BACKEND *master_host = NULL;
        
        if (p_master_ref == NULL || backend_ref == NULL)
        {
                ss_dassert(FALSE);
                succp = false;
                goto return_succp;
        }
      
	/* get the root Master */ 
	master_host = get_root_master(backend_ref, router_nservers); 

        /** Master is already chosen and connected. This is slave failure case */
        if (*p_master_ref != NULL &&
                BREF_IS_IN_USE((*p_master_ref)))
        {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [select_connect_backend_servers] Master %p fd %d found.",
                        pthread_self(),
                        (*p_master_ref)->bref_dcb,
                        (*p_master_ref)->bref_dcb->fd)));
                
                master_found     = true;
                master_connected = true;
		/* assert with master_host */
                ss_dassert(master_host && ((*p_master_ref)->bref_backend->backend_server == master_host->backend_server) && SERVER_MASTER);
        }
        /** New session or master failure case */
        else
        {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [select_connect_backend_servers] Session %p doesn't "
                        "currently have a master chosen. Proceeding to master "
                        "selection.",
                        pthread_self(),
                        session)));
                
                master_found     = false;
                master_connected = false;
        }
        /** Check slave selection criteria and set compare function */
        p = criteria_cmpfun[select_criteria];
        
        if (p == NULL)
        {
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

#if defined(EXTRA_SS_DEBUG)        
        LOGIF(LT, (skygw_log_write(LOGFILE_TRACE, "Servers and conns before ordering:")));
        
        for (i=0; i<router_nservers; i++)
        {
                BACKEND* b = backend_ref[i].bref_backend;

                LOGIF(LT, (skygw_log_write(LOGFILE_TRACE, 
                                           "master bref %p bref %p %d %s %d:%d",
                                           *p_master_ref,
                                           &backend_ref[i],
                                           backend_ref[i].bref_state,
                                           b->backend_server->name,
                                           b->backend_server->port,
                                           b->backend_conn_count)));                
        }
#endif
	/* assert with master_host */
        ss_dassert(!master_connected ||
                (master_host && 
                ((*p_master_ref)->bref_backend->backend_server == master_host->backend_server) && 
                SERVER_MASTER));
        /**
         * Sort the pointer list to servers according to connection counts. As 
         * a consequence those backends having least connections are in the 
         * beginning of the list.
         */
        qsort(backend_ref, (size_t)router_nservers, sizeof(backend_ref_t), p);

        if (LOG_IS_ENABLED(LOGFILE_TRACE))
        {
                if (select_criteria == LEAST_GLOBAL_CONNECTIONS ||
                        select_criteria == LEAST_ROUTER_CONNECTIONS ||
                        select_criteria == LEAST_BEHIND_MASTER ||
                        select_criteria == LEAST_CURRENT_OPERATIONS)
                {
                        LOGIF(LT, (skygw_log_write(LOGFILE_TRACE, 
                                "Servers and %s connection counts:",
                                select_criteria == LEAST_GLOBAL_CONNECTIONS ? 
                                "all MaxScale" : "router")));

                        for (i=0; i<router_nservers; i++)
                        {
                                BACKEND* b = backend_ref[i].bref_backend;
                                
                                switch(select_criteria) {
                                        case LEAST_GLOBAL_CONNECTIONS:
                                                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE, 
                                                        "%s:%d MaxScale connections : %d",
                                                        b->backend_server->name,
                                                        b->backend_server->port,
                                                        b->backend_server->stats.n_current)));
                                                break;
                                        
                                        case LEAST_ROUTER_CONNECTIONS:
                                                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE, 
                                                        "%s:%d RWSplit connections : %d",
                                                        b->backend_server->name,
                                                        b->backend_server->port,
                                                        b->backend_conn_count)));
                                                break;
                                                
                                        case LEAST_CURRENT_OPERATIONS:
                                                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE, 
                                                        "%s:%d current operations : %d",
                                                        b->backend_server->name,
                                                        b->backend_server->port,
                                                        b->backend_server->stats.n_current_ops)));
                                                break;
                                                
                                        case LEAST_BEHIND_MASTER:
                                                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE, 
                                                        "%s:%d replication lag : %d",
                                                        b->backend_server->name,
                                                        b->backend_server->port,
                                                        b->backend_server->rlag)));
                                        default:
                                                break;
                                }
                        } 
                }
        } /*< log only */
        
        /**
         * Choose at least 1+min_nslaves (master and slave) and at most 1+max_nslaves 
         * servers from the sorted list. First master found is selected.
         */
        for (i=0; 
             i<router_nservers && (slaves_connected < max_nslaves || !master_connected);
             i++)
        {
                BACKEND* b = backend_ref[i].bref_backend;

                if (SERVER_IS_RUNNING(b->backend_server) &&
                        ((b->backend_server->status & router->bitmask) ==
                        router->bitvalue))
                {
			/* check also for relay servers and don't take the master_host */
                        if (slaves_found < max_nslaves &&
                                (max_slave_rlag == MAX_RLAG_UNDEFINED || 
                                (b->backend_server->rlag != MAX_RLAG_NOT_AVAILABLE &&
                                 b->backend_server->rlag <= max_slave_rlag)) &&
                                (SERVER_IS_SLAVE(b->backend_server) || SERVER_IS_RELAY_SERVER(b->backend_server)) &&
				(master_host != NULL && (b->backend_server != master_host->backend_server)))
                        {
                                slaves_found += 1;
                                
                                /** Slave is already connected */
                                if (BREF_IS_IN_USE((&backend_ref[i])))
                                {
                                        slaves_connected += 1;
                                }
                                /** New slave connection is taking place */
                                else
                                {
                                        backend_ref[i].bref_dcb = dcb_connect(
                                                b->backend_server,
                                                session,
                                                b->backend_server->protocol);
                                        
                                        if (backend_ref[i].bref_dcb != NULL)
                                        {
                                                slaves_connected += 1;
                                                /**
                                                 * Start executing session command
                                                 * history.
                                                 */
                                                execute_sescmd_history(&backend_ref[i]);
                                                /** 
                                                 * When server fails, this callback
                                                 * is called.
                                                 */
                                                dcb_add_callback(
                                                        backend_ref[i].bref_dcb,
                                                        DCB_REASON_NOT_RESPONDING,
                                                        &router_handle_state_switch,
                                                        (void *)&backend_ref[i]);
                                                backend_ref[i].bref_state = 0;
                                                bref_set_state(&backend_ref[i], 
                                                               BREF_IN_USE);
                                               /** 
                                                * Increase backend connection counter.
                                                * Server's stats are _increased_ in 
                                                * dcb.c:dcb_alloc !
                                                * But decreased in the calling function 
                                                * of dcb_close.
                                                */
                                                atomic_add(&b->backend_conn_count, 1);
                                        }
                                        else
                                        {
                                                LOGIF(LE, (skygw_log_write_flush(
                                                        LOGFILE_ERROR,
                                                        "Error : Unable to establish "
                                                        "connection with slave %s:%d",
                                                        b->backend_server->name,
                                                        b->backend_server->port)));
                                                /* handle connect error */
                                        }
                                }
                        }
			/* take the master_host for master */
			else if (master_host && 
                                (b->backend_server == master_host->backend_server))
                        {
                                *p_master_ref = &backend_ref[i];
                                
                                if (master_connected)
                                {   
                                        continue;
                                }
                                master_found = true;
                                  
                                backend_ref[i].bref_dcb = dcb_connect(
                                        b->backend_server,
                                        session,
                                        b->backend_server->protocol);
                                
                                if (backend_ref[i].bref_dcb != NULL)
                                {
                                        master_connected = true;
                                        /** 
                                         * When server fails, this callback
                                         * is called.
                                         */
                                        dcb_add_callback(
                                                backend_ref[i].bref_dcb,
                                                DCB_REASON_NOT_RESPONDING,
                                                &router_handle_state_switch,
                                                (void *)&backend_ref[i]);

                                        backend_ref[i].bref_state = 0;
                                        bref_set_state(&backend_ref[i], 
                                                       BREF_IN_USE);
                                        /** Increase backend connection counters */
                                        atomic_add(&b->backend_conn_count, 1);
                                }
                                else
                                {
                                        succp = false;
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : Unable to establish "
                                                "connection with master %s:%d",
                                                b->backend_server->name,
                                                b->backend_server->port)));
                                        /** handle connect error */
                                }
                        }       
                }
        } /*< for */
        
#if defined(EXTRA_SS_DEBUG)        
        LOGIF(LT, (skygw_log_write(LOGFILE_TRACE, "Servers and conns after ordering:")));
        
        for (i=0; i<router_nservers; i++)
        {
                BACKEND* b = backend_ref[i].bref_backend;
                
                LOGIF(LT, (skygw_log_write_flush(LOGFILE_TRACE,
                                                "master bref %p bref %p %d %s %d:%d",
                                                *p_master_ref,
                                                &backend_ref[i],
                                                backend_ref[i].bref_state,
                                                b->backend_server->name,
                                                b->backend_server->port,
                                                b->backend_conn_count)));                
        }
	/* assert with master_host */
        ss_dassert(!master_connected ||
        (master_host && ((*p_master_ref)->bref_backend->backend_server == master_host->backend_server) && 
        SERVER_MASTER));
#endif
        
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

                                if (BREF_IS_IN_USE((&backend_ref[i])))
                                {                                        
                                        LOGIF(LT, (skygw_log_write(
                                                LOGFILE_TRACE,
                                                "Selected %s in \t%s:%d",
                                                STRSRVSTATUS(b->backend_server),
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
                succp = false;
                
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
                                "Error : Couldn't find suitable %s from %d "
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
                                "Error : Couldn't connect to any %s although "
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
                                "Error : Couldn't establish required amount of "
                                "slave connections for router session.")));
                }
                
                /** Clean up connections */
                for (i=0; i<router_nservers; i++)
                {
                        if (BREF_IS_IN_USE((&backend_ref[i])))
                        {
                                ss_dassert(backend_ref[i].bref_backend->backend_conn_count > 0);
                                
                                /** disconnect opened connections */
                                dcb_close(backend_ref[i].bref_dcb);
                                bref_clear_state(&backend_ref[i], BREF_IN_USE);
                                /** Decrease backend's connection counter. */
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
		LOGIF(LD, (skygw_log_write(
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
        GWBUF*           replybuf,
        backend_ref_t*   bref)
{
        mysql_sescmd_t*  scmd;
        sescmd_cursor_t* scur;
        
        scur = &bref->bref_sescmd_cur;        
        ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
        scmd = sescmd_cursor_get_command(scur);
               
        CHK_GWBUF(replybuf);
        
        /** 
         * Walk through packets in the message and the list of session 
         * commands. 
         */
        while (scmd != NULL && replybuf != NULL)
        {
                /** Faster backend has already responded to client : discard */
                if (scmd->my_sescmd_is_replied)
                {
                        bool last_packet = false;
                        
                        CHK_GWBUF(replybuf);
                        
                        while (!last_packet)
                        {
                                int  buflen;
                                
                                buflen = GWBUF_LENGTH(replybuf);
                                last_packet = GWBUF_IS_TYPE_RESPONSE_END(replybuf);
                                /** discard packet */
                                replybuf = gwbuf_consume(replybuf, buflen);
                        }
                        /** Set response status received */
                        bref_clear_state(bref, BREF_WAITING_RESULT);
                }
                /** Response is in the buffer and it will be sent to client. */
                else if (replybuf != NULL)
                {
                        /** Mark the rest session commands as replied */
                        scmd->my_sescmd_is_replied = true;
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

static bool sescmd_cursor_history_empty(
        sescmd_cursor_t* scur)
{
        bool succp;
        
        CHK_SESCMD_CUR(scur);
        
        if (scur->scmd_cur_rses->rses_properties[RSES_PROP_TYPE_SESCMD] == NULL)
        {
                succp = true;
        }
        else
        {
                succp = false;
        }
        
        return succp;
}


static void sescmd_cursor_reset(
        sescmd_cursor_t* scur)
{
        ROUTER_CLIENT_SES* rses;
        CHK_SESCMD_CUR(scur);
        CHK_CLIENT_RSES(scur->scmd_cur_rses);
        rses = scur->scmd_cur_rses;

        scur->scmd_cur_ptr_property = &rses->rses_properties[RSES_PROP_TYPE_SESCMD];
        
        CHK_RSES_PROP((*scur->scmd_cur_ptr_property));
        scur->scmd_cur_active = false;
        scur->scmd_cur_cmd = &(*scur->scmd_cur_ptr_property)->rses_prop_data.sescmd;
}

static bool execute_sescmd_history(
        backend_ref_t* bref)
{
        bool             succp;
        sescmd_cursor_t* scur;
        CHK_BACKEND_REF(bref);
        
        scur = &bref->bref_sescmd_cur;
        CHK_SESCMD_CUR(scur);
 
        if (!sescmd_cursor_history_empty(scur))
        {
                sescmd_cursor_reset(scur);
                succp = execute_sescmd_in_backend(bref);
        }
        else
        {
                succp = true;
        }

        return succp;
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
	bool             succp;
	int              rc = 0;
	sescmd_cursor_t* scur;

        if (BREF_IS_CLOSED(backend_ref))
        {
                succp = false;
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
                LOGIF(LT, (skygw_log_write_flush(
                        LOGFILE_TRACE,
                        "Cursor had no pending session commands.")));
                
                goto return_succp;
	}

	if (!sescmd_cursor_is_active(scur))
        {
                /** Cursor is left active when function returns. */
                sescmd_cursor_set_active(scur, true);
        }
#if defined(SS_DEBUG)
        LOGIF(LT, tracelog_routed_query(scur->scmd_cur_rses, 
                                        "execute_sescmd_in_backend", 
                                        backend_ref, 
                                        sescmd_cursor_clone_querybuf(scur)));

        {
                GWBUF* tmpbuf = sescmd_cursor_clone_querybuf(scur);
                uint8_t* ptr = GWBUF_DATA(tmpbuf);
                unsigned char cmd = MYSQL_GET_COMMAND(ptr);
                
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [execute_sescmd_in_backend] Just before write, fd "
                        "%d : cmd %s.",
                        pthread_self(),
                        dcb->fd,
                        STRPACKETTYPE(cmd))));
                gwbuf_free(tmpbuf);
        }
#endif /*< SS_DEBUG */
        switch (scur->scmd_cur_cmd->my_sescmd_packet_type) {
                case MYSQL_COM_CHANGE_USER:
                        rc = dcb->func.auth(
                                dcb, 
                                NULL, 
                                dcb->session, 
                                sescmd_cursor_clone_querybuf(scur));
                        break;

                case MYSQL_COM_QUERY:
                case MYSQL_COM_INIT_DB:
                default:
                        /** 
                         * Mark session command buffer, it triggers writing 
                         * MySQL command to protocol
                         */
                        gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
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

        if (rc == 1)
        {
                succp = true;
        }
        else
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

        if (GWBUF_IS_TYPE_MYSQL(buf))
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
                else if (packet_type == '\x22' || 
                        packet_type == 0x22 || 
                        packet_type == '\x26' || 
                        packet_type == 0x26 ||
                        true)
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
         * These are one-way messages and server doesn't respond to them.
         * Therefore reply processing is unnecessary and session 
         * command property is not needed. It is just routed to all available
         * backends.
         */
        if (packet_type == MYSQL_COM_STMT_SEND_LONG_DATA ||
                packet_type == MYSQL_COM_QUIT ||
                packet_type == MYSQL_COM_STMT_CLOSE)
        {
                int rc;
               
                succp = true;
                
                /** Lock router session */
                if (!rses_begin_locked_router_action(router_cli_ses))
                {
                        succp = false;
                        goto return_succp;
                }
                                
                for (i=0; i<router_cli_ses->rses_nbackends; i++)
                {
                        DCB* dcb = backend_ref[i].bref_dcb;                        

                        if (BREF_IS_IN_USE((&backend_ref[i])))
                        {
                                rc = dcb->func.write(dcb, gwbuf_clone(querybuf));
                        
                                if (rc != 1)
                                {
                                        succp = false;
                                }
                        }
                }
                rses_end_locked_router_action(router_cli_ses);
                gwbuf_free(querybuf);
                goto return_succp;
        }
        /** Lock router session */
        if (!rses_begin_locked_router_action(router_cli_ses))
        {
                succp = false;
                goto return_succp;
        }        
        /** 
         * Additional reference is created to querybuf to 
         * prevent it from being released before properties
         * are cleaned up as a part of router sessionclean-up.
         */
        prop = rses_property_init(RSES_PROP_TYPE_SESCMD);
        mysql_sescmd_init(prop, querybuf, packet_type, router_cli_ses);
        
        /** Add sescmd property to router client session */
        rses_property_add(router_cli_ses, prop);
         
        for (i=0; i<router_cli_ses->rses_nbackends; i++)
        {
                if (BREF_IS_IN_USE((&backend_ref[i])))
                {
                        sescmd_cursor_t* scur;
                        
                        scur = backend_ref_get_sescmd_cursor(&backend_ref[i]);
                        
                        /** 
                         * Add one waiter to backend reference.
                         */
                        bref_set_state(get_bref_from_dcb(router_cli_ses, 
                                                         backend_ref[i].bref_dcb), 
                                       BREF_WAITING_RESULT);
                        /** 
                         * Start execution if cursor is not already executing.
                         * Otherwise, cursor will execute pending commands
                         * when it completes with previous commands.
                         */
                        if (sescmd_cursor_is_active(scur))
                        {
                                succp = true;
                                
                                LOGIF(LT, (skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Backend %s:%d already executing sescmd.",
                                        backend_ref[i].bref_backend->backend_server->name,
                                        backend_ref[i].bref_backend->backend_server->port)));
                        }
                        else
                        {
                                succp = execute_sescmd_in_backend(&backend_ref[i]);
                                
                                if (!succp)
                                {
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : Failed to execute session "
                                                "command in %s:%d",
                                                backend_ref[i].bref_backend->backend_server->name,
                                                backend_ref[i].bref_backend->backend_server->port)));
                                }
                        }
                }
        }
        /** Unlock router session */
        rses_end_locked_router_action(router_cli_ses);
        
        atomic_add(&inst->stats.n_all, 1);
        
return_succp:
        return succp;
}

#if defined(NOT_USED)

static bool router_option_configured(
        ROUTER_INSTANCE* router,
        const char*      optionstr,
        void*            data)
{
        bool   succp = false;
        char** option;
        
        option = router->service->routerOptions;
        
        while (option != NULL)
        {
                char*  value;

                if ((value = strchr(options[i], '=')) == NULL)
                {
                        break;
                }
                else
                {
                        *value = 0;
                        value++;
                        if (strcmp(options[i], "slave_selection_criteria") == 0)
                        {
                                if (GET_SELECT_CRITERIA(value) == (select_criteria_t *)*data)
                                {
                                        succp = true;
                                        break;
                                }
                        }
                }
        }
        return succp;
}
#endif /*< NOT_USED */

static void rwsplit_process_router_options(
        ROUTER_INSTANCE* router,
        char**           options)
{
        int               i;
        char*             value;
        select_criteria_t c;
        
        for (i = 0; options[i]; i++)
        {
                if ((value = strchr(options[i], '=')) == NULL)
                {
                        LOGIF(LE, (skygw_log_write(
                                LOGFILE_ERROR, "Warning : Unsupported "
                                "router option \"%s\" for "
                                "readwritesplit router.",
                                options[i])));
                }
                else
                {
                        *value = 0;
                        value++;
                        if (strcmp(options[i], "slave_selection_criteria") == 0)
                        {
                                c = GET_SELECT_CRITERIA(value);
                                ss_dassert(
                                        c == LEAST_GLOBAL_CONNECTIONS ||
                                        c == LEAST_ROUTER_CONNECTIONS ||
                                        c == LEAST_BEHIND_MASTER ||
                                        c == LEAST_CURRENT_OPERATIONS ||
                                        c == UNDEFINED_CRITERIA);
                               
                                if (c == UNDEFINED_CRITERIA)
                                {
                                        LOGIF(LE, (skygw_log_write(
                                                LOGFILE_ERROR, "Warning : Unknown "
                                                "slave selection criteria \"%s\". "
                                                "Allowed values are LEAST_GLOBAL_CONNECTIONS, "
                                                "LEAST_ROUTER_CONNECTIONS, "
                                                "LEAST_BEHIND_MASTER,"
                                                "and LEAST_CURRENT_OPERATIONS.",
                                                STRCRITERIA(router->rwsplit_config.rw_slave_select_criteria))));
                                }
                                else
                                {
                                        router->rwsplit_config.rw_slave_select_criteria = c;
                                }
                        }
                }
        } /*< for */
}

/**
 * Error Handler routine to resolve backend failures. If it succeeds then there
 * are enough operative backends available and connected. Otherwise it fails, 
 * and session is terminated.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       message         The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action          The action: REPLY, REPLY_AND_CLOSE, NEW_CONNECTION
 * @param       succp           Result of action. 
 * 
 * Even if succp == true connecting to new slave may have failed. succp is to
 * tell whether router has enough master/slave connections to continue work.
 */

static void handleError (
        ROUTER*        instance,
        void*          router_session,
        GWBUF*         errmsgbuf,
        DCB*           backend_dcb,
        error_action_t action,
        bool*          succp)
{
        SESSION*           session;
        ROUTER_INSTANCE*   inst    = (ROUTER_INSTANCE *)instance;
        ROUTER_CLIENT_SES* rses    = (ROUTER_CLIENT_SES *)router_session;
      
        CHK_DCB(backend_dcb);
#if defined(SS_DEBUG)
        backend_dcb->dcb_errhandle_called = true;
#endif
        session = backend_dcb->session;
        
        if (session != NULL)
                CHK_SESSION(session);
        
        switch (action) {
                case ERRACT_NEW_CONNECTION:
                {
                        if (rses != NULL)
                                CHK_CLIENT_RSES(rses);
                        
                        if (!rses_begin_locked_router_action(rses))
                        {
                                *succp = false;
                                return;
                        }
                        
                        *succp = handle_error_new_connection(inst, 
                                                             rses, 
                                                             backend_dcb, 
                                                             errmsgbuf);
                        rses_end_locked_router_action(rses);
                        break;
                }
                
                case ERRACT_REPLY_CLIENT:
                {
                        *succp = handle_error_reply_client(session, errmsgbuf);
                        break;       
                }
                
                default:
                        *succp = false;
                        break;
        }
}


static bool handle_error_reply_client(
        SESSION* ses,
        GWBUF*   errmsg)
{
        session_state_t sesstate;
        DCB*            client_dcb;
        bool            succp;

        spinlock_acquire(&ses->ses_lock);
        sesstate = ses->state;
        client_dcb = ses->client;
        spinlock_release(&ses->ses_lock);

        if (sesstate == SESSION_STATE_ROUTER_READY)
        {
                CHK_DCB(client_dcb);
                client_dcb->func.write(client_dcb, errmsg);
        }
        else
        {
                while ((errmsg=gwbuf_consume(errmsg, GWBUF_LENGTH(errmsg))) != NULL)
                        ;
        }                
        succp = false; /** false because new servers aren's selected. */

        return succp;
}

/**
 * This must be called with router lock
 */
static bool handle_error_new_connection(
        ROUTER_INSTANCE*   inst,
        ROUTER_CLIENT_SES* rses,
        DCB*               backend_dcb,
        GWBUF*             errmsg)
{
        SESSION*       ses;
        int            router_nservers;
        int            max_nslaves;
        int            max_slave_rlag;
        backend_ref_t* bref;
        bool           succp;
        
        ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));
        
        ses = backend_dcb->session;
        CHK_SESSION(ses);
        
        bref = get_bref_from_dcb(rses, backend_dcb);
        
        /** failed DCB has already been replaced */
        if (bref == NULL)
        {
                succp = true;
                goto return_succp;
        }
        /** 
         * Error handler is already called for this DCB because
         * it's not polling anymore. It can be assumed that
         * it succeed because rses isn't closed.
         */
        if (backend_dcb->state != DCB_STATE_POLLING)
        {
                succp = true;
                goto return_succp;
        }
        
        CHK_BACKEND_REF(bref);
        
        if (BREF_IS_WAITING_RESULT(bref))
        {
                DCB* client_dcb;
                client_dcb = ses->client;
                client_dcb->func.write(client_dcb, errmsg);
                bref_clear_state(bref, BREF_WAITING_RESULT);
        }
        else 
        {
                while ((errmsg=gwbuf_consume(errmsg, GWBUF_LENGTH(errmsg))) != NULL)
                        ;
        }
        bref_clear_state(bref, BREF_IN_USE);
        bref_set_state(bref, BREF_CLOSED);
        /** 
         * Remove callback because this DCB won't be used 
         * unless it is reconnected later, and then the callback
         * is set again.
         */
        dcb_remove_callback(backend_dcb, 
                            DCB_REASON_NOT_RESPONDING, 
                            &router_handle_state_switch, 
                            (void *)bref);
        
        router_nservers = router_get_servercount(inst);
        max_nslaves     = rses_get_max_slavecount(rses, router_nservers);
        max_slave_rlag  = rses_get_max_replication_lag(rses);
        /** 
         * Try to get replacement slave or at least the minimum 
         * number of slave connections for router session.
         */
        succp = select_connect_backend_servers(
                        &rses->rses_master_ref,
                        rses->rses_backend_ref,
                        router_nservers,
                        max_nslaves,
                        max_slave_rlag,
                        rses->rses_config.rw_slave_select_criteria,
                        ses,
                        inst);

return_succp:
        return succp;        
}


static void print_error_packet(
        ROUTER_CLIENT_SES* rses, 
        GWBUF*             buf, 
        DCB*               dcb)
{
#if defined(SS_DEBUG)
        if (GWBUF_IS_TYPE_MYSQL(buf))
        {
                while (gwbuf_length(buf) > 0)
                {
                        /** 
                         * This works with MySQL protocol only ! 
                         * Protocol specific packet print functions would be nice.
                         */
                        uint8_t* ptr = GWBUF_DATA(buf);
                        size_t   len = MYSQL_GET_PACKET_LEN(ptr);
                        
                        if (MYSQL_GET_COMMAND(ptr) == 0xff)
                        {
                                SERVER*        srv = NULL;
                                backend_ref_t* bref = rses->rses_backend_ref;
                                int            i;
                                char*          bufstr;
                                
                                for (i=0; i<rses->rses_nbackends; i++)
                                {
                                        if (bref[i].bref_dcb == dcb)
                                        {
                                                srv = bref[i].bref_backend->backend_server;
                                        }
                                }
                                ss_dassert(srv != NULL);
                                
                                bufstr = strndup(&ptr[7], len-3);
                                
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Backend server %s:%d responded with "
                                        "error : %s",
                                        srv->name,
                                        srv->port,
                                        bufstr)));                
                                free(bufstr);
                        }
                        buf = gwbuf_consume(buf, len+4);
                }
        }
        else
        {
                while ((buf = gwbuf_consume(buf, GWBUF_LENGTH(buf))) != NULL);
        }
#endif /*< SS_DEBUG */
}

static int router_get_servercount(
        ROUTER_INSTANCE* inst)
{
        int       router_nservers = 0;
        BACKEND** b = inst->servers;
        /** count servers */
        while (*(b++) != NULL) router_nservers++;
                                                                
        return router_nservers;
}

static bool have_enough_servers(
        ROUTER_CLIENT_SES** p_rses,
        const int           min_nsrv,
        int                 router_nsrv,
        ROUTER_INSTANCE*    router)
{
        bool succp;
        
        /** With too few servers session is not created */
        if (router_nsrv < min_nsrv || 
                MAX((*p_rses)->rses_config.rw_max_slave_conn_count, 
                    (router_nsrv*(*p_rses)->rses_config.rw_max_slave_conn_percent)/100)
                        < min_nsrv)
        {
                if (router_nsrv < min_nsrv)
                {
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Unable to start %s service. There are "
                                "too few backend servers available. Found %d "
                                "when %d is required.",
                                router->service->name,
                                router_nsrv,
                                min_nsrv)));
                }
                else
                {
                        double pct = (*p_rses)->rses_config.rw_max_slave_conn_percent/100;
                        double nservers = (double)router_nsrv*pct;
                        
                        if ((*p_rses)->rses_config.rw_max_slave_conn_count < min_nsrv)
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Unable to start %s service. There are "
                                        "too few backend servers configured in "
                                        "MaxScale.cnf. Found %d when %d is required.",
                                        router->service->name,
                                        (*p_rses)->rses_config.rw_max_slave_conn_count,
                                        min_nsrv)));
                        }
                        if (nservers < min_nsrv)
                        {
                                LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Unable to start %s service. There are "
                                        "too few backend servers configured in "
                                        "MaxScale.cnf. Found %d%% when at least %.0f%% "
                                        "would be required.",
                                        router->service->name,
                                        (*p_rses)->rses_config.rw_max_slave_conn_percent,
                                        min_nsrv/(((double)router_nsrv)/100))));
                        }
                }
                free(*p_rses);
                *p_rses = NULL;
                succp = false;
        }
        else
        {
                succp = true;
        }
        return succp;
}

/** 
 * Find out the number of read backend servers.
 * Depending on the configuration value type, either copy direct count 
 * of slave connections or calculate the count from percentage value.
 */
static int rses_get_max_slavecount(
        ROUTER_CLIENT_SES* rses,
        int                router_nservers)
{
        int conf_max_nslaves;
        int max_nslaves;
        
        CHK_CLIENT_RSES(rses);
        
        if (rses->rses_config.rw_max_slave_conn_count > 0)
        {
                conf_max_nslaves = rses->rses_config.rw_max_slave_conn_count;
        }
        else
        {
                conf_max_nslaves = 
                (router_nservers*rses->rses_config.rw_max_slave_conn_percent)/100;
        }
        max_nslaves = MIN(router_nservers-1, MAX(1, conf_max_nslaves));
        
        return max_nslaves;
}


static int rses_get_max_replication_lag(
        ROUTER_CLIENT_SES* rses)
{
        int conf_max_rlag;
        
        CHK_CLIENT_RSES(rses);
        
        /** if there is no configured value, then longest possible int is used */
        if (rses->rses_config.rw_max_slave_replication_lag > 0)
        {
                conf_max_rlag = rses->rses_config.rw_max_slave_replication_lag;
        }
        else
        {
                conf_max_rlag = ~(1<<31);
        }
        
        return conf_max_rlag;
}


static backend_ref_t* get_bref_from_dcb(
        ROUTER_CLIENT_SES* rses,
        DCB*               dcb)
{
        backend_ref_t* bref;
        int            i = 0;
        CHK_DCB(dcb);
        CHK_CLIENT_RSES(rses);
        
        bref = rses->rses_backend_ref;
        
        while (i<rses->rses_nbackends)
        {
                if (bref->bref_dcb == dcb)
                {
                        break;
                }
                bref++;
                i += 1;
        }
        
        if (i == rses->rses_nbackends)
        {
                bref = NULL;
        }
        return bref;
}

static int router_handle_state_switch(
        DCB*       dcb,
        DCB_REASON reason,
        void*      data)
{
        backend_ref_t*     bref;
        int                rc = 1;
        ROUTER_CLIENT_SES* rses;
        SESSION*           ses;
        SERVER*            srv;
        
        CHK_DCB(dcb);
        bref = (backend_ref_t *)data;
        CHK_BACKEND_REF(bref);
       
        srv = bref->bref_backend->backend_server;
        
        if (SERVER_IS_RUNNING(srv) && SERVER_IS_IN_CLUSTER(srv))
        {
                goto return_rc;
        }
        ses = dcb->session;
        CHK_SESSION(ses);

        rses = (ROUTER_CLIENT_SES *)dcb->session->router_session;
        CHK_CLIENT_RSES(rses);

        switch (reason) {
                case DCB_REASON_NOT_RESPONDING:
                        dcb->func.hangup(dcb);
                        break;
                        
                default:
                        break;
        }
        
return_rc:
        return rc;
}

static sescmd_cursor_t* backend_ref_get_sescmd_cursor (
        backend_ref_t* bref)
{
        sescmd_cursor_t* scur;
        CHK_BACKEND_REF(bref);
        
        scur = &bref->bref_sescmd_cur;
        CHK_SESCMD_CUR(scur);
        
        return scur;
}

#if defined(PREP_STMT_CACHING)
#define MAX_STMT_LEN 1024

static prep_stmt_t* prep_stmt_init(
        prep_stmt_type_t type,
        void*            id)
{
        prep_stmt_t* pstmt;
        
        pstmt = (prep_stmt_t *)calloc(1, sizeof(prep_stmt_t));
        
        if (pstmt != NULL)
        {
#if defined(SS_DEBUG)
                pstmt->pstmt_chk_top  = CHK_NUM_PREP_STMT;
                pstmt->pstmt_chk_tail = CHK_NUM_PREP_STMT;
#endif
                pstmt->pstmt_state = PREP_STMT_ALLOC;
                pstmt->pstmt_type  = type;
                
                if (type == PREP_STMT_NAME)
                {
                        pstmt->pstmt_id.name = strndup((char *)id, MAX_STMT_LEN);
                }
                else
                {
                        pstmt->pstmt_id.seq = 0;
                }
        }
        CHK_PREP_STMT(pstmt);
        return pstmt;
}

static void prep_stmt_done(
        prep_stmt_t* pstmt)
{
        CHK_PREP_STMT(pstmt);
        
        if (pstmt->pstmt_type == PREP_STMT_NAME)
        {
                free(pstmt->pstmt_id.name);
        }
        free(pstmt);
}

static bool prep_stmt_drop(
        prep_stmt_t* pstmt)
{
        CHK_PREP_STMT(pstmt);
        
        pstmt->pstmt_state = PREP_STMT_DROPPED;
        return true;
}
#endif /*< PREP_STMT_CACHING */

/********************************
 * This routine returns the root master server from MySQL replication tree
 * Get the root Master rule:
 *
 * find server with the lowest replication depth level
 * and the SERVER_MASTER bitval
 * Servers are checked even if they are in 'maintenance'
 *
 * @param	servers		The list of servers
 * @param	router_nservers	The number of servers
 * @return			The Master found
 *
 */
static BACKEND *get_root_master(backend_ref_t *servers, int router_nservers) {
        int i = 0;
        BACKEND * master_host = NULL;

        for (i = 0; i< router_nservers; i++) {
                BACKEND* b = NULL;
                b = servers[i].bref_backend;
                if (b && (b->backend_server->status & (SERVER_MASTER|SERVER_MAINT)) == SERVER_MASTER) {
                        if (master_host && b->backend_server->depth < master_host->backend_server->depth) {
                                master_host = b;
                        } else {
                                if (master_host == NULL) {
                                        master_host = b;
                                }
                        }
                }
        }
	return master_host;
}

