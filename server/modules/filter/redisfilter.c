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
 * The redisfilter filter is part of MaxWeb a monitoring and administration tool for MaxScale. 
 * Visit our site http://www.maxweb.io for more information
 * 
 * Copyright Proxylab 2015 Thessaloniki-Greece http://www.proxylab.io
 */

/**
 * @file redisfilter.c
 * 
 * Redis Threaded Filter
 * 
 * The redis filter stores information over the executed queries into Redis using separate I/O Thread
 * 
 * Requirements: 
 *  - hiredis C client lib. 
  * 
 * redisfilter extracts the following data from each query then sends it through the network:
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
 *      - redisHost             Host for redis socket
 *      - redisPort             Port for redis
 *	- redissock		UNIX socket path
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
#include <pthread.h>

/*
 * hiredis c client: https://github.com/redis/hiredis >= 0.12
 * $ make clean all
 * $ sudo make install @ /usr/local/include/hiredis & /usr/local/lib
 * 
 * json c module: http://ccodearchive.net/info/json.html
 * Copied to utils folder
 */

#include <json.h>
#include <hiredis/hiredis.h>
#include <ctype.h>

extern int lm_enabled_logfiles_bitmask;

#define STR_LEN(s) (s ? strlen(s) : 0)
#define LONG_SZ (sizeof(long))
#define CHAR_SZ (sizeof(char))
#define INT_SZ  (sizeof(int))
        
MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_IN_DEVELOPMENT,
	FILTER_VERSION,
	"A filter for sending query details to Redis server separate I/O Thread."
};

static char *version_str = "V1.0.0";
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
        char    *redisHost;             /* Redis server host */   
        int     redisPort;              /* Redis server port */
        char    *redisSock;             /* Redis Unix Socket */
        redisContext *ctx;              /* The Redis context */
        
        bool    saveRealOnly;           /* Save only real queries */
} REDIS_INSTANCE;

/**
 * Structure to hold the query info
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
} REDIS_INFO;

/**
 * The session structure for filter.
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
    REDIS_INFO          *current;

    struct timeval	start;                  /* Time that session started */
    struct timeval	total;                  /* Total running time for this session */
    struct timeval	connect;                /* When session initialized */
    struct timeval	disconnect;             /* When session terminated */

    int                 n_statements;           /* Executed statements for this session */
} REDIS_SESSION;

typedef struct {
    
    JsonNode            *json_tree;
    struct timeval      request_time;
    
} REDIS_MESSAGE;

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t sig_consumer = PTHREAD_COND_INITIALIZER;
pthread_cond_t sig_producer = PTHREAD_COND_INITIALIZER;
pthread_t thread_t;
REDIS_MESSAGE the_msg;

char** str_split(char *a_str, const char a_delim);
char* str_join(char **args, const char *sep, int len);
JsonNode* infoToJson(const REDIS_INFO * data);
void print_info(const REDIS_INFO *info);
long double timeval_to_sec(struct timeval _time);

void* redisSender (void *args);

static void freeInstance(REDIS_INSTANCE **instance);
static void freeInfo(REDIS_INFO **info);
static int strCharCount(const char *str, const char c);
static size_t infoSize(const REDIS_INFO *data);
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
    REDIS_INSTANCE *my_instance;
    
    skygw_log_enable(LOGFILE_ERROR);
    
    if ((my_instance = calloc(1, sizeof(REDIS_INSTANCE))) != NULL)
    {
        my_instance->match = NULL;
        my_instance->exclude = NULL;
        my_instance->source = NULL;
        my_instance->user = NULL;
        my_instance->saveRealOnly = false;

        my_instance->redisHost = "127.0.0.1";
        my_instance->redisPort = 6379;
        my_instance->redisSock = "/tmp/redis.sock";
        
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
            else if (!strcmp(params[i]->name, "redishost"))
            {
                my_instance->redisHost = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "redisport"))
            {
                my_instance->redisPort = atoi(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "redissock"))
            {
                my_instance->redisSock = strdup(params[i]->value);
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
                skygw_log_write(LOGFILE_ERROR, "redisfilter: Unexpected parameter '%s'", params[i]->name);
            }
        }      
        if (options)
        {
            skygw_log_write(LOGFILE_TRACE, "redisfilter: Options are not supported by this filter. They will be ignored");
        }
        my_instance->sessions = 0;
        if (my_instance->match &&
                regcomp(&my_instance->re, my_instance->match, REG_EXTENDED + REG_ICASE))
        {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: Invalid regular expression '%s' for the match parameter.", my_instance->match);
            freeInstance(&my_instance);
            return NULL;
        }
        if (my_instance->exclude &&
                regcomp(&my_instance->exre, my_instance->exclude, REG_EXTENDED + REG_ICASE))
        {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: Invalid regular expression '%s' for the nomatch paramter.", my_instance->match);
            freeInstance(&my_instance);
            return NULL;
        }
        
        if(my_instance->redisPort <= 0)
        {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: Invalid Redis port[%d]" , my_instance->redisPort);
            freeInstance(&my_instance);
            return NULL;
        }
        
        if (pthread_mutex_init(&lock, NULL) != 0)
        {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: pthread_mutex_init failure:\n");
            return NULL;
        }
        
        // Redis connect
        struct timeval timeout = { 1, 500000 }; // 1.5 seconds
        redisContext *c = redisConnectUnixWithTimeout(my_instance->redisSock, timeout);
        if (c == NULL || c->err) {
            if (c) {
                skygw_log_write(LOGFILE_ERROR, "redisfilter: Connection error: %s\n", c->errstr);
                redisFree(c);
            } else {
                skygw_log_write(LOGFILE_ERROR, "Connection error: can't allocate redis context\n");
            }
            
            return NULL;
        }
        
        my_instance->ctx = c;
        
        pthread_create (&thread_t, NULL, redisSender, (void *) c);

        skygw_log_write(LOGFILE_TRACE, "redisfilter instance created.");
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
    REDIS_INSTANCE    *my_instance = (REDIS_INSTANCE *)instance;
    REDIS_SESSION     *my_session = (REDIS_SESSION*)session;
    char            *user;
        
    if ((my_session = calloc(1, sizeof(REDIS_SESSION))) != NULL)
    {             
        my_session->total.tv_sec = 0;
        my_session->total.tv_usec = 0;
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
                skygw_log_write(LOGFILE_TRACE, "redisfilter: Session inactive. Reason: hostname filter.");
        }

        if (my_instance->user && strcmp(my_session->userName, my_instance->user)){
                my_session->active = 0;
                skygw_log_write(LOGFILE_TRACE, "redisfilter: Session inactive. Reason: user filter.");
        }

        gettimeofday(&my_session->connect, NULL);
        
        if(my_session->active)
            skygw_log_write(LOGFILE_TRACE, "redisfilter: Session created.");
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
    REDIS_SESSION	*my_session = (REDIS_SESSION *)session;

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
    REDIS_SESSION	*my_session = (REDIS_SESSION *)session;

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
    REDIS_SESSION *my_session = (REDIS_SESSION *)session;

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
    REDIS_INSTANCE    *my_instance = (REDIS_INSTANCE *)instance;
    REDIS_SESSION     *my_session = (REDIS_SESSION *)session;
    char            *ptr = NULL;
    int             length;
        
    if (my_session->active && modutil_extract_SQL(queue, &ptr, &length))
    {       
        skygw_log_write_flush(LOGFILE_DEBUG, "redisfilter: Query received");
        freeInfo(&my_session->current);

        if ((my_instance->match == NULL || regexec(&my_instance->re, ptr, 0, NULL, 0) == 0) &&
                (my_instance->exclude == NULL || regexec(&my_instance->exre,ptr,0,NULL, 0) != 0))
        {

            if((my_session->current = (REDIS_INFO*) malloc(sizeof(REDIS_INFO))) == NULL){
                my_session->active = 0;
                skygw_log_write(LOGFILE_ERROR, "redisfilter: Memory allocation failed for: %s" , "route_query");                            
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
                        skygw_log_write(LOGFILE_DEBUG, "redisfilter: Query parsed.");

                        my_session->current->isRealQuery = skygw_is_real_query(queue);
                        my_session->current->statementType = query_classifier_get_type(queue);

                        if(my_session->current->isRealQuery){
                            skygw_log_write(LOGFILE_DEBUG, "redisfilter: Current is real query.");            
                            
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
                    skygw_log_write(LOGFILE_DEBUG, "redisfilter: Analyzing included tables filter.");
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
    REDIS_SESSION     *my_session = (REDIS_SESSION *)session;
    REDIS_INSTANCE    *my_instance = (REDIS_INSTANCE *)instance;
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
            skygw_log_write(LOGFILE_DEBUG, "redisfilter: Analyzing included servers filter.");
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
                goto send_to_upstream;
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

        if (my_instance->ctx == NULL) {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: NULL Redis Context");
            freeInfo(&my_session->current);
            goto send_to_upstream;
        }
        
        // Info structure -> json object
        //print_info(my_session->current);
        JsonNode* infoNode = infoToJson(my_session->current);
        if (infoNode == NULL) {
            freeInfo(&my_session->current);
            goto send_to_upstream;
        }
        
        pthread_mutex_lock(&lock);
        
        the_msg.json_tree = infoNode;
        the_msg.request_time = my_session->current->requestTime;
        
        pthread_cond_signal(&sig_consumer);
        pthread_cond_wait(&sig_producer, &lock);
        pthread_mutex_unlock(&lock);
        
        freeInfo(&my_session->current);
    }

send_to_upstream:
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
REDIS_INSTANCE	*my_instance = (REDIS_INSTANCE *)instance;
REDIS_SESSION	*my_session = (REDIS_SESSION *)fsession;
 
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
        
        if (my_instance->redisHost)
		dcb_printf(dcb, "\t\tRedis host		%s\n", my_instance->redisHost);
        
        dcb_printf(dcb, "\t\tRedis port		%d\n", my_instance->redisPort);
	
        if (my_session)
	{
		dcb_printf(dcb, "\t\t\tSession is active to file %s.\n", my_session->active ? "true" : "false");
		dcb_printf(dcb, "\t\t\tSession username %s:\n", my_session->userName);
                dcb_printf(dcb, "\t\t\tSession client host %s:\n", my_session->clientHost);
                dcb_printf(dcb, "\t\t\tSession statements %d:\n", my_session->n_statements);

	}
}

static size_t infoSize(const REDIS_INFO *data){
    
    size_t total = STR_LEN(data->sqlQuery);
    total += STR_LEN(data->canonicalSql);
    total += STR_LEN(data->transactionId);
    total += STR_LEN(data->clientName);
    total += STR_LEN(data->serverName);
    total += STR_LEN(data->serverUniqueName);
    total += STR_LEN(data->affectedTables);
    total += STR_LEN(data->queryError);

    total += 7*LONG_SZ;
    
    // statementType
    total += sizeof(skygw_query_type_t);
    
    // canonCmdType
    total += sizeof(canonical_cmd_t);
    
    // isRealQuery
    // queryFailed
    total += 2*sizeof(bool);
    
    return total;
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
static void freeInstance(REDIS_INSTANCE **my_instance){
    if(*my_instance){        
        regfree(&(*my_instance)->exre);
        regfree(&(*my_instance)->re);
        free((*my_instance)->redisHost);
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
static void freeInfo(REDIS_INFO **info){
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

/**
 * Converts an INFO struct to JSON value.
 * The return value is a pointer to a newly allocated JsonNode object, 
 * which must be freed after usage.
 * 
 * @param data
 * @return JsonNode*
 */
JsonNode* infoToJson(const REDIS_INFO * data)
{
    JsonNode *info = json_mkobject();
    if (info == NULL) {
        goto alloc_error_exit;
    }
    
    JsonNode* duration = json_mkobject();
    if (duration == NULL) {
        goto alloc_error_exit;
    }
    json_append_member(duration, "tv_sec", json_mknumber((double) data->duration.tv_sec));
    json_append_member(duration, "tv_usec", json_mknumber((double) data->duration.tv_usec));
    
    JsonNode* requestTime = json_mkobject();
    if (requestTime == NULL) {
        goto alloc_error_exit;
    }
    json_append_member(requestTime, "tv_sec", json_mknumber((double) data->requestTime.tv_sec));
    json_append_member(requestTime, "tv_usec", json_mknumber((double) data->requestTime.tv_usec));
    
    JsonNode* responseTime = json_mkobject();
    if (responseTime == NULL) {
        goto alloc_error_exit;
    }
    json_append_member(responseTime, "tv_sec", json_mknumber((double) data->responseTime.tv_sec));
    json_append_member(responseTime, "tv_usec", json_mknumber((double) data->responseTime.tv_usec));
    
    json_append_member(info, "serverId", json_mknumber((double) data->serverId));
    json_append_member(info, "duration", duration);
    json_append_member(info, "requestTime", requestTime);
    json_append_member(info, "responseTime", responseTime);
    json_append_member(info, "statementType", json_mknumber((double) data->statementType));
    json_append_member(info, "canonCmdType", json_mknumber((double) data->canonCmdType));
    json_append_member(info, "isRealQuery", json_mkbool(data->isRealQuery));
    json_append_member(info, "queryFailed", json_mkbool(data->queryFailed));
    
    json_append_member(info, "sqlQuery", json_mkstring(data->sqlQuery));
    json_append_member(info, "canonicalSql", json_mkstring(data->canonicalSql));
    json_append_member(info, "transactionId", json_mkstring(data->transactionId));
    json_append_member(info, "clientName", json_mkstring(data->clientName));
    json_append_member(info, "serverName", json_mkstring(data->serverName));
    json_append_member(info, "serverUniqueName", json_mkstring(data->serverUniqueName));
    json_append_member(info, "affectedTables", json_mkstring(data->affectedTables));
    json_append_member(info, "queryError", json_mkstring(data->queryError));
    
    return info;
    
alloc_error_exit:
    skygw_log_write(LOGFILE_ERROR, "redisfilter: Memory allocation failed for: %s" , "infoToJson");
    return NULL;
    
}

void print_info(const REDIS_INFO *info)
{
    time_t reqTimeSec = info->requestTime.tv_sec;
    struct tm *reqTime;
    char reqTimeBuf[32];

    reqTimeBuf[0] = 0; 
    reqTime = localtime(&reqTimeSec);    
    strftime(reqTimeBuf, sizeof reqTimeBuf, "%Y-%m-%d %H:%M:%S", reqTime);

    time_t resTimeSec = info->responseTime.tv_sec;
    struct tm *resTime;
    char resTimeBuf[32];

    resTimeBuf[0] = 0;
    resTime = localtime(&resTimeSec);
    strftime(resTimeBuf, sizeof resTimeBuf, "%Y-%m-%d %H:%M:%S", resTime);
    
    
    
    fprintf(stdout, 
            "\nserverId=%lu\n"
            "duration=%.3Lf\n"
            "requestTime=%s\n"
            "responseTime=%s\n"
            "statementType=%d\n"
            "canonCmdType=%d\n"
            "isRealQuery=%d\n"
            "sqlQuery=%s\n"
            "canonicalSql=%s\n"
            "transactionId=%s\n"
            "clientName=%s\n"
            "serverName=%s\n"
            "serverUniqueName=%s\n"
            "affectedTables=%s\n",
            info->serverId,
            timeval_to_sec(info->duration),
            reqTimeBuf,
            resTimeBuf,
            info->statementType,
            info->canonCmdType,
            info->isRealQuery,
            info->sqlQuery,
            info->canonicalSql,
            info->transactionId,
            info->clientName,
            info->serverName,
            info->serverUniqueName,
            info->affectedTables
    );
}


long double timeval_to_sec(struct timeval _time){
    return (long double)_time.tv_sec + ((long double)_time.tv_usec / 1000000.);
}

void* redisSender (void *args) {
    
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    
    skygw_log_write(LOGFILE_DEBUG, "redisfilter: redisSender starting");
    
    if (args == NULL) {
        skygw_log_write(LOGFILE_ERROR, "redisfilter: NULL Redis Context supplied");
        return NULL;
    }
    redisContext *ctx = (redisContext *) args;
    
    while (1)
    {
        pthread_mutex_lock(&lock);
        /* Signal the producer that the consumer is ready. */
        pthread_cond_signal(&sig_producer);
        /* Wait for a new message. */
        pthread_cond_wait(&sig_consumer, &lock);
        
        char *json_message = json_encode(the_msg.json_tree);
        if (json_message != NULL) {
            /* Send message to Redis. */
            redisReply *reply = redisCommand(ctx, "ZADD queries %lu.%d %s", 
                    the_msg.request_time.tv_sec, 
                    the_msg.request_time.tv_usec,
                    json_message);

            skygw_log_write(LOGFILE_DEBUG, "redisfilter: ZADD reply: %s", reply->str);
        
            freeReplyObject(reply);
            free(json_message);
        } else {
            skygw_log_write(LOGFILE_ERROR, "redisfilter: NULL value returned from json_encode");
        }
        
        json_delete(the_msg.json_tree);
        the_msg.json_tree = NULL;
        
        /* Unlock the mutex. */
        pthread_mutex_unlock(&lock);
    }
    
    skygw_log_write(LOGFILE_DEBUG, "redisfilter: redisSender ending");
    
    return NULL;
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
