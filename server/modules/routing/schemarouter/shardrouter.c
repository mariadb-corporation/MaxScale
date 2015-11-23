/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2015
 */

#include <my_config.h>
#include <stdio.h>
#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <router.h>
#include <shardrouter.h>
#include <sharding_common.h>
#include <secrets.h>
#include <mysql.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <dcb.h>
#include <spinlock.h>
#include <modinfo.h>
#include <modutil.h>
#include <mysql_client_server_protocol.h>


MODULE_INFO info = {
    MODULE_API_ROUTER,
    MODULE_BETA_RELEASE,
    ROUTER_VERSION,
    "A database sharding router for simple sharding"
};


/**
 * @file shardrouter.c	
 *
 * This is the sharding router that uses MaxScale's services to abstract
 * the actual implementation of the backend database. Queries are routed based on
 * the location of the database they are using. If a database exists in more than one place
 * the query is routed to the first available service.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 20/01/2015	Markus Mäkelä/Vilho Raatikka		Initial implementation
 * 09/09/2015   Martin Brampton         Modify error handler
 *
 * @endverbatim
 */

static char *version_str = "V1.0.0";
static int filterReply (FILTER* instance, void *session, GWBUF *reply);
static void dummyDiagnostic(FILTER *instance, void *session, DCB *dcb)
{
    return;
}
static void dummySetUpstream(FILTER *instance, void *fsession, UPSTREAM *downstream)
{
    return;
}
static FILTER_OBJECT dummyObject = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    dummySetUpstream,
    NULL,
    filterReply,
    dummyDiagnostic
};

static ROUTER* createInstance(SERVICE *service, char **options);
static void* newSession(ROUTER *instance, SESSION *session);
static void closeSession(ROUTER *instance, void *session);
static void freeSession(ROUTER *instance, void *session);
static int routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static void diagnostic(ROUTER *instance, DCB *dcb);

static void clientReply(
                        ROUTER* instance,
                        void* router_session,
                        GWBUF* queue,
                        DCB* backend_dcb);

static void handleError(
                        ROUTER* instance,
                        void* router_session,
                        GWBUF* errmsgbuf,
                        DCB* backend_dcb,
                        error_action_t action,
                        bool* succp);

static void print_error_packet(ROUTER_CLIENT_SES* rses, GWBUF* buf, DCB* dcb);
static int router_get_servercount(ROUTER_INSTANCE* router);


static route_target_t get_shard_route_target(
                                             skygw_query_type_t qtype,
                                             bool trx_active,
                                             HINT* hint);

static int getCapabilities();

void subsvc_clear_state(SUBSERVICE* svc,subsvc_state_t state);
void subsvc_set_state(SUBSERVICE* svc,subsvc_state_t state);
bool get_shard_subsvc(SUBSERVICE** subsvc,ROUTER_CLIENT_SES* session,char* target);

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

static mysql_sescmd_t* mysql_sescmd_init(
                                         rses_property_t* rses_prop,
                                         GWBUF* sescmd_buf,
                                         unsigned char packet_type,
                                         ROUTER_CLIENT_SES* rses);

static rses_property_t* mysql_sescmd_get_property(
                                                  mysql_sescmd_t* scmd);

static rses_property_t* rses_property_init(
                                           rses_property_type_t prop_type);

static void rses_property_add(
                              ROUTER_CLIENT_SES* rses,
                              rses_property_t* prop);

static void rses_property_done(
                               rses_property_t* prop);

static mysql_sescmd_t* rses_property_get_sescmd(
                                                rses_property_t* prop);

static bool execute_sescmd_history(SUBSERVICE* bref);


static void sescmd_cursor_reset(sescmd_cursor_t* scur);

static bool sescmd_cursor_history_empty(sescmd_cursor_t* scur);

static void sescmd_cursor_set_active(
                                     sescmd_cursor_t* sescmd_cursor,
                                     bool value);

static bool sescmd_cursor_is_active(
                                    sescmd_cursor_t* sescmd_cursor);

static GWBUF* sescmd_cursor_clone_querybuf(
                                           sescmd_cursor_t* scur);

static mysql_sescmd_t* sescmd_cursor_get_command(
                                                 sescmd_cursor_t* scur);

static SUBSERVICE* get_subsvc_from_ses(ROUTER_CLIENT_SES* rses, SESSION* ses);

static bool sescmd_cursor_next( sescmd_cursor_t* scur);

static GWBUF* sescmd_cursor_process_replies(GWBUF* replybuf, SUBSERVICE* bref);

static bool execute_sescmd_in_backend(SUBSERVICE* subsvc);

static bool route_session_write(
                                ROUTER_CLIENT_SES* router_client_ses,
                                GWBUF* querybuf,
                                ROUTER_INSTANCE* inst,
                                unsigned char packet_type,
                                skygw_query_type_t qtype);

static void refreshInstance(
                            ROUTER_INSTANCE* router,
                            CONFIG_PARAMETER* param);

static int router_handle_state_switch(DCB* dcb, DCB_REASON reason, void* data);

static SPINLOCK instlock;
static ROUTER_INSTANCE* instances;

static int hashkeyfun(void* key);
static int hashcmpfun(void *, void *);

static int
hashkeyfun(void* key)
{
    if(key == NULL)
    {
        return 0;
    }
    int hash = 0, c = 0;
    char* ptr = (char*) key;
    while((c = *ptr++))
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

static int
hashcmpfun(
           void* v1,
           void* v2)
{
    char* i1 = (char*) v1;
    char* i2 = (char*) v2;

    return strcmp(i1, i2);
}



/**
 * Convert a length encoded string into a C string.
 * @param data Pointer to the first byte of the string
 * @return Pointer to the newly allocated string or NULL if the value is NULL or an error occurred
 */
char* get_lenenc_str(void* data)
{
    unsigned char* ptr = (unsigned char*)data;
    char* rval;
    uintptr_t size;
    long offset;

    if(data == NULL)
    {
        return NULL;
    }

    if(*ptr < 251)
    {
        size = (uintptr_t)*ptr;
        offset = 1;
    }
    else
    {
        switch(*(ptr))
        {
        case 0xfb:
            return NULL;
        case 0xfc:
            size = *(ptr + 1) + (*(ptr + 2) << 8);
            offset = 2;
            break;
        case 0xfd:
            size = *ptr + (*(ptr + 2) << 8) + (*(ptr + 3) << 16);
            offset = 3;
            break;
        case 0xfe:
            size = *ptr + ((*(ptr + 2) << 8)) + (*(ptr + 3) << 16) +
                    (*(ptr + 4) << 24) + ((uintptr_t)*(ptr + 5) << 32) + ((uintptr_t)*(ptr + 6) << 40) +
                    ((uintptr_t)*(ptr + 7) << 48) + ((uintptr_t)*(ptr + 8) << 56);
            offset = 8;
            break;
        default:

            return NULL;
        }
    }

    rval = malloc(sizeof(char)*(size+1));
    if(rval)
    {
        memcpy(rval,ptr + offset,size);
        memset(rval + size,0,1);

    }
    return rval;
}

/**
 * Handle the result returned from a SHOW DATABASES query. Parse the result set
 * and associate these databases to the service that returned them.
 * @param rses
 * @param target
 * @param buf
 * @return
 */
bool
parse_mapping_response(ROUTER_CLIENT_SES* rses, char* target, GWBUF* buf)
{
   bool rval = false;
   unsigned char* ptr;
   int more = 0;

   if(PTR_IS_RESULTSET(((unsigned char*)buf->start)) &&
      modutil_count_signal_packets(buf,0,0,&more) == 2)
   {
       ptr = (unsigned char*)buf->start;

       if(ptr[5] != 1)
       {
	   /** Something else came back, discard and return with an error*/
	   return false;
       }

       /** Skip column definitions */
       while(!PTR_IS_EOF(ptr))
       {
	   ptr += gw_mysql_get_byte3(ptr) + 4;
       }

       /** Skip first EOF packet */
       ptr += gw_mysql_get_byte3(ptr) + 4;

       while(!PTR_IS_EOF(ptr))
       {
	   int payloadlen = gw_mysql_get_byte3(ptr);
	   int packetlen = payloadlen + 4;
	   char* data = get_lenenc_str(ptr+4);

	   if(data)
	   {
	       if(hashtable_add(rses->dbhash,data,target))
	       {
		   MXS_INFO("shardrouter: <%s, %s>",target,data);
	       }
	       free(data);
	   }
	   ptr += packetlen;
       }
       rval = true;

   }

   return rval;
}

/**
 * Validate the status of the subservice.
 * @param sub Subservice to validate
 * @return True if the subservice is valid, false if the session or it's router
 * are NULL or the session or the service is not in a valid state.
 */
bool subsvc_is_valid(SUBSERVICE* sub)
{
    
    if(sub->session == NULL || 
       sub->service->router == NULL)
    {
        return false;
    }
    
    spinlock_acquire(&sub->session->ses_lock);
    session_state_t ses_state = sub->session->state;
    spinlock_release(&sub->session->ses_lock);

    spinlock_acquire(&sub->service->spin);
    int svc_state = sub->service->state;
    spinlock_release(&sub->service->spin);

    if(ses_state == SESSION_STATE_ROUTER_READY &&
       (svc_state != SERVICE_STATE_FAILED ||
        svc_state != SERVICE_STATE_STOPPED))
    {
        return true;
    }

    return false;
}

/**
 * Map the databases of all subservices.
 * @param inst router instance
 * @param session router session
 * @return 0 on success, 1 on error
 */
int
gen_subsvc_dblist(ROUTER_INSTANCE* inst, ROUTER_CLIENT_SES* session)
{
    const char* query = "SHOW DATABASES;";
    GWBUF *buffer, *clone;
    int i, rval = 0;
    unsigned int len;

    session->hash_init = false;

    len = strlen(query);
    buffer = gwbuf_alloc(len + 4);
    *((unsigned char*) buffer->start) = len;
    *((unsigned char*) buffer->start + 1) = len >> 8;
    *((unsigned char*) buffer->start + 2) = len >> 16;
    *((unsigned char*) buffer->start + 3) = 0x0;
    *((unsigned char*) buffer->start + 4) = 0x03;
    memcpy(buffer->start + 5, query, strlen(query));


    for(i = 0; i < session->n_subservice; i++)
    {
        if(SUBSVC_IS_OK(session->subservice[i]))
        {
            clone = gwbuf_clone(buffer);
            
            rval |= !SESSION_ROUTE_QUERY(session->subservice[i]->session,clone);
            subsvc_set_state(session->subservice[i],SUBSVC_WAITING_RESULT|SUBSVC_QUERY_ACTIVE);
        }
    }

    gwbuf_free(buffer);
    return rval;
}

/**
 * Check the hashtable for the right backend for this query.
 * @param router Router instance
 * @param client Client router session
 * @param buffer Query to inspect
 * @return Name of the backend or NULL if the query contains no known databases.
 */
char*
get_shard_target_name(ROUTER_INSTANCE* router, ROUTER_CLIENT_SES* client, GWBUF* buffer, skygw_query_type_t qtype)
{
    HASHTABLE* ht = client->dbhash;
    int sz = 0, i, j;
    char** dbnms = NULL;
    char *rval = NULL;
    char *query = NULL,*tmp = NULL;
    bool has_dbs = false; /**If the query targets any database other than the current one*/

    if(!query_is_parsed(buffer))
    {
        parse_query(buffer);
    }

    dbnms = skygw_get_database_names(buffer, &sz);

    if(sz > 0)
    {
        for(i = 0; i < sz; i++)
        {

            if((rval = (char*) hashtable_fetch(ht, dbnms[i])))
            {
                if(strcmp(dbnms[i],"information_schema") == 0)
                {
                    has_dbs = false;
                    rval = NULL;
                }
                else
                {
                    MXS_INFO("shardrouter: Query targets database '%s' on server '%s",dbnms[i],rval);
		    has_dbs = true;
                }
            }
            free(dbnms[i]);
        }
        free(dbnms);
    }

    if(QUERY_IS_TYPE(qtype, QUERY_TYPE_SHOW_TABLES))
    {
        query = modutil_get_SQL(buffer);
        if((tmp = strcasestr(query,"from")))
        {
            char* tok = strtok(tmp, " ;");
            tok = strtok(NULL," ;");
            ss_dassert(tok != NULL);
            tmp = (char*) hashtable_fetch(ht, tok);
            if(tmp)
                MXS_INFO("shardrouter: SHOW TABLES with specific database '%s' on server '%s'", tok, tmp);
        }
        free(query);
        
        if(tmp == NULL)
        {
            rval = (char*) hashtable_fetch(ht, client->rses_mysql_session->db);
            MXS_INFO("shardrouter: SHOW TABLES query, current database '%s' on server '%s'",
                     client->rses_mysql_session->db,rval);
        }
        else
        {
            rval = tmp;            
            has_dbs = true;
        }
    }
    
    
    if(buffer->hint && buffer->hint->type == HINT_ROUTE_TO_NAMED_SERVER)
    {
        for(i = 0; i < client->n_subservice; i++)
        {

            SERVER_REF *srvrf = client->subservice[i]->service->dbref;
            while(srvrf)
            {
                if(strcmp(srvrf->server->unique_name,buffer->hint->data) == 0)
                {
                    rval = srvrf->server->unique_name;
                    MXS_INFO("shardrouter: Routing hint found (%s)",rval);
                    
                }
                srvrf = srvrf->next;
            }
        }
    }
    
    
    if(rval == NULL && !has_dbs && client->rses_mysql_session->db[0] != '\0')
    {
        /**
         * If the query contains no explicitly stated databases proceed to
         * check if the session has an active database and if it is sharded.
         */

        rval = (char*) hashtable_fetch(ht, client->rses_mysql_session->db);
	if(rval)
	{
	    MXS_INFO("shardrouter: Using active database '%s'",client->rses_mysql_session->db);
	}
    }
   
    return rval;
}

char**
tokenize_string(char* str)
{
    char *tok;
    char **list = NULL;
    int sz = 2, count = 0;

    tok = strtok(str, ", ");

    if(tok == NULL)
        return NULL;

    list = (char**) malloc(sizeof(char*)*(sz));

    while(tok)
    {
        if(count + 1 >= sz)
        {
            char** tmp = realloc(list, sizeof(char*)*(sz * 2));
            if(tmp == NULL)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("realloc returned NULL: %s.",
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                free(list);
                return NULL;
            }
            list = tmp;
            sz *= 2;
        }
        list[count] = strdup(tok);
        count++;
        tok = strtok(NULL, ", ");
    }
    list[count] = NULL;
    return list;
}

/**
 * This is the function used to channel replies from a subservice up to the client.
 * The values passed are set in the newSession function.
 * @param instance The router client session
 * @param session This is the session that's allocated for the subservice
 * @param reply The reply from the downstream filter or router
 * @return returns 1 for success and 0 for error
 */
static int
filterReply(FILTER* instance, void *session, GWBUF *reply)
{

    ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES*) instance;
    SUBSERVICE* subsvc;
    int i, rv = 1;
    sescmd_cursor_t* scur;
    GWBUF* tmp = NULL;

    if(!rses_begin_locked_router_action(rses))
    {
	tmp = reply;
	while((tmp = gwbuf_consume(tmp,gwbuf_length(tmp))));
        return 0;
    }

    subsvc = get_subsvc_from_ses(rses, session);

    if(rses->init & INIT_MAPPING)
    {
	bool mapped = true, logged = false;
	int i;

	for(i = 0; i < rses->n_subservice; i++)
	{

	    if(subsvc->session == rses->subservice[i]->session &&
	     !SUBSVC_IS_MAPPED(rses->subservice[i]))
	    {
		rses->subservice[i]->state |= SUBSVC_MAPPED;
		parse_mapping_response(rses,
				 rses->subservice[i]->service->name,
				 reply);

	    }

	    if(SUBSVC_IS_OK(rses->subservice[i]) &&
	     !SUBSVC_IS_MAPPED(rses->subservice[i]))
	    {
		mapped = false;
		if(!logged)
		{
/*
                    MXS_DEBUG("schemarouter: Still waiting for reply to SHOW DATABASES from %s for session %p",
                              bkrf[i].bref_backend->backend_server->unique_name,
                              rses->rses_client_dcb->session);
*/
                    logged = true;
		}
	    }
	}

	if(mapped)
	{
	    /*
	     * Check if the session is reconnecting with a database name
	     * that is not in the hashtable. If the database is not found
	     * then close the session.
	     */

	    rses->init &= ~INIT_MAPPING;

	    if(rses->init & INIT_USE_DB)
	    {
		char* target;

		if((target = hashtable_fetch(rses->dbhash,
					 rses->connect_db)) == NULL)
		{
		    MXS_INFO("schemarouter: Connecting to a non-existent database '%s'",
                             rses->connect_db);
		    rses->rses_closed = true;
		    if(rses->queue)
		    {
			while((rses->queue = gwbuf_consume(
			 rses->queue,gwbuf_length(rses->queue))));
		    }
		    rses_end_locked_router_action(rses);
		    goto retblock;
		}

		/* Send a COM_INIT_DB packet to the server with the right database
		 * and set it as the client's active database */

		unsigned int qlen;
		GWBUF* buffer;

		qlen = strlen(rses->connect_db);
		buffer = gwbuf_alloc(qlen + 5);
		if(buffer == NULL)
		{
		    MXS_ERROR("Buffer allocation failed.");
		    rses->rses_closed = true;
		    if(rses->queue)
			gwbuf_free(rses->queue);
		    goto retblock;
		}

		gw_mysql_set_byte3((unsigned char*)buffer->start,qlen+1);
		gwbuf_set_type(buffer,GWBUF_TYPE_MYSQL);
		*((unsigned char*)buffer->start + 3) = 0x0;
		*((unsigned char*)buffer->start + 4) = 0x2;
		memcpy(buffer->start+5,rses->connect_db,qlen);
		DCB* dcb = NULL;

		SESSION_ROUTE_QUERY(subsvc->session,buffer);

		goto retblock;
	    }

	    if(rses->queue)
	    {
		GWBUF* tmp = rses->queue;
		rses->queue = rses->queue->next;
		tmp->next = NULL;
		char* querystr = modutil_get_SQL(tmp);
		MXS_DEBUG("schemarouter: Sending queued buffer for session %p: %s",
                          rses->rses_client_dcb->session,
                          querystr);
		poll_add_epollin_event_to_dcb(rses->routedcb,tmp);
		free(querystr);

	    }
	    MXS_DEBUG("session [%p] database map finished.", rses);
	}

	goto retblock;
    }

    if(rses->queue)
    {
	GWBUF* tmp = rses->queue;
	rses->queue = rses->queue->next;
	tmp->next = NULL;
	char* querystr = modutil_get_SQL(tmp);
	MXS_DEBUG("schemarouter: Sending queued buffer for session %p: %s",
                  rses->rses_client_dcb->session,
                  querystr);
	poll_add_epollin_event_to_dcb(rses->routedcb,tmp);
	free(querystr);
	tmp = NULL;
    }

    if(rses->init & INIT_USE_DB)
    {
	MXS_DEBUG("schemarouter: Reply to USE '%s' received for session %p",
                  rses->connect_db,
                  rses->rses_client_dcb->session);
	rses->init &= ~INIT_USE_DB;
	strcpy(rses->rses_mysql_session->db,rses->connect_db);
	ss_dassert(rses->init == INIT_READY);
	if(reply)
	{
	    tmp = reply;
	    while((tmp = gwbuf_consume(tmp,gwbuf_length(tmp))));
	    tmp = NULL;
	}
	goto retblock;
    }

    scur = subsvc->scur;

    if(sescmd_cursor_is_active(scur))
    {
        if(!sescmd_cursor_next(scur))
        {
            sescmd_cursor_set_active(scur, false);
        }
        else
        {
            execute_sescmd_in_backend(subsvc);
            goto retblock;
        }
    }

    rv = SESSION_ROUTE_REPLY(rses->session, reply);

    retblock:
	    rses_end_locked_router_action(rses);
    return rv;
}

/**
 * This function reads the DCB's readqueue and sends it as a reply to the session
 * who owns the DCB. 
 * @param dcb The dummy DCB
 * @return 1 on success, 0 on failure
 */
int fakeReply(DCB* dcb)
{
    if(dcb->dcb_readqueue)
    {
        GWBUF* tmp = dcb->dcb_readqueue;
        dcb->dcb_readqueue = NULL;
        return SESSION_ROUTE_REPLY(dcb->session, tmp);
    }
    return 1;
}



/**
 * This function reads the DCB's readqueue and sends it as a query directly to the router.
 * The function is used to route queued queries to the subservices when replies are received.
 * @param dcb The dummy DCB
 * @return 1 on success, 0 on failure
 */
int fakeQuery(DCB* dcb)
{
    if(dcb->dcb_readqueue)
    {
        GWBUF* tmp = dcb->dcb_readqueue;
        void* rinst = dcb->session->service->router_instance;
        void *rses = dcb->session->router_session;
        
        dcb->dcb_readqueue = NULL;
        return dcb->session->service->router->routeQuery(rinst,rses,tmp);
    }
    return 1;
}

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
    MXS_NOTICE("Initializing statemend-based read/write split router module.");
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
ROUTER_OBJECT*
GetModuleObject()
{
    return &MyObject;
}

static void
refreshInstance(
                ROUTER_INSTANCE* router,
                CONFIG_PARAMETER* singleparam)
{
    CONFIG_PARAMETER* param;
    bool refresh_single;
    config_param_type_t paramtype;

    if(singleparam != NULL)
    {
        param = singleparam;
        refresh_single = true;
    }
    else
    {
        param = router->service->svc_config_param;
        refresh_single = false;
    }
    paramtype = config_get_paramtype(param);

    while(param != NULL)
    {
        /** Catch unused parameter types */
        ss_dassert(paramtype == COUNT_TYPE ||
                   paramtype == PERCENT_TYPE ||
                   paramtype == SQLVAR_TARGET_TYPE ||
                   paramtype == STRING_TYPE);

        if(paramtype == COUNT_TYPE)
        {
        }
        else if(paramtype == PERCENT_TYPE)
        {
        }
            /*else if (paramtype == STRING_TYPE)
                            {
                                    if (strncmp(param->name, 
                                                            "ignore_databases", 
                                                            MAX_PARAM_LEN) == 0)
                                    {
                                        router->ignore_list = tokenize_string(param->qfd.valstr);
                                    }
            }*/

        if(refresh_single)
        {
            break;
        }
        param = param->next;
    }


}

/**
 * Create an instance of shardrouter statement router within the MaxScale.
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
    ROUTER_INSTANCE* router;
    char *services, *tok, *saveptr;
    SERVICE **res_svc, **temp;
    CONFIG_PARAMETER* conf;
    int i = 0, sz;

    const int min_nsvc = 1;

    if((router = calloc(1, sizeof(ROUTER_INSTANCE))) == NULL)
    {
        return NULL;
    }
    router->service = service;
    spinlock_init(&router->lock);

    conf = config_get_param(service->svc_config_param, "subservices");

    if(conf == NULL)
    {
        MXS_ERROR("No 'subservices' confguration parameter found. "
                  " Expected a list of service names.");
        free(router);
        return NULL;
    }

    services = strdup(conf->value);
    sz = 2;

    if((res_svc = calloc(sz, sizeof(SERVICE*))) == NULL)
    {
	free(router);
	free(services);
	MXS_ERROR("Memory allocation failed.");
	return NULL;
    }

    tok = strtok_r(services, ",",&saveptr);



    while(tok)
    {
        if(sz <= i)
        {
            temp = realloc(res_svc, sizeof(SERVICE*)*(sz * 2));
            if(temp == NULL)
            {
                MXS_ERROR("Memory reallocation failed.");
                MXS_DEBUG("shardrouter.c: realloc returned NULL. "
                          "service count[%d] buffer size [%lu] tried to allocate [%lu]",
                          sz, sizeof(SERVICE*) * (sz), sizeof(SERVICE*) * (sz * 2));
                free(res_svc);
                free(router);
                return NULL;
            }
            sz = sz * 2;
            res_svc = temp;
        }

        res_svc[i] = service_find(tok);
	if(res_svc[i] == NULL)
	{
	    free(res_svc);
	    free(router);
	    MXS_ERROR("No service named '%s' found.", options[i]);
	    return NULL;
	}
        i++;
	tok = strtok_r(NULL,",",&saveptr);
    }

    free(services);


    router->services = res_svc;
    router->n_services = i;

    if(i < min_nsvc)
    {
        MXS_ERROR("Not enough parameters for 'subservice' router option. Shardrouter requires at least %d "
                  "configured services to work.", min_nsvc);
        free(router->services);
        free(router);
        return NULL;
    }

    /**
     * Process the options
     */
    router->bitmask = 0;
    router->bitvalue = 0;

    /**
     * Read config version number from service to inform what configuration 
     * is used if any.
     */
    router->shardrouter_version = service->svc_config_version;

    /**
     * We have completed the creation of the router data, so now
     * insert this router into the linked list of routers
     * that have been created with this module.
     */
    spinlock_acquire(&instlock);
    router->next = instances;
    instances = router;
    spinlock_release(&instlock);

    return(ROUTER *) router;
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
static void*
newSession(
           ROUTER* router_inst,
           SESSION* session)
{
    SUBSERVICE* subsvc;
    ROUTER_CLIENT_SES* client_rses = NULL;
    ROUTER_INSTANCE* router = (ROUTER_INSTANCE *) router_inst;
    FILTER_DEF* dummy_filterdef;
    UPSTREAM* dummy_upstream;

    int i, j;
    client_rses = (ROUTER_CLIENT_SES *) calloc(1, sizeof(ROUTER_CLIENT_SES));

    if(client_rses == NULL)
    {
        ss_dassert(false);
        goto return_rses;
    }

#if defined(SS_DEBUG)
    client_rses->rses_chk_top = CHK_NUM_ROUTER_SES;
    client_rses->rses_chk_tail = CHK_NUM_ROUTER_SES;
#endif

    client_rses->router = router;
    client_rses->rses_mysql_session = (MYSQL_session*) session->data;
    client_rses->rses_client_dcb = (DCB*) session->client;
    client_rses->rses_autocommit_enabled = true;
    client_rses->rses_transaction_active = false;
    client_rses->session = session;
    client_rses->replydcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);
    client_rses->replydcb->func.read = fakeReply;
    client_rses->replydcb->state = DCB_STATE_POLLING;
    client_rses->replydcb->session = session;
    
    client_rses->routedcb = dcb_alloc(DCB_ROLE_REQUEST_HANDLER);
    client_rses->routedcb->func.read = fakeQuery;
    client_rses->routedcb->state = DCB_STATE_POLLING;
    client_rses->routedcb->session = session;
    
    spinlock_init(&client_rses->rses_lock);

    client_rses->subservice = calloc(router->n_services, sizeof(SUBSERVICE*));

    if(client_rses->subservice == NULL)
    {
        free(client_rses);
        return NULL;
    }

    client_rses->n_subservice = router->n_services;

    for(i = 0; i < client_rses->n_subservice; i++)
    {
        if((subsvc = calloc(1, sizeof(SUBSERVICE))) == NULL)
        {
            goto errorblock;
        }
        
        /* TODO: add NULL value checks */
        client_rses->subservice[i] = subsvc;
        
        subsvc->scur = calloc(1,sizeof(sescmd_cursor_t));
        if(subsvc->scur == NULL)
        {
            subsvc_set_state(subsvc,SUBSVC_FAILED);
            MXS_ERROR("Memory allocation failed in shardrouter.");
            continue;
        }
        subsvc->scur->scmd_cur_rses = client_rses;
        subsvc->scur->scmd_cur_ptr_property = client_rses->rses_properties;
        subsvc->service = router->services[i];
        subsvc->dcb = dcb_clone(client_rses->rses_client_dcb);
        
        if(subsvc->dcb == NULL){
            subsvc_set_state(subsvc,SUBSVC_FAILED);
            MXS_ERROR("Failed to clone client DCB in shardrouter.");
            continue;
        }
        
        subsvc->session = session_alloc(subsvc->service,subsvc->dcb);
        
        if(subsvc->session == NULL){
            dcb_close(subsvc->dcb);
            subsvc->dcb = NULL;
            subsvc_set_state(subsvc,SUBSVC_FAILED);
            MXS_ERROR("Failed to create subsession for service %s in shardrouter.",subsvc->service->name);
            continue;
        }
        
        dummy_filterdef = filter_alloc("tee_dummy","tee_dummy");
        
        if(dummy_filterdef == NULL)
        {
            subsvc_set_state(subsvc,SUBSVC_FAILED);
            MXS_ERROR("Failed to allocate filter definition in shardrouter.");
            continue;
        }
        dummy_filterdef->obj = &dummyObject;
        dummy_filterdef->filter = (FILTER*)client_rses; 
        dummy_upstream = filterUpstream(dummy_filterdef,subsvc->session,&subsvc->session->tail);
        
        if(dummy_upstream == NULL)
        {
           subsvc_set_state(subsvc,SUBSVC_FAILED);
           MXS_ERROR("Failed to set filterUpstream in shardrouter.");
            continue; 
        }
        
        subsvc->session->tail = *dummy_upstream;
        
        
        subsvc_set_state(subsvc,SUBSVC_OK);
        
        free(dummy_upstream);
    }

    router->stats.n_sessions += 1;

    /**
     * Version is bigger than zero once initialized.
     */
    atomic_add(&client_rses->rses_versno, 2);
    ss_dassert(client_rses->rses_versno == 2);

    client_rses->dbhash = hashtable_alloc(100, simple_str_hash,strcmp);
    hashtable_memory_fns(client_rses->dbhash, (HASHMEMORYFN) strdup,
                         (HASHMEMORYFN) strdup,
                         (HASHMEMORYFN) free,
                         (HASHMEMORYFN) free);

    /**
     * Add this session to end of the list of active sessions in router.
     */
    spinlock_acquire(&router->lock);
    client_rses->next = router->connections;
    router->connections = client_rses;
    spinlock_release(&router->lock);
    goto retblock;
return_rses:
#if defined(SS_DEBUG)
    if(client_rses != NULL)
    {
        CHK_CLIENT_RSES(client_rses);
    }
#endif
errorblock:

    if(client_rses && client_rses->subservice)
    {
        for(j = 0; j < i; j++)
        {
            free(client_rses->subservice[i]);
        }
        free(client_rses->subservice);
    }
    free(client_rses);
    client_rses = NULL;
retblock:
    return(void *) client_rses;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance	The router instance data
 * @param session	The session being closed
 */
static void
closeSession(
             ROUTER* instance,
             void* router_session)
{
    ROUTER_CLIENT_SES* router_cli_ses;
    int i;
    MXS_DEBUG("%lu [RWSplit:closeSession]", pthread_self());

    /** 
     * router session can be NULL if newSession failed and it is discarding
     * its connections and DCB's. 
     */
    if(router_session == NULL)
    {
        return;
    }
    router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    CHK_CLIENT_RSES(router_cli_ses);

    /**
     * Lock router client session for secure read and update.
     */
    if(!router_cli_ses->rses_closed &&
       rses_begin_locked_router_action(router_cli_ses))
    {
        ROUTER_OBJECT* rtr;
        ROUTER* rinst;
        void *rses;
        SESSION *ses;
        
        for(i = 0;i<router_cli_ses->n_subservice;i++)
        {
            rtr = router_cli_ses->subservice[i]->service->router;
            rinst = router_cli_ses->subservice[i]->service->router_instance;
            ses = router_cli_ses->subservice[i]->session;
            if(ses != NULL)
            {
                rses = ses->router_session;
                ses->state = SESSION_STATE_STOPPING;
                rtr->closeSession(rinst,rses);
            }
            router_cli_ses->subservice[i]->state = SUBSVC_CLOSED;
        }
        router_cli_ses->replydcb->session = NULL;
        router_cli_ses->routedcb->session = NULL;
        dcb_close(router_cli_ses->replydcb);
        dcb_close(router_cli_ses->routedcb);
        
        /** Unlock */
        rses_end_locked_router_action(router_cli_ses);
    }
}

static void
freeSession(
            ROUTER* router_instance,
            void* router_client_session)
{
    ROUTER_CLIENT_SES* router_cli_ses;
    int i;

    router_cli_ses = (ROUTER_CLIENT_SES *) router_client_session;

   
    /** 
     * For each property type, walk through the list, finalize properties 
     * and free the allocated memory. 
     */
    for(i = RSES_PROP_TYPE_FIRST; i < RSES_PROP_TYPE_COUNT; i++)
    {
        rses_property_t* p = router_cli_ses->rses_properties[i];
        rses_property_t* q = p;

        while(p != NULL)
        {
            q = p->rses_prop_next;
            rses_property_done(p);
            p = q;
        }
    }
    
    for(i = 0;i<router_cli_ses->n_subservice;i++)
    {
        
        /* TODO: free router client session */
        free(router_cli_ses->subservice[i]);
    }

    free(router_cli_ses->subservice);

    /*
     * We are no longer in the linked list, free
     * all the memory and other resources associated
     * to the client session.
     */
    hashtable_free(router_cli_ses->dbhash);    
    free(router_cli_ses);
    return;
}

/**
 * Examine the query type, transaction state and routing hints. Find out the
 * target for query routing.
 * 
 *  @param qtype      Type of query 
 *  @param trx_active Is transcation active or not
 *  @param hint       Pointer to list of hints attached to the query buffer
 * 
 *  @return bitfield including the routing target, or the target server name 
 *          if the query would otherwise be routed to slave.
 */
static route_target_t
get_shard_route_target(skygw_query_type_t qtype,
                       bool trx_active, /*< !!! turha ? */
                       HINT* hint) /*< !!! turha ? */
{
    route_target_t target = TARGET_UNDEFINED;

    /**
     * These queries are not affected by hints
     */
    if(QUERY_IS_TYPE(qtype, QUERY_TYPE_SESSION_WRITE) ||
       QUERY_IS_TYPE(qtype, QUERY_TYPE_PREPARE_STMT) ||
       QUERY_IS_TYPE(qtype, QUERY_TYPE_PREPARE_NAMED_STMT) ||
       QUERY_IS_TYPE(qtype, QUERY_TYPE_GSYSVAR_WRITE) ||
       /** enable or disable autocommit are always routed to all */
       QUERY_IS_TYPE(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT) ||
       QUERY_IS_TYPE(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT))
    {
        /** hints don't affect on routing */
        target = TARGET_ALL;
    }
    else if(QUERY_IS_TYPE(qtype, QUERY_TYPE_SYSVAR_READ) ||
            QUERY_IS_TYPE(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        target = TARGET_ANY;
    }
#if defined(SS_DEBUG)
    MXS_INFO("Selected target \"%s\"", STRTARGET(target));
#endif
    return target;
}

/**
 * This function creates a custom SHOW DATABASES response by iterating through
 * the database names in the session's hashtable. This generates a complete list
 * of all available databases in all of the clusters.
 * @param router The router instance
 * @param client Router client session
 * @return Pointer to the generated response
 */
GWBUF*
gen_show_dbs_response(ROUTER_INSTANCE* router, ROUTER_CLIENT_SES* client)
{
    GWBUF* rval = NULL;
    HASHTABLE* ht = client->dbhash;
    SUBSERVICE** subsvcs = client->subservice;
    HASHITERATOR* iter = hashtable_iterator(ht);
    unsigned int coldef_len = 0;
    int j;
    char dbname[MYSQL_DATABASE_MAXLEN + 1];
    char *value;
    unsigned char* ptr;
    char catalog[4] = {0x03, 'd', 'e', 'f'};
    const char* schema = "information_schema";
    const char* table = "SCHEMATA";
    const char* org_table = "SCHEMATA";
    const char* name = "Database";
    const char* org_name = "SCHEMA_NAME";
    char next_length = 0x0c;
    char charset[2] = {0x21, 0x00};
    char column_length[4] = {MYSQL_DATABASE_MAXLEN,
        MYSQL_DATABASE_MAXLEN >> 8,
        MYSQL_DATABASE_MAXLEN >> 16,
        MYSQL_DATABASE_MAXLEN >> 24};
    char column_type = 0xfd;

    char eof[9] = {0x05, 0x00, 0x00,
        0x03, 0xfe, 0x00,
        0x00, 0x22, 0x00};
#if defined(NOT_USED)
    char ok_packet[11] = {0x07, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00};
#endif
/* The meanings of the "magic numbers" can be found in the COM_QUERY-response definition */
    coldef_len = sizeof(catalog) + strlen(schema) + 1 +
            strlen(table) + 1 +
            strlen(org_table) + 1 +
            strlen(name) + 1 +
            strlen(org_name) + 1 +
            1 + 2 + 4 + 1 + 2 + 1 + 2;


    rval = gwbuf_alloc(5 + 4 + coldef_len + sizeof(eof));

    ptr = rval->start;

    /**First packet*/

    *ptr++ = 0x01;
    *ptr++ = 0x00;
    *ptr++ = 0x00;
    *ptr++ = 0x01;
    *ptr++ = 0x01;

    /**Second packet containing the column definitions*/

    *ptr++ = coldef_len;
    *ptr++ = coldef_len >> 8;
    *ptr++ = coldef_len >> 16;
    *ptr++ = 0x02;

    memcpy((void*) ptr, catalog, 4);
    ptr += 4;

    *ptr++ = strlen(schema);
    memcpy((void*) ptr, schema, strlen(schema));
    ptr += strlen(schema);

    *ptr++ = strlen(table);
    memcpy((void*) ptr, table, strlen(table));
    ptr += strlen(table);

    *ptr++ = strlen(org_table);
    memcpy((void*) ptr, org_table, strlen(org_table));
    ptr += strlen(org_table);

    *ptr++ = strlen(name);
    memcpy((void*) ptr, name, strlen(name));
    ptr += strlen(name);

    *ptr++ = strlen(org_name);
    memcpy((void*) ptr, org_name, strlen(org_name));
    ptr += strlen(org_name);

    *ptr++ = next_length;
    *ptr++ = charset[0];
    *ptr++ = charset[1];
    *ptr++ = column_length[0];
    *ptr++ = column_length[1];
    *ptr++ = column_length[2];
    *ptr++ = column_length[3];
    *ptr++ = column_type;
    *ptr++ = 0x01;
    memset(ptr, 0, 4);
    ptr += 4;

    memcpy(ptr, eof, sizeof(eof));

    unsigned int packet_num = 4;

    while((value = (char*) hashtable_next(iter)))
    {
        char* svc = hashtable_fetch(ht, value);
        for(j = 0; subsvcs[j]; j++)
        {
            if(strcmp(subsvcs[j]->service->name, svc) == 0)
            {
                if(subsvcs[j]->state & SUBSVC_OK)
                {
                    GWBUF* temp;
                    int plen = strlen(value) + 1;

                    sprintf(dbname, "%s", value);
                    temp = gwbuf_alloc(plen + 4);

                    ptr = temp->start;
                    *ptr++ = plen;
                    *ptr++ = plen >> 8;
                    *ptr++ = plen >> 16;
                    *ptr++ = packet_num++;
                    *ptr++ = plen - 1;
                    memcpy(ptr, dbname, plen - 1);

                    /** Append the row*/
                    rval = gwbuf_append(rval, temp);
                }
                break;
            }
        }
    }

    eof[3] = packet_num;

    GWBUF* last_packet = gwbuf_alloc(sizeof(eof));
    memcpy(last_packet->start, eof, sizeof(eof));
    rval = gwbuf_append(rval, last_packet);

    rval = gwbuf_make_contiguous(rval);

    return rval;
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
 * @param instance		The query router instance
 * @param router_session	The session associated with the client
 * @param querybuf		MaxScale buffer queue with received packet
 *
 * @return if succeed 1, otherwise 0
 * If routeQuery fails, it means that router session has failed.
 * In any tolerated failure, handleError is called and if necessary,
 * an error message is sent to the client.
 */
static int
routeQuery(ROUTER* instance,
           void* router_session,
           GWBUF* querybuf)
{
    skygw_query_type_t qtype = QUERY_TYPE_UNKNOWN;
    mysql_server_cmd_t packet_type;
    uint8_t* packet;
    int i,ret = 1;
    SUBSERVICE* target_subsvc;
    ROUTER_INSTANCE* inst = (ROUTER_INSTANCE *) instance;
    ROUTER_CLIENT_SES* router_cli_ses = (ROUTER_CLIENT_SES *) router_session;
    bool rses_is_closed = false;
    bool change_successful = false;
    route_target_t route_target = TARGET_UNDEFINED;
    bool succp = false;
    char* tname = NULL;
    char db[MYSQL_DATABASE_MAXLEN + 1];
    char errbuf[26+MYSQL_DATABASE_MAXLEN];

    MXS_DEBUG("shardrouter: routeQuery");
    CHK_CLIENT_RSES(router_cli_ses);

    /** Dirty read for quick check if router is closed. */
    if(router_cli_ses->rses_closed)
    {
        rses_is_closed = true;
    }
    ss_dassert(!GWBUF_IS_TYPE_UNDEFINED(querybuf));

    /** Lock router session */
    if(!rses_begin_locked_router_action(router_cli_ses))
    {
        MXS_INFO("Route query aborted! Routing session is closed <");
        ret = 0;
        goto retblock;
    }
    if(!(rses_is_closed = router_cli_ses->rses_closed))
        {
	    if(router_cli_ses->init & INIT_UNINT)
	    {
		/* Generate database list */
		gen_subsvc_dblist(inst,router_cli_ses);

	    }

	    if(router_cli_ses->init & INIT_MAPPING)
	    {

		char* querystr = modutil_get_SQL(querybuf);
		MXS_DEBUG("shardrouter: Storing query for session %p: %s",
                          router_cli_ses->rses_client_dcb->session,
                          querystr);
		free(querystr);
		gwbuf_make_contiguous(querybuf);
		GWBUF* ptr = router_cli_ses->queue;

		while(ptr && ptr->next)
		{
		    ptr = ptr->next;
		}

		if(ptr == NULL)
		{
		    router_cli_ses->queue = querybuf;
		}
		else
		{
		    ptr->next = querybuf;

		}
		rses_end_locked_router_action(router_cli_ses);
		return 1;
	    }

        }

    rses_end_locked_router_action(router_cli_ses);
    
    packet = GWBUF_DATA(querybuf);
    packet_type = packet[4];

    if(rses_is_closed)
    {
        /** 
         * MYSQL_COM_QUIT may have sent by client and as a part of backend 
         * closing procedure.
         */
        if(packet_type != MYSQL_COM_QUIT)
        {
            char* query_str = modutil_get_query(querybuf);

            MXS_ERROR("Can't route %s:%s:\"%s\" to "
                      "backend server. Router is closed.",
                      STRPACKETTYPE(packet_type),
                      STRQTYPE(qtype),
                      (query_str == NULL ? "(empty)" : query_str));
            free(query_str);
        }
        ret = 0;
        goto retblock;
    }

    /** If buffer is not contiguous, make it such */
    if(querybuf->next != NULL)
    {
        querybuf = gwbuf_make_contiguous(querybuf);
    }

    switch(packet_type)
    {
    case MYSQL_COM_QUIT: /*< 1 QUIT will close all sessions */
    case MYSQL_COM_INIT_DB: /*< 2 DDL must go to the master */
    case MYSQL_COM_REFRESH: /*< 7 - I guess this is session but not sure */
    case MYSQL_COM_DEBUG: /*< 0d all servers dump debug info to stdout */
    case MYSQL_COM_PING: /*< 0e all servers are pinged */
    case MYSQL_COM_CHANGE_USER: /*< 11 all servers change it accordingly */
    case MYSQL_COM_STMT_CLOSE: /*< free prepared statement */
    case MYSQL_COM_STMT_SEND_LONG_DATA: /*< send data to column */
    case MYSQL_COM_STMT_RESET: /*< resets the data of a prepared statement */
        qtype = QUERY_TYPE_SESSION_WRITE;
        break;

    case MYSQL_COM_CREATE_DB: /**< 5 DDL must go to the master */
    case MYSQL_COM_DROP_DB: /**< 6 DDL must go to the master */
        qtype = QUERY_TYPE_WRITE;
        break;

    case MYSQL_COM_QUERY:
        qtype = query_classifier_get_type(querybuf);
        break;

    case MYSQL_COM_STMT_PREPARE:
        qtype = query_classifier_get_type(querybuf);
        qtype |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MYSQL_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        qtype = QUERY_TYPE_EXEC_STMT;
        break;

    case MYSQL_COM_SHUTDOWN: /**< 8 where should shutdown be routed ? */
    case MYSQL_COM_STATISTICS: /**< 9 ? */
    case MYSQL_COM_PROCESS_INFO: /**< 0a ? */
    case MYSQL_COM_CONNECT: /**< 0b ? */
    case MYSQL_COM_PROCESS_KILL: /**< 0c ? */
    case MYSQL_COM_TIME: /**< 0f should this be run in gateway ? */
    case MYSQL_COM_DELAYED_INSERT: /**< 10 ? */
    case MYSQL_COM_DAEMON: /**< 1d ? */
    default:
        break;
    } /**< switch by packet type */
    
    if(packet_type == MYSQL_COM_INIT_DB)
    {
        if(!(change_successful = change_current_db(router_cli_ses->current_db,
						   router_cli_ses->dbhash,
						   querybuf)))
        {
	    extract_database(querybuf,db);
	    snprintf(errbuf,25+MYSQL_DATABASE_MAXLEN,"Unknown database: %s",db);
	    create_error_reply(errbuf,router_cli_ses->replydcb);
            MXS_ERROR("Changing database failed.");
            return 1;
        }
    }

    /**
     * Find out whether the query should be routed to single server or to 
     * all of them.
     */
    if(QUERY_IS_TYPE(qtype, QUERY_TYPE_SHOW_DATABASES))
    {
        /**
         * Generate custom response that contains all the databases 
         */
       
        GWBUF* dbres = gen_show_dbs_response(inst,router_cli_ses);
        poll_add_epollin_event_to_dcb(router_cli_ses->replydcb,dbres);
        ret = 1;
        goto retblock;
    }

    route_target = get_shard_route_target(qtype,
                                          router_cli_ses->rses_transaction_active,
                                          querybuf->hint);

    if(packet_type == MYSQL_COM_INIT_DB)
    {
        tname = hashtable_fetch(router_cli_ses->dbhash, router_cli_ses->rses_mysql_session->db);
        route_target = TARGET_NAMED_SERVER;
        
    }
    else if(route_target != TARGET_ALL &&
            (tname = get_shard_target_name(inst, router_cli_ses, querybuf, qtype)) != NULL)
    {

        route_target = TARGET_NAMED_SERVER;
    }

    if(TARGET_IS_UNDEFINED(route_target))
    {
        /**
         * No valid targets found for this query, return an error packet and update the hashtable.
         */

        tname = get_shard_target_name(inst, router_cli_ses, querybuf, qtype);

        if((tname == NULL &&
            packet_type != MYSQL_COM_INIT_DB &&
            router_cli_ses->rses_mysql_session->db[0] == '\0') ||
           packet_type == MYSQL_COM_FIELD_LIST ||
           (router_cli_ses->rses_mysql_session->db[0] != '\0'))
        {
            /**
             * No current database and no databases in query or
             * the database is ignored, route to first available backend.
             */
            
            route_target = TARGET_ANY;

        }
        else
        {
            if(!change_successful)
            {
                /**
                 * Bad shard status. The changing of the database 
                 * was not successful and the error message was already sent.
                 */

                ret = 1;
            }
            goto retblock;
        }

    }

    if(TARGET_IS_ALL(route_target))
    {
        /**
         * It is not sure if the session command in question requires
         * response. Statement is examined in route_session_write.
         * Router locking is done inside the function.
         */
        succp = route_session_write(router_cli_ses,
                                    gwbuf_clone(querybuf),
                                    inst,
                                    packet_type,
                                    qtype);

        if(succp)
        {
            atomic_add(&inst->stats.n_all, 1);
            ret = 1;
        }
        goto retblock;
    }

    /** Lock router session */
    if(!rses_begin_locked_router_action(router_cli_ses))
    {
        MXS_INFO("Route query aborted! Routing session is closed <");
        ret = 0;
        goto retblock;
    }

    if(TARGET_IS_ANY(route_target))
    {
        int z;

        for(z = 0; z < router_cli_ses->n_subservice; z++)
        {
            if(router_cli_ses->subservice[z]->state & SUBSVC_OK)
            {
                tname = router_cli_ses->subservice[z]->service->name;
                route_target = TARGET_NAMED_SERVER;
                break;
            }
        }

        if(TARGET_IS_ANY(route_target))
        {

            /**No valid backends alive*/
            rses_end_locked_router_action(router_cli_ses);
            ret = 0;
            goto retblock;
        }

    }

    /**
     * Query is routed to one of the backends
     */
    if(TARGET_IS_NAMED_SERVER(route_target))
    {
        /**
         * Search backend server by name or replication lag. 
         * If it fails, then try to find valid slave or master.
         */

        succp = get_shard_subsvc(&target_subsvc,router_cli_ses,tname);
       
        if(!succp)
        {
            MXS_INFO("Was supposed to route to named server "
                     "%s but couldn't find the server in a "
                     "suitable state.",
                     tname);
        }
    }

    if(succp) /*< Have SUBSERVICE of the target service */
    {
        sescmd_cursor_t* scur;
        scur = target_subsvc->scur;
        /** 
         * Store current stmt if execution of previous session command 
         * haven't completed yet. Note that according to MySQL protocol
         * there can only be one such non-sescmd stmt at the time.
         */
        if(scur && sescmd_cursor_is_active(scur)) 
        {
            target_subsvc->pending_cmd = gwbuf_clone(querybuf);
            rses_end_locked_router_action(router_cli_ses);
            ret = 1;
            goto retblock;
        }

        if(SESSION_ROUTE_QUERY(target_subsvc->session,querybuf) == 1)
        {
            

            atomic_add(&inst->stats.n_queries, 1);
            /**
             * Add one query response waiter to backend reference
             */
            subsvc_set_state(target_subsvc,SUBSVC_QUERY_ACTIVE|SUBSVC_WAITING_RESULT);
            
            atomic_add(&target_subsvc->n_res_waiting, 1);
            
        }
        else
        {
            MXS_ERROR("Routing query failed.");
            ret = 0;
        }
    }
    else
    {
        ret = 0;
    }
    rses_end_locked_router_action(router_cli_ses);
retblock:


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
static bool
rses_begin_locked_router_action(
                                ROUTER_CLIENT_SES* rses)
{
    bool succp = false;

    CHK_CLIENT_RSES(rses);

    if(rses->rses_closed)
    {

        goto return_succp;
    }
    spinlock_acquire(&rses->rses_lock);
    if(rses->rses_closed)
    {
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
static void
rses_end_locked_router_action(
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
static void
diagnostic(ROUTER *instance, DCB *dcb)
{
    ROUTER_CLIENT_SES *router_cli_ses;
    ROUTER_INSTANCE *router = (ROUTER_INSTANCE *) instance;
    int i = 0;
    char *weightby;

    spinlock_acquire(&router->lock);
    router_cli_ses = router->connections;
    while(router_cli_ses)
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
    if((weightby = serviceGetWeightingParameter(router->service)) != NULL)
    {
        dcb_printf(dcb,
                   "\tConnection distribution based on %s "
                   "server parameter.\n", weightby);
        dcb_printf(dcb,
                   "\t\tServer               Target %%    Connections  "
                   "Operations\n");
        dcb_printf(dcb,
                   "\t\t                               Global  Router\n");
        

    }

}

/**
 * Client Reply routine    TODO: This is redundant now with filterReply in place
 *
 * The routine will reply to client for session change with master server data
 *
 * @param	instance	The router instance
 * @param	router_session	The router session 
 * @param	backend_dcb	The backend DCB
 * @param	queue		The GWBUF with reply data
 */
static void
clientReply(
            ROUTER* instance,
            void* router_session,
            GWBUF* writebuf,
            DCB* backend_dcb)
{
    
    SESSION_ROUTE_REPLY(backend_dcb->session, writebuf);
    return;
}

/** 
 * Create a generic router session property strcture.
 */
static rses_property_t*
rses_property_init(
                   rses_property_type_t prop_type)
{
    rses_property_t* prop;

    prop = (rses_property_t*) calloc(1, sizeof(rses_property_t));
    if(prop == NULL)
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
static void
rses_property_done(
                   rses_property_t* prop)
{
    CHK_RSES_PROP(prop);

    switch(prop->rses_prop_type)
    {
    case RSES_PROP_TYPE_SESCMD:
        mysql_sescmd_done(&prop->rses_prop_data.sescmd);
        break;

    case RSES_PROP_TYPE_TMPTABLES:
        hashtable_free(prop->rses_prop_data.temp_tables);
        break;

    default:
        MXS_DEBUG("%lu [rses_property_done] Unknown property type %d "
                  "in property %p",
                  pthread_self(),
                  prop->rses_prop_type,
                  prop);

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
static void
rses_property_add(
                  ROUTER_CLIENT_SES* rses,
                  rses_property_t* prop)
{
    rses_property_t* p;

    CHK_CLIENT_RSES(rses);
    CHK_RSES_PROP(prop);
    ss_dassert(SPINLOCK_IS_LOCKED(&rses->rses_lock));

    prop->rses_prop_rsession = rses;
    p = rses->rses_properties[prop->rses_prop_type];

    if(p == NULL)
    {
        rses->rses_properties[prop->rses_prop_type] = prop;
    }
    else
    {
        while(p->rses_prop_next != NULL)
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
static mysql_sescmd_t*
rses_property_get_sescmd(
                         rses_property_t* prop)
{
    mysql_sescmd_t* sescmd;

    CHK_RSES_PROP(prop);
    ss_dassert(prop->rses_prop_rsession == NULL ||
               SPINLOCK_IS_LOCKED(&prop->rses_prop_rsession->rses_lock));

    sescmd = &prop->rses_prop_data.sescmd;

    if(sescmd != NULL)
    {
        CHK_MYSQL_SESCMD(sescmd);
    }
    return sescmd;
}

/**
 * Create session command property.
 */
static mysql_sescmd_t*
mysql_sescmd_init(
                  rses_property_t* rses_prop,
                  GWBUF* sescmd_buf,
                  unsigned char packet_type,
                  ROUTER_CLIENT_SES* rses)
{
    mysql_sescmd_t* sescmd;

    CHK_RSES_PROP(rses_prop);
    /** Can't call rses_property_get_sescmd with uninitialized sescmd */
    sescmd = &rses_prop->rses_prop_data.sescmd;
    sescmd->my_sescmd_prop = rses_prop; /*< reference to owning property */
#if defined(SS_DEBUG)
    sescmd->my_sescmd_chk_top = CHK_NUM_MY_SESCMD;
    sescmd->my_sescmd_chk_tail = CHK_NUM_MY_SESCMD;
#endif
    /** Set session command buffer */
    sescmd->my_sescmd_buf = sescmd_buf;
    sescmd->my_sescmd_packet_type = packet_type;

    return sescmd;
}

static void
mysql_sescmd_done(
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
static GWBUF*
sescmd_cursor_process_replies(
                              GWBUF* replybuf,
                              SUBSERVICE* subsvc)
{
    mysql_sescmd_t* scmd;
    sescmd_cursor_t* scur;

    scur = subsvc->scur;
    ss_dassert(SPINLOCK_IS_LOCKED(&(scur->scmd_cur_rses->rses_lock)));
    scmd = sescmd_cursor_get_command(scur);

    CHK_GWBUF(replybuf);

    /** 
     * Walk through packets in the message and the list of session 
     * commands. 
     */
    while(scmd != NULL && replybuf != NULL)
    {
        /** Faster backend has already responded to client : discard */
        if(scmd->my_sescmd_is_replied)
        {
            bool last_packet = false;

            CHK_GWBUF(replybuf);

            while(!last_packet)
            {
                int buflen;

                buflen = GWBUF_LENGTH(replybuf);
                last_packet = GWBUF_IS_TYPE_RESPONSE_END(replybuf);
                /** discard packet */
                replybuf = gwbuf_consume(replybuf, buflen);
            }
            /** Set response status received */
            
            subsvc_clear_state(subsvc, SUBSVC_WAITING_RESULT);
        }
            /** Response is in the buffer and it will be sent to client. */
        else if(replybuf != NULL)
        {
            /** Mark the rest session commands as replied */
            scmd->my_sescmd_is_replied = true;
        }

        if(sescmd_cursor_next(scur))
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
static mysql_sescmd_t*
sescmd_cursor_get_command(
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
static bool
sescmd_cursor_is_active(
                        sescmd_cursor_t* sescmd_cursor)
{
    bool succp;
    ss_dassert(SPINLOCK_IS_LOCKED(&sescmd_cursor->scmd_cur_rses->rses_lock));

    succp = sescmd_cursor->scmd_cur_active;
    return succp;
}

/** router must be locked */
static void
sescmd_cursor_set_active(
                         sescmd_cursor_t* sescmd_cursor,
                         bool value)
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
static GWBUF*
sescmd_cursor_clone_querybuf(
                             sescmd_cursor_t* scur)
{
    GWBUF* buf;
    ss_dassert(scur->scmd_cur_cmd != NULL);

    buf = gwbuf_clone(scur->scmd_cur_cmd->my_sescmd_buf);

    CHK_GWBUF(buf);
    return buf;
}

static bool
sescmd_cursor_history_empty(
                            sescmd_cursor_t* scur)
{
    bool succp;

    CHK_SESCMD_CUR(scur);

    if(scur->scmd_cur_rses->rses_properties[RSES_PROP_TYPE_SESCMD] == NULL)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }

    return succp;
}

static void
sescmd_cursor_reset(
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

static bool
execute_sescmd_history(
                       SUBSERVICE* subsvc)
{
    bool succp;
    sescmd_cursor_t* scur;

    scur = subsvc->scur;
    CHK_SESCMD_CUR(scur);

    if(!sescmd_cursor_history_empty(scur))
    {
        sescmd_cursor_reset(scur);
        succp = execute_sescmd_in_backend(subsvc);
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
static bool
execute_sescmd_in_backend(SUBSERVICE* subsvc)
{
    bool succp;
    int rc = 0;
    sescmd_cursor_t* scur;
    

    if(SUBSVC_IS_CLOSED(subsvc) || !SUBSVC_IS_OK(subsvc))
    {
        succp = false;
        goto return_succp;
    }
    
    if(!subsvc_is_valid(subsvc))
    {
        succp = false;
        goto return_succp;
    }
    
    /** 
     * Get cursor pointer and copy of command buffer to cursor.
     */
    scur = subsvc->scur;

    /** Return if there are no pending ses commands */
    if(sescmd_cursor_get_command(scur) == NULL)
    {
        succp = false;
        MXS_INFO("Cursor had no pending session commands.");

        goto return_succp;
    }

    if(!sescmd_cursor_is_active(scur))
    {
        /** Cursor is left active when function returns. */
        sescmd_cursor_set_active(scur, true);
    }

    switch(scur->scmd_cur_cmd->my_sescmd_packet_type)
    {
    case MYSQL_COM_CHANGE_USER:
        /** This makes it possible to handle replies correctly */
        gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
        rc = SESSION_ROUTE_QUERY(subsvc->session,sescmd_cursor_clone_querybuf(scur));
        break;

    case MYSQL_COM_QUERY:
    default:
        /** 
         * Mark session command buffer, it triggers writing 
         * MySQL command to protocol
         */
        gwbuf_set_type(scur->scmd_cur_cmd->my_sescmd_buf, GWBUF_TYPE_SESCMD);
        rc = SESSION_ROUTE_QUERY(subsvc->session,sescmd_cursor_clone_querybuf(scur));
        break;
    }

    if(rc == 1)
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
static bool
sescmd_cursor_next(
                   sescmd_cursor_t* scur)
{
    bool succp = false;
    rses_property_t* prop_curr;
    rses_property_t* prop_next;

    ss_dassert(scur != NULL);
    ss_dassert(*(scur->scmd_cur_ptr_property) != NULL);
    ss_dassert(SPINLOCK_IS_LOCKED(
                                  &(*(scur->scmd_cur_ptr_property))->rses_prop_rsession->rses_lock));

    /** Illegal situation */
    if(scur == NULL ||
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
    if(prop_next != NULL)
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

    if(scur->scmd_cur_cmd != NULL)
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

static rses_property_t*
mysql_sescmd_get_property(
                          mysql_sescmd_t* scmd)
{
    CHK_MYSQL_SESCMD(scmd);
    return scmd->my_sescmd_prop;
}

/**
 * Return RCAP_TYPE_STMT_INPUT
 */
static int
getCapabilities()
{
    return RCAP_TYPE_STMT_INPUT;
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
static bool
route_session_write(
                    ROUTER_CLIENT_SES* router_cli_ses,
                    GWBUF* querybuf,
                    ROUTER_INSTANCE* inst,
                    unsigned char packet_type,
                    skygw_query_type_t qtype)
{
    bool succp;
    rses_property_t* prop;
    SUBSERVICE* subsvc;
    int i;

    MXS_INFO("Session write, routing to all servers.");

    /**
     * These are one-way messages and server doesn't respond to them.
     * Therefore reply processing is unnecessary and session 
     * command property is not needed. It is just routed to all available
     * backends.
     */
    if(packet_type == MYSQL_COM_STMT_SEND_LONG_DATA ||
       packet_type == MYSQL_COM_QUIT ||
       packet_type == MYSQL_COM_STMT_CLOSE)
    {
        int rc;

        succp = true;

        /** Lock router session */
        if(!rses_begin_locked_router_action(router_cli_ses))
        {
            succp = false;
            goto return_succp;
        }

        for(i = 0; i < router_cli_ses->n_subservice; i++)
        {
            subsvc = router_cli_ses->subservice[i];

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s%s%s",
                         i == 0 ? ">":"",
                         subsvc->service->name,
                         i+1 >= router_cli_ses->n_subservice ? "<" : "");
            }

            if(!SUBSVC_IS_CLOSED(subsvc) && SUBSVC_IS_OK(subsvc))
            {
                rc = SESSION_ROUTE_QUERY(subsvc->session,gwbuf_clone(querybuf));

                if(rc != 1)
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
    if(!rses_begin_locked_router_action(router_cli_ses))
    {
        succp = false;
        goto return_succp;
    }

    if(router_cli_ses->n_subservice <= 0)
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

    for(i = 0; i < router_cli_ses->n_subservice; i++)
    {
        subsvc = router_cli_ses->subservice[i];
        
        if(!SUBSVC_IS_CLOSED(subsvc))
        {
            sescmd_cursor_t* scur;

            if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
            {
                MXS_INFO("Route query to %s%s%s",
                         i == 0 ? ">":"",
                         subsvc->service->name,
                         i+1 >= router_cli_ses->n_subservice ? "<" : "");
            }

            

            
           
            scur = subsvc->scur;
            
            /** 
             * Add one waiter to backend reference.
             */
            subsvc_set_state(subsvc,SUBSVC_WAITING_RESULT);
            
            /** 
             * Start execution if cursor is not already executing.
             * Otherwise, cursor will execute pending commands
             * when it completes with previous commands.
             */
            if(sescmd_cursor_is_active(scur))
            {
                succp = true;

                MXS_INFO("Service %s already executing sescmd.",
                         subsvc->service->name);
            }
            else
            {
                succp = execute_sescmd_in_backend(subsvc);

                if(!succp)
                {
                    MXS_ERROR("Failed to execute session "
                              "command in %s",
                              subsvc->service->name);
                }
            }
        }
        else
        {
            succp = false;
        }
    }
    /** Unlock router session */
    rses_end_locked_router_action(router_cli_ses);

return_succp:
    return succp;
}

/**
 * Error Handler routine to resolve _backend_ failures. If it succeeds then there
 * are enough operative backends available and connected. Otherwise it fails, 
 * and session is terminated.
 *
 * @param       instance        The router instance
 * @param       router_session  The router session
 * @param       errmsgbuf       The error message to reply
 * @param       backend_dcb     The backend DCB
 * @param       action     	The action: ERRACT_NEW_CONNECTION or ERRACT_REPLY_CLIENT
 * @param	succp		Result of action: true if router can continue
 * 
 * Even if succp == true connecting to new slave may have failed. succp is to
 * tell whether router has enough master/slave connections to continue work.
 */
static void
handleError(
            ROUTER* instance,
            void* router_session,
            GWBUF* errmsgbuf,
            DCB* backend_dcb,
            error_action_t action,
            bool* succp)
{
    SESSION* session;
    ROUTER_CLIENT_SES* rses = (ROUTER_CLIENT_SES *) router_session;

    CHK_DCB(backend_dcb);
    
    /** Don't handle same error twice on same DCB */
    if(backend_dcb->dcb_errhandle_called)
    {
        /** we optimistically assume that previous call succeed */        
        *succp = true;
        return;
    }
    else
    {
        backend_dcb->dcb_errhandle_called = true;
    }
    session = backend_dcb->session;

    if(session == NULL || rses == NULL)
    {
        *succp = false;
    }
    else
    {
        CHK_SESSION(session);
        CHK_CLIENT_RSES(rses);

        switch(action)
        {
            case ERRACT_NEW_CONNECTION:
            {
                if(!rses_begin_locked_router_action(rses))
                {
                    *succp = false;
                    break;
                }

                rses_end_locked_router_action(rses);
                break;
            }

            case ERRACT_REPLY_CLIENT:
            {
                *succp = false; /*< no new backend servers were made available */
                break;
            }

            default:
                *succp = false;
                break;
        }
    }
    dcb_close(backend_dcb);
}



/**
 * Finds the subservice who owns this session.
 * @param rses Router client session
 * @param ses The session to look for
 * @return Pointer to SUBSESSION who owns the session
 */
static SUBSERVICE* get_subsvc_from_ses(ROUTER_CLIENT_SES* rses, SESSION* ses)
{
    int i;
    for(i = 0; i < rses->n_subservice; i++)
    {
        if(rses->subservice[i]->session == ses)
        {
            return rses->subservice[i];
        }
    }
    
    return NULL;
}


/**
 * Calls hang-up function for DCB if it is not both running and in 
 * master/slave/joined/ndb role. Called by DCB's callback routine.
 *
 * TODO: See if there's a way to inject this into the subservices
 */
static int
router_handle_state_switch(
                           DCB* dcb,
                           DCB_REASON reason,
                           void* data)
{
    SUBSERVICE* subsvc;
    int rc = 1;
    ROUTER_CLIENT_SES* rses;
    SESSION* ses;
    SERVER* srv;

    CHK_DCB(dcb);

    return rc;
#if 0
    if(SERVER_IS_RUNNING(srv) && SERVER_IS_IN_CLUSTER(srv))
    {
        goto return_rc;
    }
    ses = dcb->session;
    CHK_SESSION(ses);

    rses = (ROUTER_CLIENT_SES *) dcb->session->router_session;
    CHK_CLIENT_RSES(rses);

    switch(reason)
    {
    case DCB_REASON_NOT_RESPONDING:
        dcb->func.hangup(dcb);
        break;

    default:
        break;
    }

return_rc:
    return rc;
#endif
}
