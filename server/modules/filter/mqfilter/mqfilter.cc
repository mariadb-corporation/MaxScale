/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file mqfilter.c
 * MQ Filter - AMQP Filter.
 * A filter that logs and publishes canonized queries on to a RabbitMQ server.
 *
 * The filter reads the routed query, forms a canonized version of it and publishes the
 * message on the RabbitMQ server. The messages are timestamped with a pure unix-timestamp that
 * is meant to be easily transformable in various environments. Replies to the queries are also logged
 * and published on the RabbitMQ server.
 *
 * The filter makes no attempt to deal with queries that do not fit
 * in a single GWBUF or result sets that span multiple GWBUFs.
 *
 * To use a SSL connection the CA certificate, the client certificate and the client public
 * key must be provided.
 * By default this filter uses a TCP connection.
 *@verbatim
 * The options for this filter are:
 *
 *      logging_trigger Set the logging level
 *      logging_strict  Sets whether to trigger when any of the parameters match or only if
 *                      all parameters match
 *      logging_log_all Log only SELECT, UPDATE, DELETE and INSERT or all possible queries
 *      hostname        The server hostname where the messages are sent
 *      port            Port to send the messages to
 *      username        Server login username
 *      password        Server login password
 *      vhost           The virtual host location on the server, where the messages are sent
 *      exchange        The name of the exchange
 *      exchange_type   The type of the exchange, defaults to direct
 *      key             The routing key used when sending messages to the exchange
 *      queue           The queue that will be bound to the used exchange
 *      ssl_CA_cert     Path to the CA certificate in PEM format
 *      ssl_client_cert Path to the client cerificate in PEM format
 *      ssl_client_key  Path to the client public key in PEM format
 *
 * The logging trigger levels are:
 *      all     Log everything
 *      source  Trigger on statements originating from a particular source (database user and
 *              host combination)
 *      schema  Trigger on a certain schema
 *      object  Trigger on a particular database object (table or view)
 *@endverbatim
 * See the individual struct documentations for logging trigger parameters
 */

#define MXS_MODULE_NAME "mqfilter"

#include <stdio.h>
#include <fcntl.h>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <maxbase/atomic.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/log.h>
#include <maxscale/query_classifier.h>
#include <maxscale/session.h>
#include <maxscale/housekeeper.h>
#include <maxscale/alloc.h>

static int uid_gen;
static int hktask_id = 0;
/*
 * The filter entry points
 */
static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER*);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                setDownstream(MXS_FILTER* instance,
                                         MXS_FILTER_SESSION* fsession,
                                         MXS_DOWNSTREAM* downstream);
static void setUpstream(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* fsession,
                        MXS_UPSTREAM* upstream);
static int      routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static int      clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

/**
 * Structure used to store messages and their properties.
 */
typedef struct mqmessage_t
{
    amqp_basic_properties_t* prop;
    char*                    msg;
    struct mqmessage_t*      next;
} mqmessage;

/**
 * Logging trigger levels
 */
enum log_trigger_t
{
    TRG_ALL    = 0x00,
    TRG_SOURCE = 0x01,
    TRG_SCHEMA = 0x02,
    TRG_OBJECT = 0x04
};

/**
 * Source logging trigger
 *
 * Log only those queries that come from a valid pair of username and hostname combinations.
 * Both options allow multiple values separated by a ','.
 * @verbatim
 * Trigger options:
 *      logging_source_user     Comma-separated list of usernames to log
 *      logging_source_host     Comma-separated list of hostnames to log
 * @endverbatim
 */
typedef struct source_trigger_t
{
    char** user;
    int    usize;
    char** host;
    int    hsize;
} SRC_TRIG;

/**
 * Schema logging trigger
 *
 * Log only those queries that target a specific database.
 *
 * Trigger options:
 *      logging_schema  Comma-separated list of databases
 */
typedef struct schema_trigger_t
{
    char** objects;
    int    size;
} SHM_TRIG;

/**
 * Database object logging trigger
 *
 * Log only those queries that target specific database objects.
 *@verbatim
 * Trigger options:
 *      logging_object  Comma-separated list of database objects
 *@endverbatim
 */
typedef struct object_trigger_t
{
    char** objects;
    int    size;
} OBJ_TRIG;

/**
 * Statistics for the mqfilter.
 */
typedef struct mqstats_t
{
    int n_msg;      /*< Total number of messages */
    int n_sent;     /*< Number of sent messages */
    int n_queued;   /*< Number of unsent messages */
} MQSTATS;

/**
 * A instance structure, containing the hostname, login credentials,
 * virtual host location and the names of the exchange and the key.
 * Also contains the paths to the CA certificate and client certificate and key.
 *
 * Default values assume that a local RabbitMQ server is running on port 5672 with the default
 * user 'guest' and the password 'guest' using a default exchange named 'default_exchange' with a
 * routing key named 'key'. Type of the exchange is 'direct' by default and all queries are logged.
 *
 */
typedef struct
{
    int                     port;
    char*                   hostname;
    char*                   username;
    char*                   password;
    char*                   vhost;
    char*                   exchange;
    char*                   exchange_type;
    char*                   key;
    char*                   queue;
    bool                    use_ssl;
    bool                    log_all;
    bool                    strict_logging;
    char*                   ssl_CA_cert;
    char*                   ssl_client_cert;
    char*                   ssl_client_key;
    amqp_connection_state_t conn;       /**The connection object*/
    amqp_socket_t*          sock;       /**The currently active socket*/
    amqp_channel_t          channel;    /**The current channel in use*/
    int                     conn_stat;  /**state of the connection to the server*/
    int                     rconn_intv; /**delay for reconnects, in seconds*/
    time_t                  last_rconn; /**last reconnect attempt*/
    pthread_mutex_t         rconn_lock;
    pthread_mutex_t         msg_lock;
    mqmessage*              messages;
    enum log_trigger_t      trgtype;
    SRC_TRIG*               src_trg;
    SHM_TRIG*               shm_trg;
    OBJ_TRIG*               obj_trg;
    MQSTATS                 stats;
} MQ_INSTANCE;

/**
 * The session structure for this MQ filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * Also holds the necessary session connection information.
 *
 */
typedef struct
{
    char*          uid; /**Unique identifier used to tag messages*/
    char*          db;  /**The currently active database*/
    MXS_DOWNSTREAM down;
    MXS_UPSTREAM   up;
    MXS_SESSION*   session;
    bool           was_query;   /**True if the previous routeQuery call had valid content*/
} MQ_SESSION;

bool sendMessage(void* data);

static const MXS_ENUM_VALUE trigger_values[] =
{
    {"source", TRG_SOURCE},
    {"schema", TRG_SCHEMA},
    {"object", TRG_OBJECT},
    {"all",    TRG_ALL   },
    {NULL}
};

extern "C"
{

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_FILTER_OBJECT MyObject =
        {
            createInstance,
            newSession,
            closeSession,
            freeSession,
            setDownstream,
            setUpstream,
            routeQuery,
            clientReply,
            diagnostic,
            diagnostic_json,
            getCapabilities,
            NULL,   // No destroyInstance
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_ALPHA_RELEASE,
            MXS_FILTER_VERSION,
            "A RabbitMQ query logging filter",
            "V1.0.2",
            RCAP_TYPE_CONTIGUOUS_INPUT,
            &MyObject,
            NULL,                               /* Process init. */
            NULL,                               /* Process finish. */
            NULL,                               /* Thread init. */
            NULL,                               /* Thread finish. */
            {
                {"hostname",                  MXS_MODULE_PARAM_STRING,
                 "localhost"},
                {"username",                  MXS_MODULE_PARAM_STRING,
                 "guest"},
                {"password",                  MXS_MODULE_PARAM_STRING,
                 "guest"},
                {"vhost",                     MXS_MODULE_PARAM_STRING,
                 "/"},
                {"port",                      MXS_MODULE_PARAM_COUNT,
                 "5672"},
                {"exchange",                  MXS_MODULE_PARAM_STRING,
                 "default_exchange"},
                {"key",                       MXS_MODULE_PARAM_STRING,
                 "key"},
                {"queue",                     MXS_MODULE_PARAM_STRING},
                {"ssl_client_certificate",    MXS_MODULE_PARAM_PATH,   NULL,
                 MXS_MODULE_OPT_PATH_R_OK},
                {"ssl_client_key",            MXS_MODULE_PARAM_PATH,   NULL,
                 MXS_MODULE_OPT_PATH_R_OK},
                {"ssl_CA_cert",               MXS_MODULE_PARAM_PATH,   NULL,
                 MXS_MODULE_OPT_PATH_R_OK},
                {"exchange_type",             MXS_MODULE_PARAM_STRING,
                 "direct"},
                {"logging_trigger",           MXS_MODULE_PARAM_ENUM,   "all",
                 MXS_MODULE_OPT_NONE,
                 trigger_values},
                {"logging_source_user",       MXS_MODULE_PARAM_STRING},
                {"logging_source_host",       MXS_MODULE_PARAM_STRING},
                {"logging_schema",            MXS_MODULE_PARAM_STRING},
                {"logging_object",            MXS_MODULE_PARAM_STRING},
                {"logging_log_all",           MXS_MODULE_PARAM_BOOL,
                 "false"},
                {"logging_strict",            MXS_MODULE_PARAM_BOOL,
                 "true"},
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/**
 * Internal function used to initialize the connection to
 * the RabbitMQ server. Also used to reconnect to the server
 * in case the connection fails and to redeclare exchanges
 * and queues if they are lost
 *
 */
static int init_conn(MQ_INSTANCE* my_instance)
{
    int rval = 0;
    int amqp_ok = AMQP_STATUS_OK;

    if (my_instance->use_ssl)
    {

        if ((my_instance->sock = amqp_ssl_socket_new(my_instance->conn)) != NULL)
        {
            amqp_ok = amqp_ssl_socket_set_cacert(my_instance->sock, my_instance->ssl_CA_cert);
            if (amqp_ok != AMQP_STATUS_OK)
            {
                MXS_ERROR("Failed to set CA certificate: %s", amqp_error_string2(amqp_ok));
                goto cleanup;
            }
            if ((amqp_ok = amqp_ssl_socket_set_key(my_instance->sock,
                                                   my_instance->ssl_client_cert,
                                                   my_instance->ssl_client_key)) != AMQP_STATUS_OK)
            {
                MXS_ERROR("Failed to set client certificate and key: %s", amqp_error_string2(amqp_ok));
                goto cleanup;
            }
        }
        else
        {

            amqp_ok = AMQP_STATUS_SSL_CONNECTION_FAILED;
            MXS_ERROR("SSL socket creation failed.");
            goto cleanup;
        }

        /**SSL is not used, falling back to TCP*/
    }
    else if ((my_instance->sock = amqp_tcp_socket_new(my_instance->conn)) == NULL)
    {
        MXS_ERROR("TCP socket creation failed.");
        goto cleanup;
    }

    /**Socket creation was successful, trying to open the socket*/
    amqp_ok = amqp_socket_open(my_instance->sock, my_instance->hostname, my_instance->port);
    if (amqp_ok != AMQP_STATUS_OK)
    {
        MXS_ERROR("Failed to open socket: %s", amqp_error_string2(amqp_ok));
        goto cleanup;
    }
    amqp_rpc_reply_t reply;
    reply = amqp_login(my_instance->conn,
                       my_instance->vhost,
                       0,
                       AMQP_DEFAULT_FRAME_SIZE,
                       0,
                       AMQP_SASL_METHOD_PLAIN,
                       my_instance->username,
                       my_instance->password);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        MXS_ERROR("Login to RabbitMQ server failed.");
        goto cleanup;
    }
    amqp_channel_open(my_instance->conn, my_instance->channel);
    reply = amqp_get_rpc_reply(my_instance->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        MXS_ERROR("Channel creation failed.");
        goto cleanup;
    }

    amqp_exchange_declare(my_instance->conn,
                          my_instance->channel,
                          amqp_cstring_bytes(my_instance->exchange),
                          amqp_cstring_bytes(my_instance->exchange_type),
                          false,
                          true,
#ifdef RABBITMQ_060
                          false,
                          false,
#endif
                          amqp_empty_table);

    reply = amqp_get_rpc_reply(my_instance->conn);

    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        MXS_ERROR("Exchange declaration failed,trying to redeclare the exchange.");
        if (reply.reply_type == AMQP_RESPONSE_SERVER_EXCEPTION)
        {
            if (reply.reply.id == AMQP_CHANNEL_CLOSE_METHOD)
            {
                amqp_send_method(my_instance->conn,
                                 my_instance->channel,
                                 AMQP_CHANNEL_CLOSE_OK_METHOD,
                                 NULL);
            }
            else if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD)
            {
                amqp_send_method(my_instance->conn,
                                 my_instance->channel,
                                 AMQP_CONNECTION_CLOSE_OK_METHOD,
                                 NULL);
            }

            my_instance->channel++;
            amqp_channel_open(my_instance->conn, my_instance->channel);

            amqp_exchange_delete(my_instance->conn,
                                 my_instance->channel,
                                 amqp_cstring_bytes(my_instance->exchange),
                                 0);
            amqp_exchange_declare(my_instance->conn,
                                  my_instance->channel,
                                  amqp_cstring_bytes(my_instance->exchange),
                                  amqp_cstring_bytes(my_instance->exchange_type),
                                  false,
                                  true,
#ifdef RABBITMQ_060
                                  false,
                                  false,
#endif
                                  amqp_empty_table);
            reply = amqp_get_rpc_reply(my_instance->conn);
        }
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            MXS_ERROR("Exchange redeclaration failed.");
            goto cleanup;
        }
    }

    if (my_instance->queue)
    {



        amqp_queue_declare(my_instance->conn,
                           my_instance->channel,
                           amqp_cstring_bytes(my_instance->queue),
                           0,
                           1,
                           0,
                           0,
                           amqp_empty_table);
        reply = amqp_get_rpc_reply(my_instance->conn);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            MXS_ERROR("Queue declaration failed.");
            goto cleanup;
        }


        amqp_queue_bind(my_instance->conn,
                        my_instance->channel,
                        amqp_cstring_bytes(my_instance->queue),
                        amqp_cstring_bytes(my_instance->exchange),
                        amqp_cstring_bytes(my_instance->key),
                        amqp_empty_table);
        reply = amqp_get_rpc_reply(my_instance->conn);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            MXS_ERROR("Failed to bind queue to exchange.");
            goto cleanup;
        }
    }
    rval = 1;

cleanup:

    return rval;
}

/**
 * Parse the provided string into an array of strings.
 * The caller is responsible for freeing all the allocated memory.
 * If an error occurred no memory is allocated and the size of the array is set to zero.
 * @param str String to parse
 * @param tok Token string containing delimiting characters
 * @param szstore Address where to store the size of the array after parsing
 * @return The array containing the parsed string
 */
char** parse_optstr(const char* str, const char* tok, int* szstore)
{
    char tmp[strlen(str) + 1];
    strcpy(tmp, str);
    char* lasts, * tk = tmp;
    int i = 0, size = 1;


    while ((tk = strpbrk(tk + 1, tok)))
    {
        size++;
    }

    char** arr = static_cast<char**>(MXS_MALLOC(sizeof(char*) * size));
    MXS_ABORT_IF_NULL(arr);

    *szstore = size;
    tk = strtok_r(tmp, tok, &lasts);

    while (tk && i < size)
    {
        arr[i++] = MXS_STRDUP_A(tk);
        tk = strtok_r(NULL, tok, &lasts);
    }

    return arr;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    MQ_INSTANCE* my_instance = static_cast<MQ_INSTANCE*>(MXS_CALLOC(1, sizeof(MQ_INSTANCE)));

    if (my_instance)
    {
        pthread_mutex_init(&my_instance->rconn_lock, NULL);
        pthread_mutex_init(&my_instance->msg_lock, NULL);
        uid_gen = 0;

        if ((my_instance->conn = amqp_new_connection()) == NULL)
        {
            MXS_FREE(my_instance);
            return NULL;
        }

        my_instance->channel = 1;
        my_instance->last_rconn = time(NULL);
        my_instance->conn_stat = AMQP_STATUS_OK;
        my_instance->rconn_intv = 1;

        my_instance->port = config_get_integer(params, "port");
        my_instance->trgtype =
            static_cast<log_trigger_t>(config_get_enum(params, "logging_trigger", trigger_values));
        my_instance->log_all = config_get_bool(params, "logging_log_all");
        my_instance->strict_logging = config_get_bool(params, "logging_strict");
        my_instance->hostname = MXS_STRDUP_A(config_get_string(params, "hostname"));
        my_instance->username = MXS_STRDUP_A(config_get_string(params, "username"));
        my_instance->password = MXS_STRDUP_A(config_get_string(params, "password"));
        my_instance->vhost = MXS_STRDUP_A(config_get_string(params, "vhost"));
        my_instance->exchange = MXS_STRDUP_A(config_get_string(params, "exchange"));
        my_instance->key = MXS_STRDUP_A(config_get_string(params, "key"));
        my_instance->exchange_type = MXS_STRDUP_A(config_get_string(params, "exchange_type"));
        my_instance->queue = config_copy_string(params, "queue");
        my_instance->ssl_client_cert = config_copy_string(params, "ssl_client_certificate");
        my_instance->ssl_client_key = config_copy_string(params, "ssl_client_key");
        my_instance->ssl_CA_cert = config_copy_string(params, "ssl_CA_cert");

        if (my_instance->trgtype & TRG_SOURCE)
        {
            my_instance->src_trg = (SRC_TRIG*)MXS_CALLOC(1, sizeof(SRC_TRIG));
            MXS_ABORT_IF_NULL(my_instance->src_trg);
        }

        if (my_instance->trgtype & TRG_SCHEMA)
        {
            my_instance->shm_trg = (SHM_TRIG*)MXS_CALLOC(1, sizeof(SHM_TRIG));
            MXS_ABORT_IF_NULL(my_instance->shm_trg);
        }

        if (my_instance->trgtype & TRG_OBJECT)
        {
            my_instance->obj_trg = (OBJ_TRIG*)MXS_CALLOC(1, sizeof(OBJ_TRIG));
            MXS_ABORT_IF_NULL(my_instance->obj_trg);
        }

        MXS_CONFIG_PARAMETER* p = config_get_param(params, "logging_source_user");

        if (p && my_instance->src_trg)
        {
            my_instance->src_trg->user = parse_optstr(p->value, ",", &my_instance->src_trg->usize);
        }

        p = config_get_param(params, "logging_source_host");

        if (p && my_instance->src_trg)
        {
            my_instance->src_trg->host = parse_optstr(p->value, ",", &my_instance->src_trg->hsize);
        }

        p = config_get_param(params, "logging_schema");

        if (p && my_instance->shm_trg)
        {
            my_instance->shm_trg->objects = parse_optstr(p->value, ",", &my_instance->shm_trg->size);
        }

        p = config_get_param(params, "logging_object");

        if (p && my_instance->obj_trg)
        {
            my_instance->obj_trg->objects = parse_optstr(p->value, ",", &my_instance->obj_trg->size);
        }

        my_instance->use_ssl = my_instance->ssl_client_cert
            && my_instance->ssl_client_key
            && my_instance->ssl_CA_cert;

        if (my_instance->use_ssl)
        {
            /**Assume the underlying SSL library is already initialized*/
            amqp_set_initialize_ssl_library(0);
        }

        /**Connect to the server*/
        init_conn(my_instance);

        char taskname[512];
        snprintf(taskname, 511, "mqtask%d", atomic_add(&hktask_id, 1));
        hktask_add(taskname, sendMessage, (void*)my_instance, 5);
    }

    return (MXS_FILTER*)my_instance;
}

/**
 * Declares a persistent, non-exclusive and non-passive queue that
 * auto-deletes after all the messages have been consumed.
 * @param my_session MQ_SESSION instance used to declare the queue
 * @param qname Name of the queue to be declared
 * @return Returns 0 if an error occurred, 1 if successful
 */
int declareQueue(MQ_INSTANCE* my_instance, MQ_SESSION* my_session, char* qname)
{
    int success = 1;
    amqp_rpc_reply_t reply;

    pthread_mutex_lock(&my_instance->rconn_lock);

    amqp_queue_declare(my_instance->conn,
                       my_instance->channel,
                       amqp_cstring_bytes(qname),
                       0,
                       1,
                       0,
                       1,
                       amqp_empty_table);
    reply = amqp_get_rpc_reply(my_instance->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        success = 0;
        MXS_ERROR("Queue declaration failed.");
    }

    amqp_queue_bind(my_instance->conn,
                    my_instance->channel,
                    amqp_cstring_bytes(qname),
                    amqp_cstring_bytes(my_instance->exchange),
                    amqp_cstring_bytes(my_session->uid),
                    amqp_empty_table);
    reply = amqp_get_rpc_reply(my_instance->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        success = 0;
        MXS_ERROR("Failed to bind queue to exchange.");
    }
    pthread_mutex_unlock(&my_instance->rconn_lock);
    return success;
}

/**
 * Broadcasts a message on the message stack to the RabbitMQ server
 * and frees the allocated memory if successful. This function is only called by
 * the housekeeper thread.
 * @param data MQfilter instance
 */
bool sendMessage(void* data)
{
    MQ_INSTANCE* instance = (MQ_INSTANCE*) data;
    mqmessage* tmp;
    int err_num = AMQP_STATUS_OK;

    pthread_mutex_lock(&instance->rconn_lock);
    if (instance->conn_stat != AMQP_STATUS_OK)
    {
        if (difftime(time(NULL), instance->last_rconn) > instance->rconn_intv)
        {
            instance->last_rconn = time(NULL);

            if (init_conn(instance))
            {
                instance->rconn_intv = 1.0;
                instance->conn_stat = AMQP_STATUS_OK;
            }
            else
            {
                instance->rconn_intv += 5.0;
                MXS_ERROR("Failed to reconnect to the MQRabbit server ");
            }
        }
        err_num = instance->conn_stat;
    }
    pthread_mutex_unlock(&instance->rconn_lock);

    if (err_num != AMQP_STATUS_OK)
    {
        /** No connection to the broker */
        return true;
    }

    pthread_mutex_lock(&instance->msg_lock);
    tmp = instance->messages;

    if (tmp == NULL)
    {
        pthread_mutex_unlock(&instance->msg_lock);
        return true;
    }

    instance->messages = instance->messages->next;
    pthread_mutex_unlock(&instance->msg_lock);

    while (tmp)
    {
        err_num = amqp_basic_publish(instance->conn,
                                     instance->channel,
                                     amqp_cstring_bytes(instance->exchange),
                                     amqp_cstring_bytes(instance->key),
                                     0,
                                     0,
                                     tmp->prop,
                                     amqp_cstring_bytes(tmp->msg));

        pthread_mutex_lock(&instance->rconn_lock);
        instance->conn_stat = err_num;
        pthread_mutex_unlock(&instance->rconn_lock);

        if (err_num == AMQP_STATUS_OK)
        {
            /**Message was sent successfully*/
            MXS_FREE(tmp->prop);
            MXS_FREE(tmp->msg);
            MXS_FREE(tmp);

            atomic_add(&instance->stats.n_sent, 1);
            atomic_add(&instance->stats.n_queued, -1);
            pthread_mutex_lock(&instance->msg_lock);
            tmp = instance->messages;

            if (tmp == NULL)
            {
                pthread_mutex_unlock(&instance->msg_lock);
                return true;
            }

            instance->messages = instance->messages->next;
            pthread_mutex_unlock(&instance->msg_lock);
        }
        else
        {
            pthread_mutex_lock(&instance->msg_lock);
            tmp->next = instance->messages;
            instance->messages = tmp;
            pthread_mutex_unlock(&instance->msg_lock);
            return true;
        }
    }

    return true;
}

/**
 * Push a new message on the stack to be broadcasted later.
 * The message assumes ownership of the memory allocated to the message content and properties.
 * @param prop Message properties
 * @param msg Message content
 */
void pushMessage(MQ_INSTANCE* instance, amqp_basic_properties_t* prop, char* msg)
{

    mqmessage* newmsg = static_cast<mqmessage*>(MXS_CALLOC(1, sizeof(mqmessage)));
    if (newmsg)
    {
        newmsg->msg = msg;
        newmsg->prop = prop;
    }
    else
    {
        MXS_FREE(prop);
        MXS_FREE(msg);
        return;
    }

    pthread_mutex_lock(&instance->msg_lock);

    newmsg->next = instance->messages;
    instance->messages = newmsg;

    pthread_mutex_unlock(&instance->msg_lock);

    atomic_add(&instance->stats.n_msg, 1);
    atomic_add(&instance->stats.n_queued, 1);
}

/**
 * Associate a new session with this instance of the filter and opens
 * a connection to the server and prepares the exchange and the queue for use.
 *
 *
 * @param instance      The filter instance data
 * @param session       The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    const char* db = mxs_mysql_get_current_db(session);
    char* my_db = NULL;

    if (*db)
    {
        my_db = MXS_STRDUP(my_db);
        if (!my_db)
        {
            return NULL;
        }
    }

    MQ_SESSION* my_session;

    if ((my_session = static_cast<MQ_SESSION*>(MXS_CALLOC(1, sizeof(MQ_SESSION)))) != NULL)
    {
        my_session->was_query = false;
        my_session->uid = NULL;
        my_session->session = session;
        my_session->db = my_db;
    }
    else
    {
        MXS_FREE(my_db);
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the MQ filter we do nothing.
 *
 * @param instance      The filter instance data
 * @param session       The session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
}

/**
 * Free the memory associated with the session
 *
 * @param instance      The filter instance
 * @param session       The filter session
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    MQ_SESSION* my_session = (MQ_SESSION*) session;
    MXS_FREE(my_session->uid);
    MXS_FREE(my_session->db);
    MXS_FREE(my_session);
    return;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance      The filter instance data
 * @param session       The filter session
 * @param downstream    The downstream filter or router.
 */
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    MQ_SESSION* my_session = (MQ_SESSION*) session;
    my_session->down = *downstream;
}

static void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_UPSTREAM* upstream)
{
    MQ_SESSION* my_session = (MQ_SESSION*) session;
    my_session->up = *upstream;
}

/**
 * Generates a unique key using a number of unique unsigned integers.
 * @param array The array that is used
 * @param size Size of the array
 */
void genkey(char* array, int size)
{
    int i = 0;
    for (i = 0; i < size; i += 4)
    {
        sprintf(array + i, "%04x", atomic_add(&uid_gen, 1));
    }
    sprintf(array + i, "%0*x", size - i, atomic_add(&uid_gen, 1));
}

/**
 * Calculated the length of the SQL packet.
 * @param c Pointer to the first byte of a packet
 * @return The length of the packet
 */
unsigned int pktlen(void* c)
{
    unsigned char* ptr = (unsigned char*) c;
    unsigned int plen = *ptr;
    plen += (*++ptr << 8);
    plen += (*++ptr << 8);
    return plen;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * The function checks whether required logging trigger conditions are met and if so,
 * tries to extract a SQL query out of the query buffer, canonize the query, add
 * a timestamp to it and publish the resulting string on the exchange.
 * The message is tagged with an unique identifier and the clientReply will
 * use the same identifier for the reply from the backend to form a query-reply pair.
 *
 * @param instance      The filter instance data
 * @param session       The filter session
 * @param queue         The query data
 */
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    MQ_SESSION* my_session = (MQ_SESSION*) session;
    MQ_INSTANCE* my_instance = (MQ_INSTANCE*) instance;
    char* ptr, t_buf[128], * combined, * canon_q;
    const char* sesshost, * sessusr;
    bool success = false, src_ok = false, schema_ok = false, obj_ok = false;
    int length, i, j, dbcount = 0;
    char** sesstbls;
    unsigned int plen = 0;
    amqp_basic_properties_t* prop;

    /**The user is changing databases*/
    if (*(static_cast<char*>(queue->start) + 4) == 0x02)
    {
        if (my_session->db)
        {
            MXS_FREE(my_session->db);
        }
        plen = pktlen(queue->start);
        my_session->db = static_cast<char*>(MXS_CALLOC(plen, sizeof(char)));
        MXS_ABORT_IF_NULL(my_session->db);
        memcpy(my_session->db, static_cast<char*>(queue->start) + 5, plen - 1);
    }

    if (modutil_is_SQL(queue))
    {
        success = true;

        if (my_instance->trgtype == TRG_ALL)
        {
            MXS_INFO("Trigger is TRG_ALL");
            schema_ok = true;
            src_ok = true;
            obj_ok = true;
            goto validate_triggers;
        }

        if (my_instance->trgtype & TRG_SOURCE && my_instance->src_trg)
        {

            sessusr = session_get_user(my_session->session);
            sesshost = session_get_remote(my_session->session);

            /**Username was configured*/
            if (my_instance->src_trg->usize > 0)
            {
                for (i = 0; i < my_instance->src_trg->usize; i++)
                {
                    if (strcmp(my_instance->src_trg->user[i], sessusr) == 0)
                    {
                        MXS_INFO("Trigger is TRG_SOURCE: user: %s = %s",
                                 my_instance->src_trg->user[i],
                                 sessusr);
                        src_ok = true;
                        break;
                    }
                }
            }

            /**If username was not matched, try to match hostname*/

            if (!src_ok && my_instance->src_trg->hsize > 0)
            {

                for (i = 0; i < my_instance->src_trg->hsize; i++)
                {

                    if (strcmp(my_instance->src_trg->host[i], sesshost) == 0)
                    {
                        MXS_INFO("Trigger is TRG_SOURCE: host: %s = %s",
                                 my_instance->src_trg->host[i],
                                 sesshost);
                        src_ok = true;
                        break;
                    }
                }
            }

            if (src_ok && !my_instance->strict_logging)
            {
                schema_ok = true;
                obj_ok = true;
                goto validate_triggers;
            }
        }
        else
        {
            src_ok = true;
        }

        if (my_instance->trgtype & TRG_SCHEMA && my_instance->shm_trg)
        {
            int tbsz = 0, z;
            char** tblnames = qc_get_table_names(queue, &tbsz, true);
            char* tmp;
            bool all_remotes = true;

            for (z = 0; z < tbsz; z++)
            {
                if ((tmp = strchr(tblnames[z], '.')) != NULL)
                {
                    char* lasts;
                    tmp = strtok_r(tblnames[z], ".", &lasts);
                    for (i = 0; i < my_instance->shm_trg->size; i++)
                    {

                        if (strcmp(tmp, my_instance->shm_trg->objects[i]) == 0)
                        {

                            MXS_INFO("Trigger is TRG_SCHEMA: %s = %s",
                                     tmp,
                                     my_instance->shm_trg->objects[i]);

                            schema_ok = true;
                            break;
                        }
                    }
                }
                else
                {
                    all_remotes = false;
                }
                MXS_FREE(tblnames[z]);
            }
            MXS_FREE(tblnames);

            if (!schema_ok && !all_remotes && my_session->db && strlen(my_session->db) > 0)
            {

                for (i = 0; i < my_instance->shm_trg->size; i++)
                {

                    if (strcmp(my_session->db, my_instance->shm_trg->objects[i]) == 0)
                    {

                        MXS_INFO("Trigger is TRG_SCHEMA: %s = %s",
                                 my_session->db,
                                 my_instance->shm_trg->objects[i]);

                        schema_ok = true;
                        break;
                    }
                }
            }

            if (schema_ok && !my_instance->strict_logging)
            {
                src_ok = true;
                obj_ok = true;
                goto validate_triggers;
            }
        }
        else
        {
            schema_ok = true;
        }


        if (my_instance->trgtype & TRG_OBJECT && my_instance->obj_trg)
        {

            sesstbls = qc_get_table_names(queue, &dbcount, false);

            for (j = 0; j < dbcount; j++)
            {
                char* tbnm = NULL;

                if ((strchr(sesstbls[j], '.')) != NULL)
                {
                    char* lasts;
                    tbnm = strtok_r(sesstbls[j], ".", &lasts);
                    tbnm = strtok_r(NULL, ".", &lasts);
                }
                else
                {
                    tbnm = sesstbls[j];
                }


                for (i = 0; i < my_instance->obj_trg->size; i++)
                {


                    if (!strcmp(tbnm, my_instance->obj_trg->objects[i]))
                    {
                        obj_ok = true;
                        MXS_INFO("Trigger is TRG_OBJECT: %s = %s",
                                 my_instance->obj_trg->objects[i],
                                 sesstbls[j]);
                        break;
                    }
                }
            }
            if (dbcount > 0)
            {
                for (j = 0; j < dbcount; j++)
                {
                    MXS_FREE(sesstbls[j]);
                }
                MXS_FREE(sesstbls);
                dbcount = 0;
            }

            if (obj_ok && !my_instance->strict_logging)
            {
                src_ok = true;
                schema_ok = true;
                goto validate_triggers;
            }
        }
        else
        {
            obj_ok = true;
        }


validate_triggers:

        if (src_ok && schema_ok && obj_ok)
        {

            /**
             * Something matched the trigger, log the query
             */

            MXS_INFO("Routing message to: [%s]:%d %s as %s/%s, exchange: %s<%s> key:%s queue:%s",
                     my_instance->hostname,
                     my_instance->port,
                     my_instance->vhost,
                     my_instance->username,
                     my_instance->password,
                     my_instance->exchange,
                     my_instance->exchange_type,
                     my_instance->key,
                     my_instance->queue);

            if (my_session->uid == NULL)
            {

                my_session->uid = static_cast<char*>(MXS_CALLOC(33, sizeof(char)));

                if (my_session->uid)
                {
                    genkey(my_session->uid, 32);
                }
            }

            if (modutil_extract_SQL(queue, &ptr, &length))
            {

                my_session->was_query = true;
                prop = static_cast<amqp_basic_properties_t*>(MXS_MALLOC(sizeof(amqp_basic_properties_t)));
                if (prop)
                {
                    prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG
                        | AMQP_BASIC_DELIVERY_MODE_FLAG
                        | AMQP_BASIC_MESSAGE_ID_FLAG
                        | AMQP_BASIC_CORRELATION_ID_FLAG;
                    prop->content_type = amqp_cstring_bytes("text/plain");
                    prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
                    prop->correlation_id = amqp_cstring_bytes(my_session->uid);
                    prop->message_id = amqp_cstring_bytes("query");
                }

                if (success)
                {

                    /**Try to convert to a canonical form and use the plain query if unsuccessful*/
                    if ((canon_q = qc_get_canonical(queue)) == NULL)
                    {
                        MXS_ERROR("Cannot form canonical query.");
                    }
                }

                memset(t_buf, 0, 128);
                sprintf(t_buf, "%lu|", (unsigned long) time(NULL));

                int qlen = strnlen(canon_q, length) + strnlen(t_buf, 128);
                combined = static_cast<char*>(MXS_MALLOC((qlen + 1) * sizeof(char)));
                MXS_ABORT_IF_NULL(combined);
                strcpy(combined, t_buf);
                strncat(combined, canon_q, length);

                pushMessage(my_instance, prop, combined);
                MXS_FREE(canon_q);
            }
        }

        /** Pass the query downstream */
    }

    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session,
                                       queue);
}

/**
 * Converts a length-encoded integer to an unsigned integer as defined by the
 * MySQL manual.
 * @param c Pointer to the first byte of a length-encoded integer
 * @return The value converted to a standard unsigned integer
 */
unsigned int leitoi(unsigned char* c)
{
    unsigned char* ptr = c;
    unsigned int sz = *ptr;
    if (*ptr < 0xfb)
    {
        return sz;
    }
    if (*ptr == 0xfc)
    {
        sz = *++ptr;
        sz += (*++ptr << 8);
    }
    else if (*ptr == 0xfd)
    {
        sz = *++ptr;
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
    }
    else
    {
        sz = *++ptr;
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
        sz += (*++ptr << 8);
    }
    return sz;
}

/**
 * Converts a length-encoded integer into a standard unsigned integer
 * and advances the pointer to the next unrelated byte.
 *
 * @param c Pointer to the first byte of a length-encoded integer
 */
unsigned int consume_leitoi(unsigned char** c)
{
    unsigned int rval = leitoi(*c);
    if (**c == 0xfc)
    {
        *c += 3;
    }
    else if (**c == 0xfd)
    {
        *c += 4;
    }
    else if (**c == 0xfe)
    {
        *c += 9;
    }
    else
    {
        *c += 1;
    }
    return rval;
}

/**
 * Converts length-encoded strings to character strings and advanced
 * the pointer to the next unrelated byte.
 * The caller is responsible for freeing the allocated memory.
 * @param c Pointer to the first byte of a valid packet.
 * @return The newly allocated string or NULL of an error occurred
 */
char* consume_lestr(unsigned char** c)
{
    unsigned int slen = consume_leitoi(c);
    char* str = static_cast<char*>(MXS_CALLOC((slen + 1), sizeof(char)));
    if (str)
    {
        memcpy(str, *c, slen);
        *c += slen;
    }
    return str;
}

/**
 * Checks whether the packet is an EOF packet.
 * @param p Pointer to the first byte of a packet
 * @return 1 if the packet is an EOF packet and 0 if it is not
 */
unsigned int is_eof(void* p)
{
    unsigned char* ptr = (unsigned char*) p;
    return *(ptr) == 0x05 && *(ptr + 1) == 0x00 && *(ptr + 2) == 0x00 && *(ptr + 4) == 0xfe;
}

/**
 * The clientReply entry point. This is passed the response buffer
 * to which the filter should be applied. Once processed the
 * query is passed to the upstream component
 * (filter or router) in the filter chain.
 *
 * The function tries to extract a SQL query response out of the response buffer,
 * adds a timestamp to it and publishes the resulting string on the exchange.
 * The message is tagged with the same identifier that the query was.
 *
 * @param instance      The filter instance data
 * @param session       The filter session
 * @param reply         The response data
 */
static int clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* reply)
{
    MQ_SESSION* my_session = (MQ_SESSION*) session;
    MQ_INSTANCE* my_instance = (MQ_INSTANCE*) instance;
    char t_buf[128], * combined;
    unsigned int pkt_len = pktlen(reply->sbuf->data), offset = 0;
    amqp_basic_properties_t* prop;

    if (my_session->was_query)
    {

        int packet_ok = 0, was_last = 0;

        my_session->was_query = false;

        if (pkt_len > 0)
        {
            if ((prop = static_cast<amqp_basic_properties_t*>(MXS_MALLOC(sizeof(amqp_basic_properties_t)))))
            {
                prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG
                    | AMQP_BASIC_DELIVERY_MODE_FLAG
                    | AMQP_BASIC_MESSAGE_ID_FLAG
                    | AMQP_BASIC_CORRELATION_ID_FLAG;
                prop->content_type = amqp_cstring_bytes("text/plain");
                prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
                prop->correlation_id = amqp_cstring_bytes(my_session->uid);
                prop->message_id = amqp_cstring_bytes("reply");
            }

            combined = static_cast<char*>(MXS_CALLOC(GWBUF_LENGTH(reply) + 256, sizeof(char)));
            MXS_ABORT_IF_NULL(combined);

            memset(t_buf, 0, 128);
            sprintf(t_buf, "%lu|", (unsigned long) time(NULL));


            memcpy(combined + offset, t_buf, strnlen(t_buf, 40));
            offset += strnlen(t_buf, 40);

            if (*(reply->sbuf->data + 4) == 0x00)
            {
                /**OK packet*/
                unsigned int aff_rows = 0, l_id = 0, s_flg = 0, wrn = 0;
                unsigned char* ptr = (unsigned char*) (reply->sbuf->data + 5);
                pkt_len = pktlen(reply->sbuf->data);
                aff_rows = consume_leitoi(&ptr);
                l_id = consume_leitoi(&ptr);
                s_flg |= *ptr++;
                s_flg |= (*ptr++ << 8);
                wrn |= *ptr++;
                wrn |= (*ptr++ << 8);
                sprintf(combined + offset,
                        "OK - affected_rows: %d "
                        " last_insert_id: %d "
                        " status_flags: %#0x "
                        " warnings: %d ",
                        aff_rows,
                        l_id,
                        s_flg,
                        wrn);
                offset += strnlen(combined, GWBUF_LENGTH(reply) + 256) - offset;

                if (pkt_len > 7)
                {
                    int plen = consume_leitoi(&ptr);
                    if (plen > 0)
                    {
                        sprintf(combined + offset, " message: %.*s\n", plen, ptr);
                    }
                }

                packet_ok = 1;
                was_last = 1;
            }
            else if (*(reply->sbuf->data + 4) == 0xff)
            {
                /**ERR packet*/
                sprintf(combined + offset,
                        "ERROR - message: %.*s",
                        (int) (static_cast<unsigned char*>(reply->end) - (reply->sbuf->data + 13)),
                        (char*) reply->sbuf->data + 13);
                packet_ok = 1;
                was_last = 1;
            }
            else if (*(reply->sbuf->data + 4) == 0xfb)
            {
                /**LOCAL_INFILE request packet*/
                unsigned char* rset = (unsigned char*) reply->sbuf->data;
                strcpy(combined + offset, "LOCAL_INFILE: ");
                strncat(combined + offset, (const char*) rset + 5, pktlen(rset));
                packet_ok = 1;
                was_last = 1;
            }
            else
            {
                /**Result set*/
                unsigned char* rset = (unsigned char*) (reply->sbuf->data + 4);
                char* tmp;
                unsigned int col_cnt = consume_leitoi(&rset);

                tmp = static_cast<char*>(MXS_CALLOC(256, sizeof(char)));
                MXS_ABORT_IF_NULL(tmp);
                sprintf(tmp, "Columns: %d", col_cnt);
                memcpy(combined + offset, tmp, strnlen(tmp, 256));
                offset += strnlen(tmp, 256);
                memcpy(combined + offset, "\n", 1);
                offset++;
                MXS_FREE(tmp);

                packet_ok = 1;
                was_last = 1;
            }
            if (packet_ok)
            {

                pushMessage(my_instance, prop, combined);

                if (was_last)
                {

                    /**Successful reply received and sent, releasing uid*/

                    MXS_FREE(my_session->uid);
                    my_session->uid = NULL;
                }
            }
        }
    }

    return my_session->up.clientReply(my_session->up.instance,
                                      my_session->up.session,
                                      reply);
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param       instance        The filter instance
 * @param       fsession        Filter session, may be NULL
 * @param       dcb             The DCB for diagnostic output
 */
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    MQ_INSTANCE* my_instance = (MQ_INSTANCE*) instance;

    if (my_instance)
    {
        dcb_printf(dcb,
                   "Connecting to [%s]:%d as '%s'.\nVhost: %s\tExchange: %s\nKey: %s\tQueue: %s\n\n",
                   my_instance->hostname,
                   my_instance->port,
                   my_instance->username,
                   my_instance->vhost,
                   my_instance->exchange,
                   my_instance->key,
                   my_instance->queue
                   );
        dcb_printf(dcb,
                   "%-16s%-16s%-16s\n",
                   "Messages",
                   "Queued",
                   "Sent");
        dcb_printf(dcb,
                   "%-16d%-16d%-16d\n",
                   my_instance->stats.n_msg,
                   my_instance->stats.n_queued,
                   my_instance->stats.n_sent);
    }
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param       instance        The filter instance
 * @param       fsession        Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    MQ_INSTANCE* my_instance = (MQ_INSTANCE*)instance;
    json_t* rval = json_object();

    json_object_set_new(rval, "host", json_string(my_instance->hostname));
    json_object_set_new(rval, "user", json_string(my_instance->username));
    json_object_set_new(rval, "vhost", json_string(my_instance->vhost));
    json_object_set_new(rval, "exchange", json_string(my_instance->exchange));
    json_object_set_new(rval, "key", json_string(my_instance->key));
    json_object_set_new(rval, "queue", json_string(my_instance->queue));

    json_object_set_new(rval, "port", json_integer(my_instance->port));
    json_object_set_new(rval, "messages", json_integer(my_instance->stats.n_msg));
    json_object_set_new(rval, "queued", json_integer(my_instance->stats.n_queued));
    json_object_set_new(rval, "sent", json_integer(my_instance->stats.n_sent));

    return rval;
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}
