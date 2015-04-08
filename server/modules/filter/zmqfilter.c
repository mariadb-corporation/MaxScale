/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
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
 * The zmqfilter is part of MaxWeb a monitoring and administration tool for MaxScale. 
 * Visit our site http://www.maxweb.io for more information
 * 
 * Copyright Proxylab 2014 Thessaloniki-Greece http://www.proxylab.io
 */

/**
 * @file zmqfilter.c
 * 
 * ZeroMQ Filter
 * 
 * The current filter exports and sends information about executed queries to a zeroMQ
 * consumer which in turn saves the data into an MariaDB/MySql database instance. Communication 
 * between filter and consumer relies on the push/pull pipeline of zeroMQ.
 * 
 * Attention: before using zmqfilter please be sure that you have successfully installed zeromq (http://zeromq.org/intro:get-the-software) 
 * and czmq (http://czmq.zeromq.org/) libraries in your system. 
 * 
 * ZMQFilter extracts the following data from each query then sends it through the network:
 *      - server_id             The server identifier
 *      - duration              The query duration
 *      - request_time          The time that query started executing
 *      - response_time         The time that query execution completed
 *      - statemen_type         The type of the current statement
 *      - isReal_query          True if current query is one of INSERT, UPDATE, DELETE, SELECT
 *      - sql_query             Current query along with its parameters
 *      - canonical_sql         Current query without the parameters
 *      - client_name           Name(or IP) of the current client 
 *      - server_name           The name(or IP) of the server in which query was executed
 *      - server_unique_name    The unique server name in which query was executed   
 *      - affected_tables       One or more tables that the current query is related with
 *      - query_failed          True if current query failed to execute
 *      - query_error           A string containing the error for the current query
 *
 *@verbatim
 * The options for this filter are:
 *
 *      - source                The source of the client connection 
 *      - includedServers       When this is not empty filter should accept queries only from this server list e.g: master,slave1
 *      - includedTables        When this is not empty filter should accept queries only from this table list
 *      - user                  A user name to filter on
 *      - match                 Optional text to match against
 *      - exclude               Optional text to match against for exclusion
 *      - saveRealOnly          Save only real queries
 *      - zmqHost               Host for ZMQ socket
 *      - zmqPort               Port for ZMQ port
 *
 *@endverbatim
 * 
 */

#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <regex.h>
#include <query_classifier.h>
#include "czmq.h"

extern int lm_enabled_logfiles_bitmask;

#define MAX_SEND_RETRIES 3
#define MAX_ZMQ_SENDHWM 100000
#define MAX_ZMQ_SNDTIMEO 5
#define NETWORK_SIGNAL "\000"
#define SERVER_OK "\002"

#define STR_LEN(s) (s ? strlen(s) : 0)
#define LONG_SZ (sizeof(long))
#define CHAR_SZ (sizeof(char))
#define INT_SZ  (sizeof(int))
        
MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_IN_DEVELOPMENT,
	FILTER_VERSION,
	"A filter for sending query details by using zmq."
};

static char *version_str = "V1.0.1";
/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	void	setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	int	clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);

static FILTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    setUpstream,
    routeQuery,
    clientReply,
    diagnostic,
};

typedef enum canonical_cmd{
    SELECT = 1,
    INSERT,
    INSERT_SELECT,        
    UPDATE,
    REPLACE,
    REPLACE_SELECT,
    DELETE,
    TRUNCATE,
    PREPARE,
    EXECUTE,        
    OTHER        
}canonical_cmd_t;
/**
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
typedef struct {
	int	sessions;               /* Session count */
	char	*source;                /* The source of the client connection */
        char    *includedServers;       /* When this is not empty filter should accept queries only from this server list e.g:master,slave1 */
        char    *includedTables;        /* When this is not empty filter should accept queries only from this table list */
	char	*user;                  /* A user name to filter on */
	char	*match;                 /* Optional text to match against */
	regex_t	re;                     /* Compiled regex text */
	char	*exclude;               /* Optional text to match against for exclusion */
	regex_t	exre;                   /* Compiled regex nomatch text */
        
        bool    saveRealOnly;           /* Save only real queries */
        char    *zmqHost;               /* Host for ZMQ socket */   
        int     zmqPort;                /* Port for ZMQ port */    
        char    endpoint[32];           /* ZMQ connection address */
} ZMQ_INSTANCE;

/**
 * Structure that holds the information we need to transfer to the zmq consumer.
 */
typedef struct {
    long                            serverId;              
    struct timeval                  duration;	  
    struct timeval                  requestTime;
    struct timeval                  responseTime;
    skygw_query_type_t              statementType;
    canonical_cmd_t                 canonCmdType;
    bool                            isRealQuery;
    int                             canonicalSqlId;

    char                            *sqlQuery;
    char                            *canonicalSql;             
    char                            *transactionId; 
    char                            *clientName;
    char                            *serverName;
    char                            *serverUniqueName;
    char                            *affectedTables;
    
    bool                            queryFailed;
    char                            *queryError;
} ZMQ_INFO;

/**
 * The session structure for this ZMQ filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct {
    DOWNSTREAM          down;
    UPSTREAM            up;
    int                 active;

    char		*userName;
    char                *clientHost;        
    ZMQ_INFO            *current;

    struct timeval	start;                  /* Time that session started */
    struct timeval	connect;                /* When session initialized */
    struct timeval	disconnect;             /* When session terminated */
    zsock_t             *socket;                /* ZMQ client requester */    
    int                 n_statements;           /* Executed statements for this session */
} ZMQ_SESSION;


char** str_split(char *a_str, const char a_delim);
char* str_join(char **args, const char *sep, int len);
static int strCharCount(const char *str, const char c);

static bool initZmqConnection(ZMQ_SESSION *session, ZMQ_INSTANCE *instance);
static void sendZmqRequest (zmsg_t *request, ZMQ_SESSION *instance);
static void freeInstance(ZMQ_INSTANCE **instance);
static void freeInfo(ZMQ_INFO **info);
static zmsg_t * infoToZmqMessage(const ZMQ_INFO *data);
static long bytesToLong(const unsigned char *data, int start, size_t size);
static char* longToBytes(long num, const size_t sz);
static bool invalid_char (int c);
static void strip(char * str);

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
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
FILTER_OBJECT *
GetModuleObject()
{
    return &MyObject;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 * 
 * @param options	The options for this filter
 * @param params	The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options, FILTER_PARAMETER **params)
{
    ZMQ_INSTANCE *my_instance;

    if ((my_instance = calloc(1, sizeof(ZMQ_INSTANCE))) != NULL)
    {
        my_instance->match = NULL;
        my_instance->exclude = NULL;
        my_instance->source = NULL;
        my_instance->user = NULL;
        my_instance->saveRealOnly = false;

        my_instance->zmqHost = "127.0.0.1";
        my_instance->zmqPort = 0;
        
        int i=0;
        for (i = 0; params && params[i]; i++)
        {
            if (!strcmp(params[i]->name, "source"))
                my_instance->source = strdup(params[i]->value);
            else if (!strcmp(params[i]->name, "included_servers"))
            {
                my_instance->includedServers = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "included_tables"))
            {
                my_instance->includedTables = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "user"))
            {
                my_instance->user = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "exclude"))
            {
                my_instance->exclude = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "match"))
            {
                my_instance->match = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "zmqhost"))
            {
                my_instance->zmqHost = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "zmqport"))
            {
                my_instance->zmqPort = atoi(params[i]->value);
            }
            else if(!strcmp(params[i]->name, "save_real_only"))
            {
                if (!strcmp(params[i]->value, "yes"))
                    my_instance->saveRealOnly = true;
                else
                    my_instance->saveRealOnly = false;
            }
            else if (!filter_standard_parameter(params[i]->name))
            {
                skygw_log_write(LOGFILE_ERROR, "zmqfilter: Unexpected parameter '%s'", params[i]->name);
            }
        }      
        if (options)
        {
            skygw_log_write(LOGFILE_TRACE, "zmqfilter: Options are not supported by this filter. They will be ignored");
        }
        my_instance->sessions = 0;
        if (my_instance->match &&
                regcomp(&my_instance->re, my_instance->match, REG_EXTENDED + REG_ICASE))
        {
            skygw_log_write(LOGFILE_ERROR, "zmqfilter: Invalid regular expression '%s' for the match parameter.", my_instance->match);
            freeInstance(&my_instance);
            return NULL;
        }
        if (my_instance->exclude &&
                regcomp(&my_instance->exre, my_instance->exclude, REG_EXTENDED + REG_ICASE))
        {
            skygw_log_write(LOGFILE_ERROR, "zmqfilter: Invalid regular expression '%s' for the nomatch paramter.", my_instance->match);
            freeInstance(&my_instance);
            return NULL;
        }
        
        if(my_instance->zmqPort <= 0)
        {
            skygw_log_write(LOGFILE_ERROR, "zmqfilter: Invalid zeromq port[%d]" , my_instance->zmqPort);
            freeInstance(&my_instance);
            return NULL;
        }

        skygw_log_write(LOGFILE_TRACE, "zmqfilter instance created.");
    }

    return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Create the file to log to and open it.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{  
    ZMQ_INSTANCE    *my_instance = (ZMQ_INSTANCE *)instance;
    ZMQ_SESSION     *my_session = (ZMQ_SESSION*)session;
    char            *user;
        
    if ((my_session = calloc(1, sizeof(ZMQ_SESSION))) != NULL)
    {             
        my_session->current = NULL;

        if(session && session->client)
            my_session->clientHost = strdup(session_get_remote(session));

        if ((user = session_getUser(session)) != NULL)
                my_session->userName = strdup(user);
        else
                my_session->userName = NULL;

        my_session->active = 1;

        if (my_instance->source && strcmp(my_session->clientHost, my_instance->source)){
                my_session->active = 0;
                skygw_log_write(LOGFILE_TRACE, "zmqfilter: Session inactive. Reason: hostname filter.");
        }

        if (my_instance->user && strcmp(my_session->userName, my_instance->user)){
                my_session->active = 0;
                skygw_log_write(LOGFILE_TRACE, "zmqfilter: Session inactive. Reason: user filter.");
        }

        gettimeofday(&my_session->connect, NULL);   
        

        if(initZmqConnection(my_session, my_instance) == false)
            my_session->active = 0;     
        
        if(my_session->active){
            my_instance->sessions++;
            skygw_log_write(LOGFILE_TRACE, "zmqfilter: Session created.");
        }
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the TOPN filter we simple close the file descriptor.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
    
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
    ZMQ_SESSION	*my_session = (ZMQ_SESSION *)session;
    
    zsock_destroy(&my_session->socket);  
    free(my_session->clientHost);
    free(my_session->userName);
    freeInfo(&my_session->current);
    free(session);
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    ZMQ_SESSION	*my_session = (ZMQ_SESSION *)session;

    my_session->down = *downstream;
}

/**
 * Set the upstream filter or session to which results will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param upstream	The upstream filter or session.
 */
static void
setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
    ZMQ_SESSION *my_session = (ZMQ_SESSION *)session;

    my_session->up = *upstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    ZMQ_INSTANCE    *my_instance = (ZMQ_INSTANCE *)instance;
    ZMQ_SESSION     *my_session = (ZMQ_SESSION *)session;
    char            *ptr = NULL;
    int             length;
        
    if (my_session->active && modutil_extract_SQL(queue, &ptr, &length))
    {       
        skygw_log_write(LOGFILE_DEBUG, "zmqfilter: Query received");
        freeInfo(&my_session->current);

        if ((my_instance->match == NULL || regexec(&my_instance->re, ptr, 0, NULL, 0) == 0) &&
                (my_instance->exclude == NULL || regexec(&my_instance->exre,ptr,0,NULL, 0) != 0))
        {

            if((my_session->current = (ZMQ_INFO*) malloc(sizeof(ZMQ_INFO))) == NULL){
                my_session->active = 0;
                skygw_log_write(LOGFILE_ERROR, "zmqfilter: Memory allocation failed for: %s" , "route_query");                            
            }
            else
            {
                my_session->n_statements++;
                my_session->current->clientName = strdup(my_session->clientHost);

                my_session->current->sqlQuery = NULL;
                my_session->current->canonicalSql = NULL;
                my_session->current->queryError = NULL;
                my_session->current->transactionId = NULL;
                my_session->current->serverId = 0;
                my_session->current->serverName = NULL;
                my_session->current->serverUniqueName = NULL;
                my_session->current->affectedTables = NULL;
                my_session->current->isRealQuery = false;
                my_session->current->statementType = QUERY_TYPE_UNKNOWN;

                gettimeofday(&my_session->current->requestTime, NULL);
                my_session->current->sqlQuery = strndup(ptr, length);        

                int i, tbl_count = 0;
                char **tables;
                if (!query_is_parsed(queue)){
                    if(parse_query(queue)){
                        skygw_log_write(LOGFILE_DEBUG, "zmqfilter: Query parsed.");

                        my_session->current->isRealQuery = skygw_is_real_query(queue);
                        my_session->current->statementType = query_classifier_get_type(queue);

                        if(my_session->current->isRealQuery){
                            skygw_log_write(LOGFILE_DEBUG, "zmqfilter: Current is real query.");            
                            
                            //get query tables 
                            tables = skygw_get_table_names(queue, &tbl_count, false);
                            my_session->current->canonicalSql = skygw_get_canonical(queue);                            
                            
                            if(tbl_count > 0)
                                my_session->current->affectedTables = str_join(tables, ",", tbl_count);

                            char *real_query_t = skygw_get_realq_type_str(queue);
                            if(strcmp(real_query_t, "SELECT") == 0)
                                my_session->current->canonCmdType = SELECT;
                            else if(strcmp(real_query_t, "INSERT") == 0)
                                my_session->current->canonCmdType = INSERT;
                            else if(strcmp(real_query_t, "INSERT_SELECT") == 0)
                                my_session->current->canonCmdType = INSERT_SELECT;
                            else if(strcmp(real_query_t, "UPDATE") == 0)
                                my_session->current->canonCmdType = UPDATE;
                            else if(strcmp(real_query_t, "REPLACE") == 0)
                                my_session->current->canonCmdType = REPLACE;
                            else if(strcmp(real_query_t, "REPLACE_SELECT") == 0)
                                my_session->current->canonCmdType = REPLACE_SELECT;
                            else if(strcmp(real_query_t, "DELETE") == 0)
                                my_session->current->canonCmdType = DELETE;
                            else if(strcmp(real_query_t, "TRUNCATE") == 0)
                                my_session->current->canonCmdType = TRUNCATE;
                            else if(strcmp(real_query_t, "PREPARE") == 0)
                                my_session->current->canonCmdType = PREPARE;
                            else if(strcmp(real_query_t, "EXECUTE") == 0)
                                my_session->current->canonCmdType = EXECUTE;
                            else
                                my_session->current->canonCmdType = OTHER;
                            
                            free(real_query_t);
                        }
                    }                            
                }
                
                //save real queries only
                if(my_instance->saveRealOnly && !my_session->current->isRealQuery){
                    freeInfo(&my_session->current);
                    goto send_to_downstream;
                }

                //save only if query is related to one or more included tables
                if(my_instance->includedTables && tbl_count > 0){
                    skygw_log_write(LOGFILE_DEBUG, "zmqfilter: Analyzing included tables filter.");
                    char **cnf_tables;
                    int cnf_tables_cnt = strCharCount(my_instance->includedTables, ',');
                    bool found = false;

                    if(cnf_tables_cnt >= 1){//more than one tables to check
                        cnf_tables_cnt++;

                        char *tmp = strdup(my_instance->includedTables);
                        cnf_tables = str_split(tmp, ',');
                        free(tmp);

                        int j;
                        for(i = 0; i < tbl_count; i++){
                            for(j = 0; j < cnf_tables_cnt; j++)
                                if(strcmp(cnf_tables[j], tables[i]) == 0){
                                    found = true;
                                    break;
                                }

                            if(found) break;
                        }

                    } else{//one table to check
                        for(i = 0; i < tbl_count; i++){
                            if(strcmp(my_instance->includedTables, tables[i]) == 0){
                                found = true;
                                break;
                            }
                        }
                    }

                    for(i=0; i<cnf_tables_cnt; i++)
                        free(cnf_tables[i]);
                    free(cnf_tables);

                    if(!found) 
                        freeInfo(&my_session->current);
                }

                if(tbl_count > 0){

                    for(i=0; i<tbl_count; i++){
                        free(tables[i]);
                    }
                    free(tables);
                }

                //TODO: transactionId  
            }
        }
    }

send_to_downstream:
	/* Pass the query downstream */
	return my_session->down.routeQuery(my_session->down.instance, my_session->down.session, queue);
    
}

static int
clientReply(FILTER *instance, void *session, GWBUF *reply)
{
    ZMQ_SESSION     *my_session = (ZMQ_SESSION *)session;
    ZMQ_INSTANCE    *my_instance = (ZMQ_INSTANCE *)instance;
    struct timeval  diff;
    int i;

    if (my_session->current)
    {       
        gettimeofday(&my_session->current->responseTime, NULL);
        timersub(&(my_session->current->responseTime), &(my_session->current->requestTime), &diff);

        my_session->current->duration.tv_sec = diff.tv_sec;
        my_session->current->duration.tv_usec = diff.tv_usec;

        char *srv = gwbuf_get_property(reply, "SERVER_NAME");
        char *srv_id = gwbuf_get_property(reply, "SERVER_ID");
        char *srv_uniq = gwbuf_get_property(reply, "SERVER_UNIQUE_NAME");

        if(srv)
            my_session->current->serverName = strdup(srv);

        if(srv_id)
            my_session->current->serverId = atol(srv_id);

        if(srv_uniq)
            my_session->current->serverUniqueName = strdup(srv_uniq);

        //save only if query executed in one of the included servers
        if(my_instance->includedServers){
            skygw_log_write(LOGFILE_DEBUG, "zmqfilter: Analyzing included servers filter.");
            char **srvs;
            int cnt = strCharCount(my_instance->includedServers, ',');
            bool found = false;

            if(cnt >= 1){
                cnt++;
                char *tmp = strdup(my_instance->includedServers);
                srvs = str_split(tmp, ',');
                free(tmp);

                for(i = 0; i < cnt; i++){
                    if(strcmp(srvs[i], my_session->current->serverName) == 0 || strcmp(srvs[i], my_session->current->serverUniqueName)){
                        found = true;
                        break;
                    }
                }
            } else{
                if(strcmp(my_instance->includedServers, my_session->current->serverName) == 0 || 
                        strcmp(my_instance->includedServers, my_session->current->serverUniqueName) == 0)
                    found = true;
            }

            for(i=0; i<cnt; i++)
                free(srvs[i]);
            free(srvs);

            if(!found){
                freeInfo(&my_session->current);
                goto send_to_uptream;
            }
        }
        
        my_session->current->queryFailed = false;
        if(*(reply->sbuf->data + 4) == 0x00){ /**OK packet*/
            
        }
        else if(*(reply->sbuf->data + 4) == 0xff){ /**ERR packet*/

            my_session->current->queryFailed = true;
            my_session->current->queryError = calloc(512, CHAR_SZ);            
            strcpy(my_session->current->queryError, (char *)reply->sbuf->data + 13);
            
            //bug fix for non ASCII char returned by mysql when query fails...
            strip(my_session->current->queryError);
        }
        else{
            
        }

        zmsg_t *msg = infoToZmqMessage(my_session->current);

        sendZmqRequest(msg, my_session);
        
        freeInfo(&my_session->current);
    }

send_to_uptream:
    /* Pass the result upstream */
    return my_session->up.clientReply(my_session->up.instance, my_session->up.session, reply);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
ZMQ_INSTANCE	*my_instance = (ZMQ_INSTANCE *)instance;
ZMQ_SESSION	*my_session = (ZMQ_SESSION *)fsession;
 
	dcb_printf(dcb, "\t\tCurrent sessions size			%d\n", my_instance->sessions);
        dcb_printf(dcb, "\t\tSave real queries only			%s\n", my_instance->saveRealOnly ? "true" : "false");
        
	if (my_instance->source)
		dcb_printf(dcb, "\t\tLimit logging to connections from 	%s\n", my_instance->source);
        
	if (my_instance->user)
		dcb_printf(dcb, "\t\tLimit logging to user		%s\n", my_instance->user);
        
	if (my_instance->match)
		dcb_printf(dcb, "\t\tInclude queries that match		%s\n", my_instance->match);
        
	if (my_instance->exclude)
		dcb_printf(dcb, "\t\tExclude queries that match		%s\n", my_instance->exclude);
        
        if (my_instance->includedServers)
		dcb_printf(dcb, "\t\tInclude servers that match		%s\n", my_instance->includedServers);
        
        if (my_instance->includedTables)
		dcb_printf(dcb, "\t\tInclude tables that match		%s\n", my_instance->includedTables);
        
        if (my_instance->zmqHost)
		dcb_printf(dcb, "\t\tZMQ host		%s\n", my_instance->zmqHost);
        
        dcb_printf(dcb, "\t\tZMQ port		%d\n", my_instance->zmqPort);
	
        if (my_session)
	{
		dcb_printf(dcb, "\t\t\tSession is active to file %s.\n", my_session->active ? "true" : "false");
		dcb_printf(dcb, "\t\t\tSession username %s:\n", my_session->userName);
                dcb_printf(dcb, "\t\t\tSession client host %s:\n", my_session->clientHost);
                dcb_printf(dcb, "\t\t\tSession statements %d:\n", my_session->n_statements);

	}
}

/**
 * Initialize connection for zeromq
 * 
 * @return true if connect succeeded false otherwise
 */
static bool initZmqConnection(ZMQ_SESSION *session, ZMQ_INSTANCE *instance){   
    sprintf(instance->endpoint, "tcp://%s:%d", instance->zmqHost, instance->zmqPort);
    session->socket = zsock_new_push (instance->endpoint);
    zsock_set_sndhwm(session->socket, MAX_ZMQ_SENDHWM);
    zsock_set_sndtimeo(session->socket, MAX_ZMQ_SNDTIMEO);//wait MAX_ZMQ_SNDTIMEO before aborting
    
    if(session->socket == NULL){
        skygw_log_write(LOGFILE_ERROR, "zmqfilter: zmq create socket failed. Error[%s]", zmq_strerror(zmq_errno()));
        return false;
    }
    
    skygw_log_write(LOGFILE_TRACE, "zmqfilter: zmq connection succeeded.");
    
    return true;
}

/**
 * Pushes the request message to the zmq pipeline
 * 
 * @param request The zmq message to send through the pipeline
 * @param instance The current filter instance
 */
static void sendZmqRequest (zmsg_t *request, ZMQ_SESSION *session)
{   
    int retries = 1;

    //try MAX_RETRIES times to send the message
    do{
        zmsg_send (&request, session->socket);
    }while(request && retries++ < MAX_SEND_RETRIES);
    
    if(request){
        skygw_log_write_flush(LOGFILE_ERROR, "zmqfilter: zstr_send failed. Error[%s]\n", zmq_strerror(zmq_errno()));
        zmsg_destroy(&request);
    }
}


/**
 * Creates and return a new zeromq message object from the given
 * ZMQ_INFO object
 * 
 * @param data ZMQ_INFO instance to serialize
 * @return The new zeromq object 
 */
static zmsg_t * infoToZmqMessage(const ZMQ_INFO *data){
    size_t sqlSize = STR_LEN(data->sqlQuery);
    size_t canonicalSqlSize = STR_LEN(data->canonicalSql);
    size_t transactionIdSize = STR_LEN(data->transactionId);
    size_t clientNameSize = STR_LEN(data->clientName);
    size_t serverNameSize = STR_LEN(data->serverName);
    size_t serverUniqueNameSize = STR_LEN(data->serverUniqueName);  
    size_t affectedTablesSize = STR_LEN(data->affectedTables);
    size_t queryErrorSize = STR_LEN(data->queryError);
    
    zmsg_t *serialized = zmsg_new ();
    void *buffer = longToBytes(data->serverId, LONG_SZ);
    
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);
    
    buffer = longToBytes(data->duration.tv_sec, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);
    
    buffer = longToBytes(data->duration.tv_usec,LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);    
    free(buffer);
    
    buffer = longToBytes(data->requestTime.tv_sec, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);
    
    buffer =  longToBytes(data->requestTime.tv_usec, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);    
    free(buffer);
    
    buffer = longToBytes(data->responseTime.tv_sec, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);
    
    buffer = longToBytes(data->responseTime.tv_usec, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);
    
    buffer = longToBytes((long)data->statementType, LONG_SZ);
    zmsg_addmem(serialized, buffer, sizeof(skygw_query_type_t));
    free(buffer);
    
    buffer = longToBytes((long)data->canonCmdType, LONG_SZ);
    zmsg_addmem(serialized, buffer, sizeof(canonical_cmd_t));
    free(buffer);
    
    zmsg_addmem(serialized, &data->isRealQuery, sizeof(bool));
    
    zmsg_addmem(serialized, &data->queryFailed, sizeof(bool));
           
    zmsg_addmem(serialized, sqlSize ?  data->sqlQuery: NULL, sqlSize ? sqlSize + 1 : 0);
    
    zmsg_addmem(serialized, canonicalSqlSize ?  data->canonicalSql: NULL, canonicalSqlSize ? canonicalSqlSize + 1 : 0);
    
    zmsg_addmem(serialized, transactionIdSize ?  data->transactionId: NULL, transactionIdSize ? transactionIdSize + 1 : 0);
    
    zmsg_addmem(serialized, clientNameSize ? data->clientName: NULL, clientNameSize ? clientNameSize + 1 : 0);

    zmsg_addmem(serialized, serverNameSize ?  data->serverName: NULL, serverNameSize ? serverNameSize + 1 : 0);

    zmsg_addmem(serialized, serverUniqueNameSize ?  data->serverUniqueName: NULL, serverUniqueNameSize ? serverUniqueNameSize + 1 : 0);
    
    zmsg_addmem(serialized, affectedTablesSize ?  data->affectedTables: NULL, affectedTablesSize ? affectedTablesSize + 1 : 0);

    zmsg_addmem(serialized, queryErrorSize ?  data->queryError: NULL, queryErrorSize ? queryErrorSize + 1 : 0);

    return serialized;
}

/**
 * Converts a sequence of bytes into long
 * 
 * @param data The bytes array
 * @param start The array position that conversion should start from
 * @param size The number of bytes to include in the conversion
 * @return The converted long number
 */
static long bytesToLong(const unsigned char *data, int start, size_t size){
    union {
        unsigned char c[size];
        long l;
    } converter;
    
    int byte = 0, p = 0;
    for(byte = 0, p = start; p < start + size; byte++, p++){
        converter.c[byte] = data[p];
    }
    
    return converter.l;
}

/**
 * Converts a long into an array of bytes
 * 
 * @param num The long number to convert array
 * @param size The number of bytes to include in the conversion
 * @return The converted long number
 */
static char* longToBytes(long num, const size_t sz){
    union {
        unsigned char c[sz];
        long l;
    } converter;
    
    converter.l = num;
    
    char *res = malloc(CHAR_SZ * sz);
    memcpy(res, converter.c, sz);
    
    return res;
}

/**
 * Deallocate my_instance memory
 * 
 * @param my_instance
 */
static void freeInstance(ZMQ_INSTANCE **my_instance){
    if(*my_instance){          
        regfree(&(*my_instance)->exre);
        regfree(&(*my_instance)->re);
        free((*my_instance)->zmqHost);
        free((*my_instance)->match);
        free((*my_instance)->source);
        free((*my_instance)->user);
        free((*my_instance)->includedServers);
        free((*my_instance)->includedTables);
        free((*my_instance)->exclude);
        free(*my_instance);
        *my_instance = NULL;
    }
}

/**
 * Deallocate info memory
 * 
 * @param info
 */
static void freeInfo(ZMQ_INFO **info){
    if(*info){        
        if((*info)->queryFailed)
            free((*info)->queryError);        
        free((*info)->sqlQuery);
        free((*info)->canonicalSql);
        free((*info)->serverName);
        free((*info)->clientName);
        free((*info)->serverUniqueName);
        free((*info)->transactionId);
        free((*info)->affectedTables);        
        free(*info);
        *info = NULL;
    }
}

char* str_join(char **args, const char *sep, int len){
	int i;
	int size;
	char * str;
	int seplen = strlen(sep);
	
	if(len==0) return NULL;
	
	size = (len-1) * seplen;
	for(i=0; i<len; i++){
		size+=strlen(args[i]);
	}
	
	if(size==0) return NULL;
	
	str = (char*)malloc(sizeof(char)*(size+1));
	strcpy(str, args[0]);
	
	for(i=1; i<len; i++){
		strcat(str, sep);
		strcat(str, args[i]);
	}
	return str;
}

/**
 * Splits the string for the given delimiter and returns the results into an array
 * @param a_str The string to split
 * @param a_delim The delimiter
 * @return The array containing the results
 */
char** str_split(char *a_str, const char a_delim)
{
    char** result    = 0;
    size_t count     = 0;
    char* tmp;
    char* last_comma = 0;
    char delim[2];
    delim[0] = a_delim;
    delim[1] = 0;
    tmp = a_str;
    /* Count how many elements will be extracted. */
    while (*tmp)
    {
        if (a_delim == *tmp)
        {
            count++;
            last_comma = tmp;
        }
        tmp++;
    }

    /* Add space for trailing token. */
    count += last_comma < (a_str + strlen(a_str) - 1);

    /* Add space for terminating null string so caller
       knows where the list of returned strings ends. */
    count++;

    result = malloc(sizeof(char*) * count);

    if (result)
    {
        size_t idx  = 0;
        char* token;
        token = strtok(a_str, delim);
        while (token)
        {
            assert(idx < count);
            *(result + idx++) = strdup(token);
            token = strtok(0, delim);
        }
        assert(idx == count - 1);
        *(result + idx) = 0;
    }
    
    return result;
}

/**
 * Counts occurrences of a char in a given string
 * 
 * @param str The string
 * @param c The char to count
 * @return Occurrences of c in str
 */
static int strCharCount(const char *str, const char c){
    int cnt = 0;
    
    do{
        if(*str == c)
            cnt++;
    }while(*str++);
                                
    return cnt;
}

static bool invalid_char (int c) 
{  
    return (c<32 || c>126 || c == 34 || c == 92); 
} 

static void strip(char * str)
{
	unsigned char *ptr, *s = (void*)str;
	ptr = s;
	while (*s != '\0') {
		if (invalid_char((int)*s) == false)
			*(ptr++) = *s;
		s++;
	}
	*ptr = '\0';
}