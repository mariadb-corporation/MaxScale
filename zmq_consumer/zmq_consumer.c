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
 * The ZMQ consumer is part of MaxWeb a monitoring and administration tool for MaxScale. 
 * Visit our site http://www.maxweb.io for more information
 * 
 * Copyright Proxylab 2014 Thessaloniki-Greece http://www.proxylab.io
 * 
 */

/**
 * @file zmq_consumer.c
 * 
 * ZeroMQ Consumer
 * 
 * This program is used to retrieve queries information from the zmqfilter. The messages are sent through
 * zmq library. Additionally we use zeromq pipeline method to transfer messages along with load-balancing
 * pattern. Currently all the data is saved into a mariaDB instance (could be mysql as well) 
 * using LOAD DATA INFILE method. To achieve higher insert rates please make use of multi-threaded mode.
 * 
 * Attention: before using zmq_server please be sure that you have successfully installed zeromq (http://zeromq.org/intro:get-the-software) 
 * and czmq (http://czmq.zeromq.org/) libraries in your system.
 * 
 *@verbatim
 * The options of the configuration file are the following:
 *
 *        - threads                   The number of workers responsible for saving data
 *        - inserts_buffer_size       The size of INLOAD DATA FILE buffer
 *        - logging_enabled           True when logging is available
 *        - daemon_mode               True to enable daemon mode
 *
 *        - endpoint                  The zeromq endpoint for frontend socket
 *        - io_threads                The zeromq number of i/o threads
 *        - sndhwm                    The zeromq HWM value for sending sockets
 *        - rcvhwm                    The zeromq HWM value for receivers
 *        - pipehwm                   The zeromq HWM value for pipelines
 *
 *        - dbserver                  The mariadb/mysql database server name
 *        - dbport                    The mariadb/mysql client port
 *        - dbname                    The database name
 *        - dbuser                    The database user
 *        - dbpasswd                  User password
 *
 *        - log_directory             Logging directory
 *        - log_level                 Logging level
 *        - log_rolling_size          The max size in bytes before creating a new log file
 *
 *
 *@endverbatim
 * 
 */

#include <stdarg.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <mysql.h>
#include <ini.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <zmq.h>
#include <strings.h>
#include "czmq.h"

#define MAX_ZMQ_SNDTIMEO 5
#define BACKEND_WAIT_TIME 10
#define FRONTEND_WAIT_TIME 10
#define LOG_WAIT_TIME 10
#define INPROC_BACKEND "inproc://workers"
#define INPROC_FRONTEND "inproc://frontend"
#define INPROC_CQUERIES "inproc://canonical_queries_proc"
#define INPROC_LOG "inproc://logging"
#define LOG_FILE_NAME "zmq_consumer"
#define LOG_FILE_EXT ".log"

#define WORKER_READY "\001" //Signals worker is ready
#define WORKER_AVAILABLE "\002" //Signals worker is available again
#define WORKER_SIGNAL "\000" //Network Signal

#define STR_HASH_SZ 32
#define MAX_PATH_LEN 512
#define MAX_QUERY_LEN 2048

#define LOG_RATE 10000.0
#define LOG_MAX_LINE_LEN 1024
#define LOG_MAX_FORMAT 256

#define MIN_RAND 23356552
#define MAX_RAND 98546258

#define LONG_SZ (sizeof(long))
#define ULONG_SZ (sizeof(ulong))
#define CHAR_SZ (sizeof(char))
#define INT_SZ  (sizeof(int))

#define atob(val) (strcmp(val,"true") == 0 ? true : false)
#define str_len(s) (s ? strlen(s) : 0)

#define LONG_LEN  ({                            \
        ulong number = ULONG_MAX;               \
        int digits = 0;                         \
        do {                                    \
            number /= 10;                       \
            digits++;                           \
        } while (number != 0);                  \
        digits;})

#define ULONG_LEN (LONG_LEN)
#define INT_LEN (ULONG_LEN / 2)
#define UINT_LEN (INT_LEN)

#define report_load_prog(_t, _s) (fprintf(stdout, load_progress_messages[(int)_t], _s ? "OK" : "FAILED"))

#define MEM_ALLOC_ERROR "Couldn't allocate memory!\n"
#define LOAD_DATA_INFILE    "LOAD DATA LOCAL INFILE '%s' INTO TABLE queries " \
                                "FIELDS TERMINATED BY ',' ENCLOSED BY '\"' "    \
                                "LINES TERMINATED BY '\n' " \
                                "(clientName, serverId, transactionId, duration, requestTime, responseTime,"    \
                                "statementType, canonCommandType, sqlQuery, canonicalSqlHash,"    \
                                "affectedTables, serverName, serverUniqueName, isRealQuery, queryFailed, queryError);"

#define CREATE_TABLE_QUERIES    "CREATE TABLE IF NOT EXISTS `queries` ("    \
                                "`id` BIGINT UNSIGNED UNSIGNED NOT NULL AUTO_INCREMENT," \
                                "`clientName` VARCHAR(50) COLLATE utf8_unicode_ci NOT NULL," \
                                "`serverId` BIGINT UNSIGNED NOT NULL," \
                                "`transactionId` VARCHAR(50) COLLATE utf8_unicode_ci NULL," \
                                "`duration` DOUBLE(24,3) unsigned NOT NULL,"    \
                                "`requestTime` DATETIME NOT NULL,"  \
                                "`responseTime` DATETIME NOT NULL," \
                                "`statementType` INT NOT NULL," \
                                "`canonCommandType` TINYINT(4) NOT NULL," \
                                "`sqlQuery` VARCHAR(2048) COLLATE utf8_unicode_ci NOT NULL,"    \
                                "`canonicalSqlHash` BIGINT UNSIGNED NULL,"  \
                                "`affectedTables` VARCHAR(256) COLLATE utf8_unicode_ci NULL,"   \
                                "`serverName` VARCHAR(50) COLLATE utf8_unicode_ci NOT NULL,"    \
                                "`serverUniqueName` VARCHAR(50) COLLATE utf8_unicode_ci NOT NULL,"  \
                                "`isRealQuery` TINYINT NOT NULL DEFAULT 0,"  \
                                "`queryFailed` TINYINT NOT NULL DEFAULT 0,"  \
                                "`queryError` VARCHAR(512),"  \
                                "`createdAt` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP," \
                                "PRIMARY KEY (`id`),"   \
                                "INDEX `canon_indx` (`canonicalSqlHash`) USING BTREE" \
                                ") ENGINE=innoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;"

#define CREATE_TABLE_CANONICAL_QUERIES  "CREATE TABLE IF NOT EXISTS `canonical_queries` ("  \
                                        "`id` MEDIUMINT UNSIGNED NOT NULL AUTO_INCREMENT," \
                                        "`hash` BIGINT UNSIGNED NOT NULL,"  \
                                        "`canonicalQuery` VARCHAR(2048) NOT NULL,"  \
                                        "`count` INT UNSIGNED NOT NULL DEFAULT 1,"   \
                                        "`createdAt` TIMESTAMP NOT NULL DEFAULT 0," \
                                        "`updatedAt` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP," \
                                        "PRIMARY KEY (`hash`),"   \
                                        "INDEX `indx_id` (`id`) USING BTREE"  \
                                        ")ENGINE=innoDB DEFAULT CHARSET=utf8 COLLATE=utf8_unicode_ci;"

#define SELECT_ALL_CANONICAL_QUERIES    "SELECT hash,canonicalQuery FROM canonical_queries ORDER BY id ASC;"
#define INSERT_CANONICAL_QUERIES        "INSERT INTO canonical_queries(hash,canonicalQuery,createdAt,updatedAt) VALUES(%lu,'%s',NULL,NULL);"    
#define UPDATE_CANONICAL_QUERIES        "UPDATE canonical_queries SET count = count + 1 WHERE hash=%lu;"  

typedef enum logging_level{
    UNKNOWN     = 0,
    FATAL       = 1,        
    ERROR       = (1 << 1),
    WARN        = (1 << 2),
    INFO        = (1 << 3),
    DEBUG       = (1 << 4)        
}log_level_t;

typedef struct info {
        long                            serverId;              
	struct timeval                  duration;	  
        struct timeval                  requestTime;
        struct timeval                  responseTime;
        int			        statementType;
        int                             canonCmdType;
        bool                            isRealQuery;
        ulong                           canonicalSqlHash;
        
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

typedef struct canonical_query{
        char                            *canonicalSql;
        ulong                            hash;
        bool                             isNewRecord;    
} CANONICAL_QUERY;

typedef struct
{
    char        *db_server;
    char        *db_name;
    char        *db_uname;
    char        *db_passwd;
    int         db_port;
    
    char        *zmq_endpoint;  //zmq endpoint address
    int         zmq_io_threads; //zmq threads, default is 1
    int         zmq_sndhwm;     //zmq send HWM, default is 1000
    int         zmq_rcvhwm;     //zmq receive HWM, default is 1000
    int         zmq_pipehwm;    //zmq HWM for pipes, default is 1000
    
    int         threads;        //num of workers-threads
    int         bulk_size;      //size bulk file buffer
    char        *log_directory; //directory for logs
    ulong       log_rolling_size;//rolling size for log file
    log_level_t log_level;      //logging level ERROR,WARN,INFO or DEBUG
    bool        daemon_mode;    //true if program run with daemon mode
    bool        delayed_enabled;//true if insert method is INSERT DELAYED, otherwise bulk insert is executed
    bool        logging_enabled;//true if logging enabled
} CONFIG;

typedef enum reporting_type{        
    RPT_PARSE_CONFIG = 0, 
    RPT_INIT_ZMQ,
    RPT_INIT_LOG,
    RPT_INIT_MYSQL,
    RPT_INIT_MUTEXES,
    RPT_INIT_BD_ITEMS,
    RPT_LOAD_CANONICAL,
    RPT_CREATE_FRONTEND_SOCK,
    RPT_CREATE_BACKEND_SOCK,
    RPT_CREATE_CANONICAL_SOCK
}report_t;

typedef struct logging_info{
    char        log_dir[MAX_PATH_LEN];    
    char        file_path[MAX_PATH_LEN];
    uint        levels;
    pthread_mutex_t log_lock;
    zsock_t *  log_sock;
} LOGGER;

static CONFIG *cnfg;
static LOGGER *logger;
static int s_interrupted = 0;
static bool all_terminated = false;
static int terminated_count = 0;
static pthread_mutex_t lock, term_lock;

const char *load_progress_messages[] = { 
        "Parsing zmq_config.ini...\t\t\t\t[%s]\n",
        "Initializing ZMQ library...\t\t\t[%s]\n",
        "Initializing log sockets...\t\t\t[%s]\n",
        "Initializing mysql library...\t\t\t[%s]\n", 
        "Initializing mutexes...\t\t\t\t[%s]\n", 
        "Initializing database items...\t\t\t[%s]\n",
        "Loading canonical queries...\t\t\t[%s]\n",
        "Creating ZMQ frontend socket...\t\t\t[%s]\n",
        "Creating ZMQ backend socket...\t\t\t[%s]\n",
        "Creating ZMQ canonical queries socket...\t[%s]\n\n"
        
};

static void* worker_routine_bulk (void *args);
static void* worker_cqueries (void *args);
static void* worker_log (void *logger);
static bool load_cqueries(zhash_t *cq_dict);
static char* extract_cquery(zmsg_t *original_msg);
static CANONICAL_QUERY* get_cquery_obj(char *canon_query, zhash_t *cq_dict);
static char* generate_insert_cquery(const CANONICAL_QUERY *cq, MYSQL *mysql);
static char* generate_update_cquery(const CANONICAL_QUERY *cq, MYSQL *mysql);
static void write_query_params(FILE *f, ZMQ_INFO *data, MYSQL *mysql);
static ZMQ_INFO* zmsg_to_info(zmsg_t *message);
static CANONICAL_QUERY* zmsg_to_cquery(zmsg_t *msg);
static zmsg_t* cquery_to_zmsg(const CANONICAL_QUERY *data);
void cleanup_query(char* row, char separator);
static bool init_db_objects();
static bool init_zmq(CONFIG *cnf);
static void free_info(ZMQ_INFO **info);
static void free_config(CONFIG **cnf);
static int config_handler(void* cnfg, const char* section, const char* name, const char* value);
static size_t hash(const void *key);
void zhash_destructor (void **item);

static char* format_log_msg(log_level_t log_level, char *format, va_list args);
static char* get_target_log(const char *log_dir, char *err_msg);
static bool roll_log_file(const char* path);
static bool init_log(CONFIG *cnf, char *err_msg);
static void zmq_log(log_level_t level, char *format, ...);
static void free_logger(LOGGER **log);

/******************************* TOOLS ****************************************/

static void s_signal_handler (int signal_value);
static void s_catch_signals (void);
static char* get_cur_dir();
static long double timeval_to_sec(struct timeval _time);
static char* time_to_str (struct timeval *time);
static struct timeval* get_time_elapsed(struct timeval start, struct timeval end);
static ulong bytes_to_ulong(byte *a);
static byte* ulong_to_bytes(ulong num);
static ulong bytes_to_ulong_v2(const byte *data, const size_t size);
static byte* ulong_to_bytes_v2(ulong num, const size_t sz);
char** str_split(char *a_str, const char a_delim);
static char* uint_to_str(ulong num, size_t sz);
static int range_rand(int min, int max);
static void daemonize();
static void print_info(const ZMQ_INFO *info);
static void print_config(const CONFIG *cnf);
static void print_cquery(const CANONICAL_QUERY *data);
static void print_hash(zhash_t * zh);

/******************************* TOOLS ****************************************/

int main (int argc, char *argv []){
    srand(time(NULL));
    zhash_t* cq_dict = zhash_new ();
    zhash_set_destructor(cq_dict, zhash_destructor);
    
    cnfg = (CONFIG *)malloc(sizeof(CONFIG));
    bool status[] = {true, true, true, true, true, true, true, true, true, true}; //holds progress of loading modules
       
    report_t cur_t = RPT_PARSE_CONFIG;
    status[cur_t] = (ini_parse("zmq_config.ini", config_handler, cnfg) >= 0);
    report_load_prog(cur_t, status[cur_t]);
    if (status[cur_t] == false) {
        fprintf(stderr, "E: Can't load 'zmq_config.ini'\n");        
        goto cleanup_cnfg;
    }
    
    if(cnfg->daemon_mode == true){
        daemonize();        
    }
    
    cur_t = RPT_INIT_ZMQ;
    status[cur_t] = init_zmq(cnfg);
    report_load_prog(cur_t, status[cur_t]);
    if (status[cur_t] == false) {
        fprintf(stderr, "E: Initializing zmq failed\n");
        goto cleanup_cnfg;
    }
    
    if(cnfg->logging_enabled){
        char err_msg[256];
        memset(err_msg, 0, 256);
        
        cur_t = RPT_INIT_LOG;
        status[cur_t] = init_log(cnfg, err_msg);
        report_load_prog(cur_t, status[cur_t]); 
        if(status[cur_t] == false){
            fprintf(stderr, "E: Initializing logs failed. Error:%s\n", err_msg);
            goto cleanup_logger;
        }        
    }

    cur_t = RPT_INIT_MYSQL;
    status[cur_t] = (mysql_library_init(0, NULL, NULL) == 0);
    report_load_prog(cur_t, status[cur_t]);
    if (status[cur_t] == false) {
        zmq_log(ERROR, "Could not initialize MySQL library");
        goto cleanup_mysql;
    }
    
    cur_t = RPT_INIT_MUTEXES;
    status[cur_t] = (pthread_mutex_init(&lock, NULL) == 0) && (pthread_mutex_init(&term_lock, NULL) == 0);
    report_load_prog(cur_t, status[cur_t]);
    if (status[cur_t] == false){
         zmq_log(ERROR, "Mutex init failed");
        goto cleanup_mutex;
    }

    cur_t = RPT_INIT_BD_ITEMS;
    status[cur_t] = init_db_objects();
    report_load_prog(cur_t, status[cur_t]);
    if(status[cur_t] == false){
        zmq_log(ERROR,"Failed creating database tables.");
        goto cleanup_mutex;
    }

    cur_t = RPT_LOAD_CANONICAL;
    status[cur_t] = load_cqueries(cq_dict);
    report_load_prog(cur_t, status[cur_t]);
    if(status[cur_t] == false){
        zmq_log(ERROR, "Failed loading canonical queries.");        
        goto cleanup_dict;
    }
    
    //  Socket to talk to clients
    zsock_t *frontend = zsock_new_pull (cnfg->zmq_endpoint);

    cur_t = RPT_CREATE_FRONTEND_SOCK;
    status[cur_t] = (frontend != NULL);
    
    report_load_prog(cur_t, status[cur_t]);
    if(status[cur_t] == false){
        zmq_log(ERROR, "Frontend socket initialization failed. Error:%s", zmq_strerror(zmq_errno()));
        goto cleanup_dict;
    }
    
    //  Socket to talk to workers
    zsock_t *backend = zsock_new_router(INPROC_BACKEND);
    
    cur_t = RPT_CREATE_BACKEND_SOCK;
    status[cur_t] = (backend != NULL);
    
    report_load_prog(cur_t, status[cur_t]);
    if(status[cur_t] == false){
        zmq_log(ERROR, "Backend socket initialization failed. Error:%s", zmq_strerror(zmq_errno()));
        goto cleanup_frontend;
    }
    
    zsock_t *cq_sock = zsock_new_push (INPROC_CQUERIES);
    
    cur_t = RPT_CREATE_CANONICAL_SOCK;
    status[cur_t] = (cq_sock != NULL);
    
    report_load_prog(cur_t, status[cur_t]);
    if(status[cur_t] == false){
        zmq_log(ERROR, "Canonical socket initialization failed. Error:%s", zmq_strerror(zmq_errno()));
        goto cleanup_backend;
    }
    
    s_catch_signals();
    
    //  Launch pool of worker threads
    int thread_nbr = 0;
    pthread_t *thread_ids;
    thread_ids = (pthread_t*)calloc(cnfg->threads, sizeof(pthread_t));
    
    if(thread_ids == NULL){
        zmq_log (ERROR, MEM_ALLOC_ERROR);
        goto cleanup_canonical;
    }
        
    for (thread_nbr = 0; thread_nbr < cnfg->threads; thread_nbr++)
            pthread_create (&thread_ids[thread_nbr], NULL, worker_routine_bulk, NULL);    
    
    usleep(2000);
    
    pthread_t thread_canon;
    pthread_create (&thread_canon, NULL, worker_cqueries, NULL);
    
    usleep(2000);
    
    pthread_t thread_log;
    if(cnfg->logging_enabled)
        pthread_create (&thread_log, NULL, worker_log, NULL);
   
    void *resf = zsock_resolve(frontend);
    void *resb = zsock_resolve(backend);
    
    // Queue of available workers
    zlist_t *workers = zlist_new ();
    ulong processed_messages = 0;
    struct timeval start, end, *elapsed;
    gettimeofday(&start, NULL);
    while(!s_interrupted){
        
        zmq_pollitem_t frontend_items [] = { {resf, 0, ZMQ_POLLIN, 0 } };
        zmq_pollitem_t backend_items [] = { {resb,  0, ZMQ_POLLIN, 0 } };
        
        //  Poll frontend only if we have available workers
        int rc = zmq_poll (frontend_items, zlist_size(workers) ? 1 : 0, FRONTEND_WAIT_TIME * ZMQ_POLL_MSEC);
        if (rc == -1)
            break;              //  Interrupted
        
        //  Handle worker activity on frontend
        if (frontend_items [0].revents & ZMQ_POLLIN) {
            //  Get client request, route to first available worker
            zmsg_t *msg = zmsg_recv (frontend);
            int msg_sz = zmsg_size(msg);
            
            if (msg && msg_sz > 1) {
                byte *buf = NULL;
                CANONICAL_QUERY *cquery = NULL;
                char *canon_query = extract_cquery(msg);
                bool is_canonical = (canon_query != NULL);                
                if(is_canonical){
                    cquery = get_cquery_obj(canon_query, cq_dict);
                    buf = ulong_to_bytes_v2(cquery->hash, LONG_SZ);
                    zmsg_t *cquery_msg = cquery_to_zmsg(cquery);
                    zmsg_send(&cquery_msg, cq_sock);
                }
                
                zframe_t *wrk_id = (zframe_t*)zlist_pop(workers);
                zframe_t *empty_frm = zframe_new_empty();
                zmsg_prepend(msg, &empty_frm);//insert an empty frame first
                zmsg_prepend(msg, &wrk_id);//then worker identity to the front of the msg 
                zmsg_addmem(msg, is_canonical ? buf : NULL, ULONG_SZ);//add canonical hash to the end
                zmsg_send (&msg, backend);
                
                free(buf);
                free(canon_query);
                
            } else{//is a signal msg
                int sign = zmsg_popint(msg);                
                if(sign == 0)    
                    zmq_log(INFO, "Signal received....");
                else
                    zmq_log(WARN, "Aknown message received....");
                
                zmsg_destroy(&msg);
            }
            
        }
        
        rc = zmq_poll (backend_items, 1, BACKEND_WAIT_TIME * ZMQ_POLL_MSEC);
        if (rc == -1)
            break;              //  Interrupted
        
        //  Handle worker activity on backend
        if (backend_items [0].revents & ZMQ_POLLIN) {            
            //  Use worker identity for load-balancing
            zmsg_t *msg = zmsg_recv(backend);
            if (!msg)
                break;          //  Interrupted
            
            zframe_t *wrk_id = zmsg_pop(msg);
            zlist_append (workers, wrk_id);
            
            zframe_t *dummy = zmsg_pop(msg);//pop and destroy next frame
            zframe_destroy(&dummy);

            zframe_t *frame = zmsg_first(msg);
            char *str_id = zframe_strhex(wrk_id);
            if (memcmp (zframe_data (frame), WORKER_READY, 1) == 0)
                zmq_log(INFO, "Worker %s sent READY.", str_id);
            else if(memcmp(zframe_data(frame), WORKER_AVAILABLE, 1) == 0){
                processed_messages++;         
                if(processed_messages % (ulong)LOG_RATE == 0){
                    gettimeofday(&end, NULL);
                    elapsed = get_time_elapsed(start, end);
                    
                    char *ft = time_to_str(elapsed);
                    
                    int rate = LOG_RATE / timeval_to_sec(*elapsed);
                    zmq_log(INFO, "Processed messages [%lu] - batch time [%s] - processing rate [%d m/sec]", processed_messages, ft, rate);                    
                    free(ft);
                    
                    gettimeofday(&start, NULL);
                }
            }
            
            zmsg_destroy (&msg);
            free(str_id);
        }
    }
    
    // When we’re done, clean up properly
    while (zlist_size (workers)) {
        zframe_t *frame = (zframe_t *) zlist_pop (workers);
        zframe_destroy (&frame);
    }
    zlist_destroy (&workers);
    
    if(s_interrupted)
        zmq_log (INFO,"Interrupt received, killing main thread.");         
       
    for (thread_nbr = 0; thread_nbr < cnfg->threads; thread_nbr++)
            pthread_join(thread_ids[thread_nbr], NULL);        
    
    pthread_join(thread_canon, NULL);
    
    if(cnfg->logging_enabled){       
        do{
            all_terminated = (terminated_count == (cnfg->threads + 1));
        }while(!all_terminated);
        
        pthread_join(thread_log, NULL);
    }
    
    free(thread_ids);

cleanup_canonical:
    zsock_destroy (&cq_sock);
    
cleanup_backend:
    zsock_destroy (&backend);

cleanup_frontend:    
    zsock_destroy (&frontend);

cleanup_dict:
    zhash_purge(cq_dict);
    zhash_destroy(&cq_dict);

cleanup_mutex:    
    pthread_mutex_destroy(&lock);
    pthread_mutex_destroy(&term_lock);

cleanup_mysql:
    mysql_library_end();    

cleanup_logger:      
    zsock_destroy(&logger->log_sock);
    free_logger(&logger);

cleanup_cnfg:
    free_config(&cnfg);
    
    return 0;
}

/**
 * The routine is a worker which retrieves canonical queries information by
 * listening to the inproc://canonical_queries_proc zeromq socket and then saves the 
 * received data into the canonical_queries table.
 * 
 * @param args
 * @return 
 */
static void* worker_cqueries (void *args){
    MYSQL mysql;
    
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "zmq_server");
    
    unsigned long int tid = pthread_self();
    zmq_log(INFO, "Thread %lu is alive.", tid);
    
    if (!mysql_real_connect(&mysql, cnfg->db_server, cnfg->db_uname, cnfg->db_passwd, cnfg->db_name, cnfg->db_port, NULL, 0))
    {
        zmq_log(ERROR, "Failed to connect to database: Error: %s", mysql_error(&mysql));
        goto cleanup_mysql;
    }
    
    zsock_t *cq_sock = zsock_new_pull (INPROC_CQUERIES);
    
    if(cq_sock == NULL){
        zmq_log(ERROR, "Canonical queries socket initialization failed. Error:%s", zmq_strerror(zmq_errno()));
        goto cleanup_mysql;
    }
    
    void *res_worker = zsock_resolve(cq_sock);
    while (!s_interrupted) {
        
        zmq_pollitem_t items[] = { {res_worker, 0, ZMQ_POLLIN, 0} };
        int rc = zmq_poll(items, 1, BACKEND_WAIT_TIME * ZMQ_POLL_MSEC );
        if (rc == -1){
           break;//Interrupted 
        }
       
        if (items [0].revents & ZMQ_POLLIN) {
            zmsg_t *req = zmsg_recv (res_worker);    
            if(!req){
                break;//Interrupted
            } 
            
            CANONICAL_QUERY *cquery = zmsg_to_cquery(req);
            char *query = NULL;
            //insert new record here
            if(cquery->isNewRecord)
                query = generate_insert_cquery(cquery, &mysql);
            else//update an existing record
                query = generate_update_cquery(cquery, &mysql);
          
            if(mysql_query(&mysql, query)){                
                    zmq_log (ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));  
                    free(query);
                    free(cquery);
                    break;
            }
            
            free(query);
            free(cquery->canonicalSql);
            free(cquery);
        }
    }

    if(s_interrupted)
        zmq_log (INFO, "Interrupt received, killing thread[%lu]...", tid);   

    zsock_destroy(&cq_sock);
    
cleanup_mysql:
    mysql_close(&mysql);
    mysql_thread_end();
    
    pthread_mutex_lock(&term_lock);
    terminated_count++;
    pthread_mutex_unlock(&term_lock); 
    
    return NULL;
}

/**
 * The routine is a worker which retrieves queries information by
 * listening to the inproc://workers zeromq socket and then saves the received data into the queries table
 * using the LOAD DATA INFILE method.
 * 
 * @param args
 * @return 
 */
static void* worker_routine_bulk (void *args) {

    MYSQL mysql;  
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_OPT_LOCAL_INFILE, "1" );
    mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "zmq_server");

    if (!mysql_real_connect(&mysql, cnfg->db_server, cnfg->db_uname, cnfg->db_passwd, cnfg->db_name, cnfg->db_port, NULL, 0))
    {
        zmq_log(ERROR, "Failed to connect to database: Error: %s", mysql_error(&mysql));
        goto cleanup_mysql;
    }
    
    unsigned long int tid = pthread_self();
    zmq_log(INFO, "Thread %lu is alive.", tid);
    
    //connect to zmq endpoint 
    zsock_t *worker = zsock_new_req(INPROC_BACKEND);

    if(worker == NULL){
        
        zmq_log(ERROR, "zmq receiver failed to initialize with error:%s", zmq_strerror(zmq_errno()));
        goto cleanup_mysql;
    }
    
    //create file in which we will store queries data
    int writes_count = 0;
    char *cdir, *path, *str_uuid, *query;
    cdir = get_cur_dir();
    
    if(cdir == NULL)
        goto cleanup_zmq;
    
    zuuid_t *uuid = zuuid_new ();    
    str_uuid = zuuid_str(uuid);

    //create file path
    path = malloc(strlen(cdir) + 38);//uuid len + "/data/" len
    
    if(path == NULL){
        zmq_log(ERROR, MEM_ALLOC_ERROR);
        goto cleanup_dir;
    }
    
    strcpy(path, cdir);
    strcpy(path + strlen(cdir), "/data/");
    strcpy(path + strlen(cdir) + 6, str_uuid);
    
    query = (char*) malloc(str_len(LOAD_DATA_INFILE) + str_len(path) + 1);
    
    if(query == NULL){
        zmq_log(ERROR, MEM_ALLOC_ERROR);
        goto cleanup_path;
    }
    
    sprintf(query, LOAD_DATA_INFILE, path);
    
    FILE *f = fopen(path, "w");
    if(f == NULL) {
        zmq_log(ERROR, "Failed opening file: %s", path);
        goto cleanup;
    }
    
    // Tell broker we’re ready for work
    zframe_t *frame = zframe_new (WORKER_READY, 1);
    zframe_send (&frame, worker, 0);
    void *res_worker = zsock_resolve(worker);
    while (!s_interrupted) {
 
        zmq_pollitem_t items[] = { {res_worker, 0, ZMQ_POLLIN, 0} };
        int rc = zmq_poll(items, 1, BACKEND_WAIT_TIME * ZMQ_POLL_MSEC );
        if (rc == -1){
           break;//Interrupted 
        }

        if (items [0].revents & ZMQ_POLLIN) {        
            zmsg_t *req = zmsg_recv (res_worker);            
            if(!req){
                break;//Interrupted
            } 
            
            //handle signal msg here
            int msg_sz = 0;
            if((msg_sz = zmsg_size(req)) == 1){
                zmsg_send(&req, res_worker);
                continue;
            }
            
            ZMQ_INFO *client_msg = zmsg_to_info(req);
            ++writes_count;
            if(writes_count > cnfg->bulk_size){
                fclose(f);
                f = NULL;
                
                if(mysql_query(&mysql, query)){                
                    zmq_log (ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));  
                    free_info(&client_msg);
                    break;
                }
                
                writes_count = 0;
                
                f = fopen(path, "w");//clean existing data and reopen stream
                
                if(f == NULL) {
                    zmq_log(ERROR, "Failed reopening file: %s", path);
                    free_info(&client_msg);
                    break;
                }
            }
            
            write_query_params(f, client_msg, &mysql);
            
            free_info(&client_msg);
            
            // Tell broker we’re available for work
            zframe_t *rep_frm = zframe_new(WORKER_AVAILABLE, 1);
            zframe_send (&rep_frm, res_worker, 0);
        } 
        else{//polling timer elapsed
            fclose(f);
            f = NULL;
            
            if(mysql_query(&mysql, query)){                
                zmq_log (ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));  
                break;
            }

            writes_count = 0;

            f = fopen(path, "w");//clean existing data and reopen stream

            if(f == NULL) {
                zmq_log (ERROR,"Failed reopening file.2nd: %s", path);
                break;
            }
        }
    }//while
    
    if(s_interrupted){
        fclose(f);
        f = NULL;
        if(zsys_file_size(path) > 0){
            if(mysql_query(&mysql, query))          
                zmq_log (ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));  
        }
        
        zmq_log(INFO, "Interrupt received, killing thread[%lu]...", tid);       
    }

cleanup:   
    if(f != NULL)    
        fclose(f);
    remove(path);
    free(query);

cleanup_path:    
    free(path);  

cleanup_dir:
    free(cdir);
    zuuid_destroy(&uuid);

cleanup_zmq:
    zsock_destroy (&worker);

cleanup_mysql:        
    mysql_close(&mysql);
    mysql_thread_end();

    pthread_mutex_lock(&term_lock);
    terminated_count++;
    pthread_mutex_unlock(&term_lock);
    return NULL;
}

/**
 * This worker is responsible for receiving messages via inproc://logging socket 
 * and saving them into the current active log file
 * 
 * @param args
 * @return 
 */
static void* worker_log (void *args) {
    FILE *out_stream = fopen(logger->file_path, "a");
    if(out_stream == NULL){
        fprintf(stderr, "E: Failed opening log file for writing. Error:%s", strerror(errno));
        return NULL;
    }
    
    zsock_t *log_socket = zsock_new_pull(INPROC_LOG);    
    if(log_socket == NULL){
        fprintf(stderr, "E: Could not create pull logging socket. Error:%s\n", zmq_strerror(zmq_errno()));
        goto cleanup;
    }
    
    struct stat file_stats;
    stat(logger->file_path, &file_stats);
    
    size_t c_log_size = file_stats.st_size;
    void *res_worker = zsock_resolve(log_socket);
    while (!all_terminated) {
 
        zmq_pollitem_t items[] = { {res_worker, 0, ZMQ_POLLIN, 0} };
        int rc = zmq_poll(items, 1, LOG_WAIT_TIME * ZMQ_POLL_MSEC );
        if (rc == -1){
           continue;//Ignore Interrupt signal
        }

        if (items [0].revents & ZMQ_POLLIN) {        
            char *received = zstr_recv (res_worker);           
            if(!received){
                continue;//Ignore Interrupt signal
            }                  
            
            if(c_log_size >= cnfg->log_rolling_size){
                if(roll_log_file(logger->file_path)){              
                    if(out_stream != NULL){
                        fclose(out_stream);
                        out_stream = NULL;
                    }
                    
                    out_stream = fopen(logger->file_path, "a");
                    if(out_stream == NULL){
                        fprintf(stderr, "E: Failed opening log file for writing. Error:%s", strerror(errno));
                        break;
                    }
                    
                    c_log_size = 0;
                }
                else
                    fprintf(stderr, "E: Creating rolling file failed");
            }
            
            fprintf(out_stream, received);            
            c_log_size += str_len(received);
            
            free(received);
        }
        else{
            fclose(out_stream);
            out_stream = NULL;
            
            out_stream = fopen(logger->file_path, "a");
            if(out_stream == NULL){
                fprintf(stderr, "E: Failed opening log file for writing. Error:%s", strerror(errno));
                break;
            }
        }
    }//while
    zsock_destroy (&log_socket);

cleanup:
    if(out_stream != NULL)
        fclose(out_stream);
    
    return NULL;
}

/**
 * Exports zeromq message data and saves it into a ZMQ_INFO object.
 * 
 * @param message The zeromq message
 * @return The exported ZMQ_INFO object
 */
static ZMQ_INFO* zmsg_to_info(zmsg_t *message){
    
    if(!message || !zmsg_size(message))
        return NULL;
    
    ZMQ_INFO *info = (ZMQ_INFO*)malloc(sizeof(ZMQ_INFO));
    
    if(!info){
        zmq_log(ERROR, "Cant allocate memory!");
        return NULL;
    }
   
    zframe_t *frame = zmsg_pop(message);
    info->serverId =  zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->duration.tv_sec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->duration.tv_usec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->requestTime.tv_sec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->requestTime.tv_usec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->responseTime.tv_sec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->responseTime.tv_usec = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->statementType = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), INT_SZ) : 0;
    zframe_destroy(&frame);
    
    frame = zmsg_pop(message);
    info->canonCmdType = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), INT_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->isRealQuery = zframe_size(frame) ? (*(zframe_data(frame)) ? true : false) : 0;
    zframe_destroy(&frame);
    
    frame = zmsg_pop(message);
    info->queryFailed = zframe_size(frame) ? (*(zframe_data(frame)) ? true : false) : 0;
    zframe_destroy(&frame);
    /**
     * Strings handling...
     */

    frame = zmsg_pop(message);
    info->sqlQuery = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->canonicalSql = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->transactionId = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->clientName = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->serverName = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);
 
    frame = zmsg_pop(message);
    info->serverUniqueName = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);
    
    frame = zmsg_pop(message);
    info->affectedTables = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    frame = zmsg_pop(message);
    info->queryError = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);
    
    if(info->isRealQuery == true){
        frame = zmsg_pop(message);
        info->canonicalSqlHash = zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
        zframe_destroy(&frame);
    }
    
    zmsg_destroy(&message);
    return info;
}

/**
 * Exports zeromq message data and saves it into a CANONICAL_QUERY object.
 * 
 * @param msg
 * @return The exported CANONICAL_QUERY object
 */
static CANONICAL_QUERY* zmsg_to_cquery(zmsg_t *msg){
        
    if(!msg || !zmsg_size(msg))
        return NULL;
    
    CANONICAL_QUERY *cquery = (CANONICAL_QUERY*)malloc(sizeof(CANONICAL_QUERY));
    
    if(!cquery){
        zmq_log(ERROR, MEM_ALLOC_ERROR);
        return NULL;
    }
   
    zframe_t *frame = zmsg_pop(msg);
    cquery->hash =  zframe_size(frame) ? bytes_to_ulong_v2(zframe_data(frame), ULONG_SZ) : 0;
    zframe_destroy(&frame);

    frame = zmsg_pop(msg);
    cquery->isNewRecord = zframe_size(frame) ? (*(zframe_data(frame)) ? true : false) : 0;
    zframe_destroy(&frame);
 
    frame = zmsg_pop(msg);
    cquery->canonicalSql = zframe_size(frame) ? zframe_strdup(frame) : NULL;
    zframe_destroy(&frame);

    zmsg_destroy(&msg);
    return cquery;
}

/**
 * Transforms a CANONICAL_QUERY into a zeromq object
 * 
 * @param data The CANONICAL_QUERY object
 * @return A zmsg_t object
 */
static zmsg_t* cquery_to_zmsg(const CANONICAL_QUERY *data){
    size_t canonicalSqlSize = str_len(data->canonicalSql);    
    zmsg_t *serialized = zmsg_new ();
    
    void *buffer = ulong_to_bytes_v2(data->hash, LONG_SZ);
    zmsg_addmem(serialized, buffer, LONG_SZ);
    free(buffer);    

    zmsg_addmem(serialized, &data->isNewRecord, CHAR_SZ);    
    zmsg_addmem(serialized, canonicalSqlSize ?  data->canonicalSql: NULL, canonicalSqlSize ? canonicalSqlSize + 1 : 0);

    return serialized;
}

/**
 * Removes extra spaces from row and converts all letters to lower case
 * 
 * @param row 
 * @param separator
 */
void cleanup_query(char* row, char separator){
  char *current = row;
  int spacing = 0;
  int i;

  for(i=0; row[i]; ++i) {
    if(row[i]==' ') {
      if (!spacing) {
        /* start of a run of spaces -> separator */
        *current++ = separator;
        spacing = 1;
      }
    } else {
      *current++ = tolower(row[i]);
      spacing = 0;
    }
  }
  *current = 0;    
}

static void free_info(ZMQ_INFO **info){
    if(*info){
        free((*info)->queryError);
        free((*info)->sqlQuery);
        free((*info)->canonicalSql);
        free((*info)->serverName);
        free((*info)->serverUniqueName);
        free((*info)->clientName);
        free((*info)->transactionId);
        free((*info)->affectedTables);        
        free(*info);
        *info = NULL;
    }
}

static void free_config(CONFIG **cnf){
    if(*cnf){
        free((*cnf)->db_name);
        free((*cnf)->db_server);
        free((*cnf)->db_uname);
        free((*cnf)->db_passwd);
        free((*cnf)->zmq_endpoint);
        free(*cnf);
        *cnf = NULL;
    }
}

static int config_handler(void* cnfg, const char* section, const char* name, const char* value){
    CONFIG* pconfig = (CONFIG*)cnfg;

    #define MATCH(s, n) strcmp(section, s) == 0 && strcmp(name, n) == 0

    if (MATCH("database", "dbserver")) {
        pconfig->db_server = strdup(value);
    } else if (MATCH("database", "dbport")) {
        pconfig->db_port = atoi(value);
    } else if (MATCH("database", "dbname")) {
        pconfig->db_name = strdup(value);
    } else if (MATCH("database", "dbuser")) {
        pconfig->db_uname = strdup(value);
    } else if (MATCH("database", "dbpasswd")) {
        pconfig->db_passwd = strdup(value);
    } else if (MATCH("global", "threads")) {
        pconfig->threads = atoi(value);
    } else if(MATCH("global", "inserts_buffer_size")) {
        pconfig->bulk_size = atoi(value);
    } else if(MATCH("global", "logging_enabled")) {
        pconfig->logging_enabled = atob(value);
    } else if(MATCH("global", "daemon_mode")) {
        pconfig->daemon_mode = atob(value);
    } else if(MATCH("global", "delayed_enabled")) {
        pconfig->delayed_enabled = atob(value);
    } else if(MATCH("logging", "log_directory")) {
        pconfig->log_directory = strdup(value);
    } else if(MATCH("logging", "log_rolling_size")) {
        pconfig->log_rolling_size = atol(value);
    }else if(MATCH("logging", "log_level")) {        
        if(strcasecmp("ERROR", value) == 0)            
            pconfig->log_level = ERROR;
        else if(strcasecmp("WARN", value) == 0)
            pconfig->log_level = WARN;
        else if(strcasecmp("INFO", value) == 0)
            pconfig->log_level = INFO;
        else if(strcasecmp("DEBUG", value) == 0)
            pconfig->log_level = DEBUG;
        else
            pconfig->log_level = UNKNOWN;
        
    }else if(MATCH("zmq", "endpoint")) {
        pconfig->zmq_endpoint = strdup(value);
    } else if(MATCH("zmq", "io_threads")) {
        pconfig->zmq_io_threads = atoi(value);
    } else if(MATCH("zmq", "sndhwm")) {
        pconfig->zmq_sndhwm = atoi(value);
    } else if(MATCH("zmq", "rcvhwm")) {
        pconfig->zmq_rcvhwm = atoi(value);
    } else if(MATCH("zmq", "pipehwm")) {
        pconfig->zmq_pipehwm = atoi(value);
    }
    else {
        return 0;  /* unknown section/name, error */        
    }
    
    return 1;
}

/**
 * Generates insert query for canonical queries table
 * 
 * @param cq The object holding query parameters
 * @param mysql The mysql connection object
 * @return A string that contains the insert query
 */
static char* generate_insert_cquery(const CANONICAL_QUERY *cq, MYSQL *mysql){
    ulong length = str_len(cq->canonicalSql);
    char sql_esc[(2 * length) + 1];
    memset(sql_esc, 0, (2 * length) + 1);
    mysql_real_escape_string(mysql, sql_esc, cq->canonicalSql, length);
    
    size_t total_len = sizeof(INSERT_CANONICAL_QUERIES) + sizeof(sql_esc) + LONG_LEN;
    char *qBuf = malloc(total_len);
    memset(qBuf, 0, total_len);
    sprintf(qBuf, INSERT_CANONICAL_QUERIES, cq->hash, sql_esc);

    return qBuf;
}
 
/**
 * Generates update query for canonical queries.
 * 
 * @param cq The object holding query parameters
 * @param mysql The mysql connection object
 * @return A string that contains the update query
 */
static char* generate_update_cquery(const CANONICAL_QUERY *cq, MYSQL *mysql){
    size_t total_len = sizeof(UPDATE_CANONICAL_QUERIES) + LONG_LEN;
    char *qBuf = malloc(total_len);
    memset(qBuf, 0, total_len);
    sprintf(qBuf, UPDATE_CANONICAL_QUERIES, cq->hash);

    return qBuf;
}

/**
 * Generates and saves to out_stream a single row of the queries table
 * 
 * @param out_stream The output stream for the LOAD INFILE method
 * @param data The values for that row
 * @param mysql The mysql connection object
 */
static void write_query_params(FILE *out_stream, ZMQ_INFO *data, MYSQL *mysql){
    time_t reqTimeSec = data->requestTime.tv_sec;
    struct tm *reqTime;
    char reqTimeBuf[32];

    memset(reqTimeBuf, 0, 32);
    reqTime = localtime(&reqTimeSec);    
    strftime(reqTimeBuf, sizeof reqTimeBuf, "%Y-%m-%d %H:%M:%S", reqTime);

    time_t resTimeSec = data->responseTime.tv_sec;
    struct tm *resTime;
    char resTimeBuf[32];

    memset(resTimeBuf, 0, 32);
    resTime = localtime(&resTimeSec);
    strftime(resTimeBuf, sizeof resTimeBuf, "%Y-%m-%d %H:%M:%S", resTime);
    
    ulong length = str_len(data->sqlQuery);
    char sql_esc[(2 * length) + 1];
    memset(sql_esc, 0, (2 * length) + 1);
    mysql_real_escape_string(mysql, sql_esc, data->sqlQuery, length);

    length = str_len(data->queryError);
    char error_esc[(2 * length) + 1];
    memset(error_esc, 0,(2 * length) + 1);
    mysql_real_escape_string(mysql, error_esc, data->queryError, length);
    
    fprintf(out_stream, "\"%s\",\"%lu\",\"%s\",\"%.3Lf\",\"%s\",\"%s\",\"%d\",\"%d\",\"%s\",\"%lu\",\"%s\",\"%s\",\"%s\",\"%d\",\"%d\",\"%s\"\n",
            data->clientName, 
            data->serverId, 
            data->transactionId,
            timeval_to_sec(data->duration),
            reqTimeBuf,
            resTimeBuf,
            data->statementType,
            data->canonCmdType,
            sql_esc,
            data->canonicalSqlHash,
            data->affectedTables, 
            data->serverName,
            data->serverUniqueName,
            data->isRealQuery,
            data->queryFailed,
            error_esc);
}

/**
 * Creates queries and canonical_queries tables if they don't exist already
 * 
 * @return True if success false otherwise 
 */
static bool init_db_objects(){
    
    MYSQL mysql;  
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "zmq_server");
    bool res = true;
    
    if (!mysql_real_connect(&mysql, cnfg->db_server, cnfg->db_uname, cnfg->db_passwd, cnfg->db_name, cnfg->db_port, NULL, 0))
    {
        zmq_log(ERROR, "Failed to connect to database: Error: %s", mysql_error(&mysql));
        res = false;
    }
    
    if(mysql_query(&mysql, CREATE_TABLE_QUERIES)){
        zmq_log(ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));
        res = false;
    }
    
    if(mysql_query(&mysql, CREATE_TABLE_CANONICAL_QUERIES)){
        zmq_log(ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));
        res = false;
    }
    
    mysql_close(&mysql);
    
    return res;
}

static bool init_zmq(CONFIG *cnf){
    zsys_set_io_threads(cnf->zmq_io_threads);
    zsys_set_sndhwm(cnf->zmq_sndhwm);
    zsys_set_rcvhwm(cnf->zmq_rcvhwm);
    zsys_set_pipehwm(cnf->zmq_pipehwm);
    return true;
}

/**
 * Loads the canonical queries from the corresponding table and saves them to cq_dict dictionary
 * 
 * @param cq_dict The hashing container 
 * @return True if success false otherwise 
 */
static bool load_cqueries(zhash_t *cq_dict){    
    MYSQL mysql;  
    bool res = false;
    mysql_init(&mysql);
    mysql_options(&mysql, MYSQL_READ_DEFAULT_GROUP, "zmq_server");
    
    if (!mysql_real_connect(&mysql, cnfg->db_server, cnfg->db_uname, cnfg->db_passwd, cnfg->db_name, cnfg->db_port, NULL, 0))
    {
        zmq_log(ERROR, "Failed to connect to database: Error: %s", mysql_error(&mysql));
        goto cleanup;
    }
    
    if(mysql_query(&mysql, SELECT_ALL_CANONICAL_QUERIES)){
        zmq_log(ERROR, "Query failed. Error:[%s]", mysql_error(&mysql));
        goto cleanup;
    }

    MYSQL_RES *result = mysql_store_result(&mysql);
    
    if (result == NULL){
        zmq_log(ERROR, "mysql_store_result failed. Error:[%s]", mysql_error(&mysql));
        goto cleanup;
    }  
    
    int total_rows = mysql_num_rows(result);
    
    CANONICAL_QUERY *cqueries = (CANONICAL_QUERY*) calloc(total_rows, sizeof(CANONICAL_QUERY));    
    
    if(cqueries == NULL){
        zmq_log (ERROR, MEM_ALLOC_ERROR);
        goto cleanup;
    }
    
    int num_fields = mysql_num_fields(result);
    MYSQL_FIELD *fields = mysql_fetch_fields(result);
    MYSQL_ROW row;
    int i = 0, row_indx = 0;
    while ((row = mysql_fetch_row(result))) 
    {        
        for(i = 0; i < num_fields; i++) 
        { 
            if (strcmp(fields[i].name, "canonicalQuery") == 0)
                cqueries[row_indx].canonicalSql = strdup(row[i]);
            else{
                cqueries[row_indx].hash = strtoul(row[i], NULL, 0);
            }            
        }        
        cqueries[row_indx].isNewRecord = false;        
        zhash_insert(cq_dict, cqueries[row_indx].canonicalSql, &cqueries[row_indx]);        
        row_indx++;
    }

    res = true;
cleanup:
    mysql_free_result(result);
    mysql_close(&mysql);
    
    return res;
}

/**
 * Extracts the canonical query from a zeromq message.
 * User must deallocate the returned value after using it.
 * 
 * @param original_msg The zeromq mesage
 * @return A string containing the canonical query
 */
static char *extract_cquery(zmsg_t *original_msg){
    zframe_t *c_frm;
    //scan until isRealQuery frame found
    while((c_frm = zmsg_next(original_msg)) && zframe_size(c_frm) != 1);
    
    if(c_frm == NULL)
        return NULL;

    bool is_real = (*(zframe_data(c_frm)) ? true : false);
    
    if(is_real == false)//is not real query so there is nothing to do...
        return NULL;
    
    zmsg_next(original_msg);//forward pointer to the next frm twice
    zmsg_next(original_msg);
    c_frm = zmsg_next(original_msg);//now pointer shows to canonicalSql
    
    return zframe_strdup(c_frm);
}

/**
 * Looks up for the canon_query into the cq_dict dictionary. If lookup is successful
 * retrieves the value of the found object and stores it into a new CANONICAL_QUERY object
 * otherwise creates and returns a new CANONICAL_QUERY object.
 * 
 * @param canon_query The query to search for
 * @param cq_dict The dictionary to search into
 * @return A CANONICAL_QUERY object
 */
static CANONICAL_QUERY* get_cquery_obj(char *canon_query, zhash_t *cq_dict){
    cleanup_query(canon_query, ' ');//cleanup query from extra spaces and make all low case
    CANONICAL_QUERY *cquery = (CANONICAL_QUERY*)zhash_lookup(cq_dict, canon_query);
            
    bool found = (cquery != NULL);
    bool hash_valid = found && (strcmp(canon_query, cquery->canonicalSql) == 0);
    
    if(!hash_valid){
        
        cquery = (CANONICAL_QUERY*)malloc(sizeof(CANONICAL_QUERY));

        if(cquery == NULL){
            zmq_log(ERROR, MEM_ALLOC_ERROR);
            goto cleanup;
        }
        
        cquery->canonicalSql = strdup(canon_query);
        cquery->isNewRecord = true;

        if(!found){
            cquery->hash = hash((unsigned char*)canon_query);//generates hash for new query
            zhash_insert(cq_dict, canon_query, cquery);
        }
        else{//this is the case where hash generates an existing id for different query
            zmq_log(WARN, "Attention! Same hash for different queries found! Hash:%lu Query:%s", hash(cquery->canonicalSql), cquery->canonicalSql);
            ushort retries = 0;
            int total_len = strlen(canon_query) + INT_LEN;
            char hashed_rand_query[total_len]; 
            //try create new hash by concatenating a random int to the query
            do{                                     
                memset(hashed_rand_query, 0, total_len);
                strcpy(hashed_rand_query, canon_query);

                int random_seed = range_rand(MIN_RAND , MAX_RAND);
                char *str_rand = uint_to_str(random_seed, INT_SZ);
                strcat(hashed_rand_query, str_rand);

                hash_valid = (zhash_lookup(cq_dict, hashed_rand_query) == NULL);

                free(str_rand);
            }while(!hash_valid && retries++ < 5);

            if(!hash_valid){
                zmq_log(FATAL, "Failed to create hash for query '%s'", canon_query);
                free(cquery->canonicalSql);
                free(cquery);
            } else{
                cquery->hash = hash(hashed_rand_query);
                zhash_insert(cq_dict, hashed_rand_query, cquery);
            }
        }

    } else {
        cquery->isNewRecord = false;
    }                    

cleanup:    

    return cquery;
}

static void s_signal_handler (int signal_value){
    s_interrupted = 1;
    char *sys_msg_frmt = "\nSystem signal received program will be terminated. Message:%s\n";
    char *sys_msg = NULL;
    switch(signal_value){
        case SIGINT:
            sys_msg = "Interrupt (ANSI).";
            break;
        case SIGTERM:
            sys_msg = "Termination (ANSI).";
            break;
        case SIGQUIT:
            sys_msg = "Quit (POSIX).";
            break;
        case SIGABRT:
            sys_msg = "Abort (ANSI).";
            break;
        default:
            sys_msg = "Unhandled signal received.";
    }
    
    fprintf(stdout, sys_msg_frmt, sys_msg);
}

//  Signal handling
//
//  Call s_catch_signals() in your application at startup, and then
//  exit your main loop if s_interrupted is ever 1. Works especially
//  well with zmq_poll.
static void s_catch_signals (void){
    struct sigaction action;
    action.sa_handler = s_signal_handler;
    action.sa_flags = 0;
    sigemptyset (&action.sa_mask);
    sigaction (SIGINT, &action, NULL);
    sigaction (SIGTERM, &action, NULL);
    sigaction (SIGQUIT, &action, NULL);
    sigaction (SIGABRT, &action, NULL);
}

/**
 * Returns current directory
 * 
 * @return A string containing current directory
 */
static char* get_cur_dir(){
    char *cdir = NULL;    
    cdir = malloc(MAX_PATH_LEN);
    
    if(cdir == NULL){
        zmq_log(ERROR, MEM_ALLOC_ERROR);
        return NULL;
    }
    
    getcwd(cdir, MAX_PATH_LEN);
    
    return cdir;
}

/**
 * Converts a timeval object to seconds
 * 
 * @param _time 
 * @return
 */
static long double timeval_to_sec(struct timeval _time){
    return (long double)_time.tv_sec + ((long double)_time.tv_usec / 1000000.);
}

/**
 * Converts a byte array to unsigned long
 * 
 * @param a The byte array
 * @return The result as ulong
 */
static ulong bytes_to_ulong(byte *a) {
    ulong retval  = (ulong) a[0] << 56 | 
                        (ulong) a[1] << 48 |
                            (ulong) a[2] << 40 | 
                                (ulong) a[3] << 32 |
                                    (ulong) a[4] << 24 |
                                        (ulong) a[5] << 16 |
                                            (ulong) a[6] << 8 | (ulong) a[7];
    
    return retval;
}

/**
 * Converts an unsigned long to a byte array
 * 
 * @param num The number to convert
 * @return The result as a byte array
 */
static byte* ulong_to_bytes(ulong num){
    byte *byte_ar = malloc(8);
    byte_ar[0] = (ulong)((num >> 56) & 0xFF);
    byte_ar[1] = (ulong)((num >> 48) & 0xFF);
    byte_ar[2] = (ulong)((num >> 40) & 0XFF);
    byte_ar[3] = (ulong)((num >> 32) & 0xFF);
    byte_ar[4] = (ulong)((num >> 24) & 0xFF);
    byte_ar[5] = (ulong)((num >> 16) & 0XFF);
    byte_ar[6] = (ulong)((num >> 8) & 0XFF);
    byte_ar[7] = (ulong)((num & 0XFF));
    return byte_ar;
}

static ulong bytes_to_ulong_v2(const byte *data, const size_t size){
    union {
        byte c[size];
        ulong l;
    } converter;
    
    memset(converter.c, 0, size);
    memcpy(converter.c, data, size);
    return converter.l;
}

static char* time_to_str (struct timeval *time)
{
    char *t_res = NULL;
    time_t t_sec = time->tv_sec;
    struct tm *l_time;
    char t_buf[32];

    memset(t_buf, 0, 32);
    l_time = localtime(&t_sec);    
    strftime(t_buf, sizeof t_buf, "%H:%M:%S", l_time);
    
    ulong msec = time->tv_usec / 1000;
    
    t_res = (char*)malloc(32 + 5);//t_buf sz + msec sz
    sprintf(t_res, "%s.%04lu", t_buf, msec);
    
    return t_res;
}

static struct timeval* get_time_elapsed(struct timeval start, struct timeval end){
    struct timeval *elapsed;
    elapsed = (struct timeval*)malloc(sizeof(struct timeval));
    
    timersub(&end, &start, elapsed);

    return elapsed;
}

/**
 * Converts a long into an array of bytes
 * 
 * @param num The long number to convert array
 * @param size The number of bytes to include in the conversion
 * @return The converted long number
 */
static byte* ulong_to_bytes_v2(ulong num, const size_t sz){
    union { 
        byte c[sz];
        ulong l;
    } converter;
    
    converter.l = num;
    
    byte *res = malloc(sz);
    memset(res, 0, sz);
    memcpy(res, converter.c, sz);
    
    return res;
}

/**
 * Splits the string for the given delimiter and returns the results into an array
 * @param a_str The string to split
 * @param a_delim The delimiter
 * @return The array containing the results
 */
char** str_split(char *a_str, const char a_delim){
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

static char* uint_to_str(ulong num, size_t sz){
    int len = 0;
    
    if(sz == LONG_SZ)
        len = LONG_SZ;
    else
        len = INT_LEN;

    char *res = (char*)malloc(len);    
    memset(res, 0, len);
    sprintf(res, "%lu", num);
    
    return res;
}

/**
 * Generate a random number between min and max
 * 
 * @param min
 * @param max
 * @return 
 */
int range_rand(int min, int max)
{
    int diff = max - min;
    return (int) (((double)(diff+1)/RAND_MAX) * rand() + min);
}

/**
 * Daemonizes current program
 * 
 */
static void daemonize(void) {
	pid_t pid;

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "fork() error %s\n", strerror(errno));
		exit(1);
	}

	if (pid != 0) {
                getchar();
		exit(0);
	}
}

/**
 * Implementation of Bernstein hashing algorithm (see http://www.cse.yorku.ca/~oz/hash.html)
 * @param str
 * @return hash value
 */
static size_t hash(const void *key){
    const char *pointer = (const char *) key;
    size_t key_hash = 0;
    while (*pointer)
        key_hash = 33 * key_hash ^ *pointer++;
    
    return key_hash;
}

void zhash_destructor (void **item){
    CANONICAL_QUERY **canonical_query = (CANONICAL_QUERY**)item;
    if(*canonical_query){
        free((*canonical_query)->canonicalSql);
        *canonical_query = NULL;
    }
}

/**
 * Formats a line of the log file
 * 
 * @param log_level The level of current log message
 * @param format 
 * @param args
 * @return A string containing formatted line
 */
static char* format_log_msg(log_level_t log_level, char *format, va_list args){    
    char *line_buffer = (char*)malloc(LOG_MAX_LINE_LEN);
    char *tmp_format = (char*)malloc(LOG_MAX_FORMAT);
    char level[7];
    
    switch(log_level){
        case DEBUG:
            strcpy(level, "DEBUG");
            break;
        case INFO:
            strcpy(level, "INFO");
            break;
        case WARN:
            strcpy(level, "WARN");
            break;
        case ERROR:
            strcpy(level, "ERROR");
            break;
        case FATAL:
            strcpy(level, "FATAL");
            break;
        default:
            strcpy(level, "UNKNOWN");
    }
    
    struct timeval now;
    gettimeofday(&now, NULL);
    
    time_t now_sec = now.tv_sec;
    struct tm *t_zone_local;
    char time_buf[32];
    
    memset(time_buf, 0, 32);
    t_zone_local = localtime(&now_sec);
    strftime(time_buf, sizeof time_buf, "%Y-%m-%d %H:%M:%S", t_zone_local);
    
    memset(tmp_format, 0, LOG_MAX_FORMAT);
    sprintf(tmp_format, "[%s] %s zmq_consumer - %s\n", level, time_buf, format);

    if(args)
        vsprintf(line_buffer, tmp_format, args);
    else
        strcpy(line_buffer, tmp_format);
    
    free(tmp_format);
    return line_buffer;
}

/**
 * Explores and returns the name of the current-active log file (based on min size)
 * 
 * @param log_dir Current logging dir
 * @param err_msg Error message
 * @return A string containing the found log file
 */
static char* get_target_log(const char *log_dir, char *err_msg){
    char *c_target = NULL;
    DIR *dir;
    struct dirent *ent;
    struct stat sb;
    
    if ((dir = opendir (log_dir)) != NULL) {
        long int min = LONG_MAX;
        c_target = (char*)malloc(MAX_PATH_LEN);        
        memset(c_target, 0, MAX_PATH_LEN);
        
        while ((ent = readdir (dir)) != NULL) {
            if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                continue;

            char tmp_target[MAX_PATH_LEN];
            memset(tmp_target, 0, MAX_PATH_LEN);
            strncpy(tmp_target, log_dir, MAX_PATH_LEN);
            strncat(tmp_target, ent->d_name, MAX_PATH_LEN);

            stat(tmp_target, &sb);

            if(sb.st_size < min){
                min = sb.st_size;
                strncpy(c_target, tmp_target, MAX_PATH_LEN);
            }
        }
        
        closedir (dir);
    } else {
      /* could not open directory */
      strcpy(err_msg, strerror(errno)); 
    }
    
    return c_target;
}

/**
 * Creates a new log file
 * 
 * @param path The old file name
 * @return A string with the new log file name
 */
static bool roll_log_file(const char* path){
    int dir_len = str_len(logger->log_dir);        
    char *name = strdup(path);

    name = (name + dir_len);//advance to name       

    char **f_parts = str_split(name, '.');

    //validate file name
    bool valid = str_len(f_parts[0]) && str_len(f_parts[1]) && (strcmp(f_parts[1], "log") == 0);

    if(!valid)
        return false;

    int name_len = str_len(LOG_FILE_NAME);
    int ext_len = str_len(LOG_FILE_EXT);
    char *new_name;
    
    if(strcasecmp(f_parts[0], LOG_FILE_NAME) == 0){//first log name contains no num
        new_name = malloc(name_len + ext_len + 2);
        sprintf(new_name,"%s%s%s", LOG_FILE_NAME, "1", LOG_FILE_EXT);
    }
    else{            
        new_name = malloc(name_len + ext_len + INT_LEN + 1);
        sprintf(new_name,"%s%d%s", LOG_FILE_NAME, atoi(f_parts[0] + name_len) + 1, LOG_FILE_EXT);
    }

    memset(logger->file_path, 0, MAX_PATH_LEN);
    strcpy(logger->file_path, logger->log_dir);
    strcat(logger->file_path, new_name);

    free(new_name);
    free(f_parts[0]);
    free(f_parts[1]);
    free(f_parts);
    
    return true;
}

/**
 * Initializes logging module
 * @param cnf
 * @param err_msg
 * @return 
 */
static bool init_log(CONFIG *cnf, char *err_msg){
    bool res = false, is_new = false;
    logger = (LOGGER*)malloc(sizeof(LOGGER));
     
    switch(cnf->log_level){        
        case DEBUG:
            logger->levels = DEBUG | INFO | WARN | ERROR;
            break;
        case INFO:
            logger->levels = INFO | WARN | ERROR;
            break;
        case WARN:
            logger->levels = WARN | ERROR;
            break;
        case ERROR:
            logger->levels = ERROR | FATAL;
        case FATAL:
            logger->levels = FATAL;
            break;
        default:
            logger->levels = 0;
    }   
            
    logger->log_sock = zsock_new_push(INPROC_LOG);    
    if(logger->log_sock == NULL){
        sprintf(err_msg, "Could not create push logging socket. Error:%s\n", zmq_strerror(zmq_errno()));
        return false;
    }
    zsock_set_sndtimeo(logger->log_sock, MAX_ZMQ_SNDTIMEO);
    pthread_mutex_init(&logger->log_lock, NULL);
    
    struct stat s;
    int err = stat(cnf->log_directory, &s);
    if(cnf->log_directory){//use specified log dir
        
        if(cnf->log_directory[str_len(cnf->log_directory) - 1] != '/')
            strcat(cnf->log_directory, "/");
        
        if(-1 == err) {
            if(ENOENT == errno) {
                if(mkdir(cnf->log_directory, 0777) == 0){
                    res = true;
                    is_new = true;
                    strncpy(logger->log_dir, cnf->log_directory, MAX_PATH_LEN);
                }
                else
                    sprintf(err_msg, "Failed creating log dir %s. Error:%s", cnf->log_directory, strerror(errno));                   

            } else 
                strcpy(err_msg, strerror(errno));
            
        } else {
            if(S_ISDIR(s.st_mode)) {
                res = true;
                strncpy(logger->log_dir, cnf->log_directory, MAX_PATH_LEN);
            } else 
                sprintf(err_msg, "%s is not a valid dir.", cnf->log_directory);            
        }  
    }
    else {//create log folder to current dir
        char log_dir[MAX_PATH_LEN];
        char *c_dir = get_cur_dir();
        
        memset(log_dir, 0, MAX_PATH_LEN);
        memset(logger->log_dir, 0, MAX_PATH_LEN);
        
        strcpy(log_dir, c_dir);
        strcat(log_dir, "/log/");       
        
        err = stat(log_dir, &s);
        if(-1 == err) {
            if(ENOENT == errno) {
                if(mkdir(log_dir, 0777) == 0){
                    res = true;
                    is_new = true;
                    strncpy(logger->log_dir, log_dir, MAX_PATH_LEN);
                }
                else
                    sprintf(err_msg, "Failed creating log dir %s. Error:%s", log_dir, strerror(errno));  
            } else 
                strcpy(err_msg, strerror(errno));
            
        } else {
            if(S_ISDIR(s.st_mode)) {
                res = true;
                strncpy(logger->log_dir, log_dir, MAX_PATH_LEN);
            } else 
                sprintf(err_msg, "%s is not a valid dir.", log_dir);            
        }  

        free(c_dir);
    }
    
    if(res == true){
        char log_file[MAX_PATH_LEN];
        memset(log_file, 0, MAX_PATH_LEN);
        
        if(is_new){//if new dir create log file            
            strncpy(log_file, logger->log_dir, MAX_PATH_LEN);
            strcat(log_file, LOG_FILE_NAME);
            strcat(log_file, LOG_FILE_EXT);
            
            FILE *out_stream = fopen(log_file, "a");
            
            if(out_stream == NULL){
                sprintf(err_msg, "Failed creating log file %s. Error:%s", log_file, strerror(errno));
                res = false;
            }
            else{
                fprintf(out_stream, format_log_msg(INFO, "Log initialized...", NULL));
                fclose(out_stream); 
                strncpy(logger->file_path, log_file, MAX_PATH_LEN);
            }           
        }
        else{//log dir exists so get filename from existing logs
            char *tmp_file = get_target_log(logger->log_dir, err_msg);
            
            if(str_len(err_msg) > 0)
                res = false;
            else{
                if(str_len(tmp_file) == 0){//no file found, create a new one
                    strncpy(log_file, logger->log_dir, MAX_PATH_LEN);
                    strcat(log_file, LOG_FILE_NAME);
                    strcat(log_file, LOG_FILE_EXT);
                    
                    FILE *out_stream = fopen(log_file, "a");
                    if(out_stream == NULL){
                        sprintf(err_msg, "Failed creating log file %s. Error:%s", log_file, strerror(errno));
                        res = false;
                    }
                    else{                        
                        fprintf(out_stream, format_log_msg(INFO, "Log initialized...", NULL));
                        fclose(out_stream); 
                        strncpy(logger->file_path, log_file, MAX_PATH_LEN);
                    }   
                }
                else{
                    FILE *out_stream = fopen(tmp_file, "a");
                    if(out_stream == NULL){
                        sprintf(err_msg, "Failed opening log file %s. Error:%s", tmp_file, strerror(errno));
                        res = false;
                    }
                    else{
                        fprintf(out_stream, format_log_msg(INFO, "Log initialized...", NULL));
                        fclose(out_stream);
                        strncpy(logger->file_path, tmp_file, MAX_PATH_LEN);                        
                    }
                }
            }
            
            free(tmp_file);
        }
    }
    
    return res;
}

static void zmq_log(log_level_t level, char *format, ...){       
    pthread_mutex_lock(&logger->log_lock);
    if(logger && ((logger->levels & level) == 0))
        return;   
    
    char *msg = NULL;    
    va_list aptr;
    va_start(aptr, format);
    msg = format_log_msg(level, format, aptr);
    va_end(aptr);

    //if not running under daemon_mode show messages on the screen
    if(cnfg->daemon_mode == false)
        fprintf(stderr, msg);

    if(cnfg->logging_enabled)
        zstr_send(zsock_resolve(logger->log_sock), msg);

    free(msg);
    pthread_mutex_unlock(&logger->log_lock);
}

static void free_logger(LOGGER **log){
    if(*log){
        free(*log);
    }
}

static void print_info(const ZMQ_INFO *info){
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

static void print_config(const CONFIG *cnf){
    char *msg = "\tdb_server=%s\n"
                "\tdb_name=%s\n"
                "\tdb_uname=%s\n"
                "\tdb_passwd=%s\n"
                "\tdb_port=%d\n"
                "\tzmq_endpoint=%s\n"
                "\tthreads=%d\n"
                "\tbulk_size=%d\n"
                "\tdaemon_mode=%d\n"
                "\tlogging_enabled=%d\n";
                
    fprintf(stdout, msg, 
            cnf->db_server,
            cnf->db_name,
            cnf->db_uname,
            cnf->db_passwd,
            cnf->db_port,
            cnf->zmq_endpoint,
            cnf->threads,
            cnf->bulk_size,
            cnf->daemon_mode,
            cnf->logging_enabled
    );
}

static void print_cquery(const CANONICAL_QUERY *data){
    fprintf(stdout, 
            "\ncanonicalSql=%s\n"
            "hash=%lu\n"
            "isNewRecord=%s\n\n",
            data->canonicalSql,
            data->hash,
            data->isNewRecord == true ? "true" : "false"
    );
}

static void print_hash(zhash_t * zh){
    void *cur = zhash_first(zh);
    
    while(cur != NULL){
        printf("Item key:%lu\n", bytes_to_ulong_v2(zhash_cursor(zh), ULONG_SZ));
        print_cquery((CANONICAL_QUERY*)cur);
        
        cur = zhash_next(zh);
    }
}
