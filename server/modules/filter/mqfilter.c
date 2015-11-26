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
 * Copyright MariaDB Corporation Ab 2014
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
 * To use a SSL connection the CA certificate, the client certificate and the client public key must be provided.
 * By default this filter uses a TCP connection.
 *@verbatim
 * The options for this filter are:
 *
 *	logging_trigger	Set the logging level
 *	logging_strict	Sets whether to trigger when any of the parameters match or only if all parameters match
 *	logging_log_all	Log only SELECT, UPDATE, DELETE and INSERT or all possible queries
 * 	hostname	The server hostname where the messages are sent
 * 	port		Port to send the messages to
 * 	username	Server login username
 * 	password 	Server login password
 * 	vhost		The virtual host location on the server, where the messages are sent
 * 	exchange	The name of the exchange
 * 	exchange_type	The type of the exchange, defaults to direct
 * 	key		The routing key used when sending messages to the exchange
 * 	queue		The queue that will be bound to the used exchange
 * 	ssl_CA_cert	Path to the CA certificate in PEM format
 * 	ssl_client_cert Path to the client cerificate in PEM format
 * 	ssl_client_key	Path to the client public key in PEM format
 *
 * The logging trigger levels are:
 *	all	Log everything
 *	source	Trigger on statements originating from a particular source (database user and host combination)
 *	schema	Trigger on a certain schema
 *	object	Trigger on a particular database object (table or view)
 *@endverbatim
 * See the individual struct documentations for logging trigger parameters
 */
#include <my_config.h>
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <atomic.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <amqp_tcp_socket.h>
#include <amqp_ssl_socket.h>
#include <mysql_client_server_protocol.h>
#include <log_manager.h>
#include <query_classifier.h>
#include <spinlock.h>
#include <session.h>
#include <plugin.h>
#include <housekeeper.h>

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_ALPHA_RELEASE,
    FILTER_VERSION,
    "A RabbitMQ query logging filter"
};

static char *version_str = "V1.0.2";
static int uid_gen;
static int hktask_id = 0;
/*
 * The filter entry points
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static void setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static int clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject =
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
};

/**
 *Structure used to store messages and their properties.
 */
typedef struct mqmessage_t
{
    amqp_basic_properties_t *prop;
    char *msg;
    struct mqmessage_t *next;
} mqmessage;

/**
 *Logging trigger levels
 */
enum log_trigger_t
{
    TRG_ALL = 0x00,
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
 *	logging_source_user	Comma-separated list of usernames to log
 *	logging_source_host	Comma-separated list of hostnames to log
 * @endverbatim
 */
typedef struct source_trigger_t
{
    char** user;
    int usize;
    char** host;
    int hsize;
} SRC_TRIG;

/**
 * Schema logging trigger
 *
 * Log only those queries that target a specific database.
 *
 * Trigger options:
 *	logging_schema	Comma-separated list of databases
 */
typedef struct schema_trigger_t
{
    char** objects;
    int size;
} SHM_TRIG;

/**
 * Database object logging trigger
 *
 * Log only those queries that target specific database objects.
 *@verbatim
 * Trigger options:
 *	logging_object	Comma-separated list of database objects
 *@endverbatim
 */
typedef struct object_trigger_t
{
    char** objects;
    int size;
} OBJ_TRIG;

/**
 * Statistics for the mqfilter.
 */
typedef struct mqstats_t
{
    int n_msg; /*< Total number of messages */
    int n_sent; /*< Number of sent messages */
    int n_queued; /*< Number of unsent messages */
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
    int port;
    char *hostname;
    char *username;
    char *password;
    char *vhost;
    char *exchange;
    char *exchange_type;
    char *key;
    char *queue;
    bool use_ssl;
    bool log_all;
    bool strict_logging;
    char *ssl_CA_cert;
    char *ssl_client_cert;
    char *ssl_client_key;
    amqp_connection_state_t conn; /**The connection object*/
    amqp_socket_t* sock; /**The currently active socket*/
    amqp_channel_t channel; /**The current channel in use*/
    int conn_stat; /**state of the connection to the server*/
    int rconn_intv; /**delay for reconnects, in seconds*/
    time_t last_rconn; /**last reconnect attempt*/
    SPINLOCK rconn_lock;
    SPINLOCK msg_lock;
    mqmessage* messages;
    enum log_trigger_t trgtype;
    SRC_TRIG* src_trg;
    SHM_TRIG* shm_trg;
    OBJ_TRIG* obj_trg;
    MQSTATS stats;
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
    char* uid; /**Unique identifier used to tag messages*/
    char* db; /**The currently active database*/
    DOWNSTREAM down;
    UPSTREAM up;
    SESSION* session;
    bool was_query; /**True if the previous routeQuery call had valid content*/
} MQ_SESSION;

void sendMessage(void* data);

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
 * Internal function used to initialize the connection to
 * the RabbitMQ server. Also used to reconnect to the server
 * in case the connection fails and to redeclare exchanges
 * and queues if they are lost
 *
 */
static int
init_conn(MQ_INSTANCE *my_instance)
{
    int rval = 0;
    int amqp_ok = AMQP_STATUS_OK;

    if (my_instance->use_ssl)
    {

        if ((my_instance->sock = amqp_ssl_socket_new(my_instance->conn)) != NULL)
        {

            if ((amqp_ok = amqp_ssl_socket_set_cacert(my_instance->sock, my_instance->ssl_CA_cert)) != AMQP_STATUS_OK)
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
    if ((amqp_ok = amqp_socket_open(my_instance->sock, my_instance->hostname, my_instance->port)) != AMQP_STATUS_OK)
    {
        MXS_ERROR("Failed to open socket: %s", amqp_error_string2(amqp_ok));
        goto cleanup;
    }
    amqp_rpc_reply_t reply;
    reply = amqp_login(my_instance->conn, my_instance->vhost, 0, AMQP_DEFAULT_FRAME_SIZE, 0, AMQP_SASL_METHOD_PLAIN, my_instance->username, my_instance->password);
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

    amqp_exchange_declare(my_instance->conn, my_instance->channel,
                          amqp_cstring_bytes(my_instance->exchange),
                          amqp_cstring_bytes(my_instance->exchange_type),
                          false, true,
#ifdef RABBITMQ_060
                          false, false,
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
                amqp_send_method(my_instance->conn, my_instance->channel, AMQP_CHANNEL_CLOSE_OK_METHOD, NULL);
            }
            else if (reply.reply.id == AMQP_CONNECTION_CLOSE_METHOD)
            {
                amqp_send_method(my_instance->conn, my_instance->channel, AMQP_CONNECTION_CLOSE_OK_METHOD, NULL);
            }

            my_instance->channel++;
            amqp_channel_open(my_instance->conn, my_instance->channel);

            amqp_exchange_delete(my_instance->conn, my_instance->channel, amqp_cstring_bytes(my_instance->exchange), 0);
            amqp_exchange_declare(my_instance->conn, my_instance->channel,
                                  amqp_cstring_bytes(my_instance->exchange),
                                  amqp_cstring_bytes(my_instance->exchange_type),
                                  false, true,
#ifdef RABBITMQ_060
                                  false, false,
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



        amqp_queue_declare(my_instance->conn, my_instance->channel,
                           amqp_cstring_bytes(my_instance->queue),
                           0, 1, 0, 0,
                           amqp_empty_table);
        reply = amqp_get_rpc_reply(my_instance->conn);
        if (reply.reply_type != AMQP_RESPONSE_NORMAL)
        {
            MXS_ERROR("Queue declaration failed.");
            goto cleanup;
        }


        amqp_queue_bind(my_instance->conn, my_instance->channel,
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
char** parse_optstr(char* str, char* tok, int* szstore)
{
    char *lasts, *tk = str;
    char **arr;
    int i = 0, size = 1;


    while ((tk = strpbrk(tk + 1, tok)))
    {
        size++;
    }

    arr = malloc(sizeof(char*)*size);

    if (arr == NULL)
    {
        MXS_ERROR("Cannot allocate enough memory.");
        *szstore = 0;
        return NULL;
    }

    *szstore = size;
    tk = strtok_r(str, tok, &lasts);
    while (tk && i < size)
    {
        arr[i++] = strdup(tk);
        tk = strtok_r(NULL, tok, &lasts);
    }
    return arr;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param options	The options for this filter
 *
 * @return The instance data for this new instance
 */
static FILTER *
createInstance(char **options, FILTER_PARAMETER **params)
{
    MQ_INSTANCE *my_instance;
    int paramcount = 0, parammax = 64, i = 0, x = 0, arrsize = 0;
    FILTER_PARAMETER** paramlist;
    char** arr;
    char taskname[512];

    if ((my_instance = calloc(1, sizeof(MQ_INSTANCE))))
    {
        spinlock_init(&my_instance->rconn_lock);
        spinlock_init(&my_instance->msg_lock);
        uid_gen = 0;
        paramlist = malloc(sizeof(FILTER_PARAMETER*)*64);

        if ((my_instance->conn = amqp_new_connection()) == NULL)
        {
            return NULL;
        }
        my_instance->channel = 1;
        my_instance->last_rconn = time(NULL);
        my_instance->conn_stat = AMQP_STATUS_OK;
        my_instance->rconn_intv = 1;
        my_instance->port = 5672;
        my_instance->trgtype = TRG_ALL;
        my_instance->log_all = false;
        my_instance->strict_logging = true;

        for (i = 0; params[i]; i++)
        {
            if (!strcmp(params[i]->name, "hostname"))
            {
                my_instance->hostname = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "username"))
            {
                my_instance->username = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "password"))
            {
                my_instance->password = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "vhost"))
            {
                my_instance->vhost = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "port"))
            {
                my_instance->port = atoi(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "exchange"))
            {
                my_instance->exchange = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "key"))
            {
                my_instance->key = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "queue"))
            {
                my_instance->queue = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "ssl_client_certificate"))
            {

                my_instance->ssl_client_cert = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "ssl_client_key"))
            {

                my_instance->ssl_client_key = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "ssl_CA_cert"))
            {

                my_instance->ssl_CA_cert = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "exchange_type"))
            {

                my_instance->exchange_type = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "logging_trigger"))
            {

                arr = parse_optstr(params[i]->value, ",", &arrsize);

                for (x = 0; x < arrsize; x++)
                {
                    if (!strcmp(arr[x], "source"))
                    {
                        my_instance->trgtype |= TRG_SOURCE;
                    }
                    else if (!strcmp(arr[x], "schema"))
                    {
                        my_instance->trgtype |= TRG_SCHEMA;
                    }
                    else if (!strcmp(arr[x], "object"))
                    {
                        my_instance->trgtype |= TRG_OBJECT;
                    }
                    else if (!strcmp(arr[x], "all"))
                    {
                        my_instance->trgtype = TRG_ALL;
                    }
                    else
                    {
                        MXS_ERROR("Unknown option for 'logging_trigger':%s.", arr[x]);
                    }
                }

                if (arrsize > 0)
                {
                    free(arr);
                }
                arrsize = 0;



            }
            else if (strstr(params[i]->name, "logging_"))
            {

                if (paramcount < parammax)
                {
                    paramlist[paramcount] = malloc(sizeof(FILTER_PARAMETER));
                    paramlist[paramcount]->name = strdup(params[i]->name);
                    paramlist[paramcount]->value = strdup(params[i]->value);
                    paramcount++;
                }
            }
        }

        if (my_instance->trgtype & TRG_SOURCE)
        {

            my_instance->src_trg = (SRC_TRIG*) malloc(sizeof(SRC_TRIG));
            my_instance->src_trg->user = NULL;
            my_instance->src_trg->host = NULL;
            my_instance->src_trg->usize = 0;
            my_instance->src_trg->hsize = 0;

        }

        if (my_instance->trgtype & TRG_SCHEMA)
        {

            my_instance->shm_trg = (SHM_TRIG*) malloc(sizeof(SHM_TRIG));
            my_instance->shm_trg->objects = NULL;
            my_instance->shm_trg->size = 0;

        }

        if (my_instance->trgtype & TRG_OBJECT)
        {

            my_instance->obj_trg = (OBJ_TRIG*) malloc(sizeof(OBJ_TRIG));
            my_instance->obj_trg->objects = NULL;
            my_instance->obj_trg->size = 0;

        }

        for (i = 0; i < paramcount; i++)
        {

            if (!strcmp(paramlist[i]->name, "logging_source_user"))
            {

                if (my_instance->src_trg)
                {
                    my_instance->src_trg->user = parse_optstr(paramlist[i]->value, ",", &arrsize);
                    my_instance->src_trg->usize = arrsize;
                    arrsize = 0;
                }

            }
            else if (!strcmp(paramlist[i]->name, "logging_source_host"))
            {

                if (my_instance->src_trg)
                {
                    my_instance->src_trg->host = parse_optstr(paramlist[i]->value, ",", &arrsize);
                    my_instance->src_trg->hsize = arrsize;
                    arrsize = 0;
                }

            }
            else if (!strcmp(paramlist[i]->name, "logging_schema"))
            {

                if (my_instance->shm_trg)
                {
                    my_instance->shm_trg->objects = parse_optstr(paramlist[i]->value, ",", &arrsize);
                    my_instance->shm_trg->size = arrsize;
                    arrsize = 0;
                }

            }
            else if (!strcmp(paramlist[i]->name, "logging_object"))
            {

                if (my_instance->obj_trg)
                {
                    my_instance->obj_trg->objects = parse_optstr(paramlist[i]->value, ",", &arrsize);
                    my_instance->obj_trg->size = arrsize;
                    arrsize = 0;
                }

            }
            else if (!strcmp(paramlist[i]->name, "logging_log_all"))
            {
                if (config_truth_value(paramlist[i]->value))
                {
                    my_instance->log_all = true;
                }
            }
            else if (!strcmp(paramlist[i]->name, "logging_strict"))
            {
                if (!config_truth_value(paramlist[i]->value))
                {
                    my_instance->strict_logging = false;
                }
            }
            free(paramlist[i]->name);
            free(paramlist[i]->value);
            free(paramlist[i]);
        }

        free(paramlist);

        if (my_instance->hostname == NULL)
        {
            my_instance->hostname = strdup("localhost");
        }
        if (my_instance->username == NULL)
        {
            my_instance->username = strdup("guest");
        }
        if (my_instance->password == NULL)
        {
            my_instance->password = strdup("guest");
        }
        if (my_instance->vhost == NULL)
        {
            my_instance->vhost = strdup("/");
        }
        if (my_instance->exchange == NULL)
        {
            my_instance->exchange = strdup("default_exchange");
        }
        if (my_instance->key == NULL)
        {
            my_instance->key = strdup("key");
        }
        if (my_instance->exchange_type == NULL)
        {
            my_instance->exchange_type = strdup("direct");
        }

        if (my_instance->ssl_client_cert != NULL &&
            my_instance->ssl_client_key != NULL &&
            my_instance->ssl_CA_cert != NULL)
        {
            my_instance->use_ssl = true;
        }
        else
        {
            my_instance->use_ssl = false;
        }

        if (my_instance->use_ssl)
        {
            amqp_set_initialize_ssl_library(0); /**Assume the underlying SSL library is already initialized*/
        }

        /**Connect to the server*/
        init_conn(my_instance);

        snprintf(taskname, 511, "mqtask%d", atomic_add(&hktask_id, 1));
        hktask_add(taskname, sendMessage, (void*) my_instance, 5);

    }
    return(FILTER *) my_instance;
}

/**
 * Declares a persistent, non-exclusive and non-passive queue that
 * auto-deletes after all the messages have been consumed.
 * @param my_session MQ_SESSION instance used to declare the queue
 * @param qname Name of the queue to be declared
 * @return Returns 0 if an error occurred, 1 if successful
 */
int declareQueue(MQ_INSTANCE *my_instance, MQ_SESSION* my_session, char* qname)
{
    int success = 1;
    amqp_rpc_reply_t reply;

    spinlock_acquire(&my_instance->rconn_lock);

    amqp_queue_declare(my_instance->conn, my_instance->channel,
                       amqp_cstring_bytes(qname),
                       0, 1, 0, 1,
                       amqp_empty_table);
    reply = amqp_get_rpc_reply(my_instance->conn);
    if (reply.reply_type != AMQP_RESPONSE_NORMAL)
    {
        success = 0;
        MXS_ERROR("Queue declaration failed.");

    }

    amqp_queue_bind(my_instance->conn, my_instance->channel,
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
    spinlock_release(&my_instance->rconn_lock);
    return success;
}

/**
 * Broadcasts a message on the message stack to the RabbitMQ server
 * and frees the allocated memory if successful. This function is only called by
 * the housekeeper thread.
 * @param data MQfilter instance
 */
void sendMessage(void* data)
{
    MQ_INSTANCE *instance = (MQ_INSTANCE*) data;
    mqmessage *tmp;
    int err_num;

    spinlock_acquire(&instance->rconn_lock);
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
    spinlock_release(&instance->rconn_lock);

    if (err_num != AMQP_STATUS_OK)
    {
        /** No connection to the broker */
        return;
    }

    spinlock_acquire(&instance->msg_lock);
    tmp = instance->messages;

    if (tmp == NULL)
    {
        spinlock_release(&instance->msg_lock);
        return;
    }

    instance->messages = instance->messages->next;
    spinlock_release(&instance->msg_lock);

    while (tmp)
    {
        err_num = amqp_basic_publish(instance->conn, instance->channel,
                                     amqp_cstring_bytes(instance->exchange),
                                     amqp_cstring_bytes(instance->key),
                                     0, 0, tmp->prop, amqp_cstring_bytes(tmp->msg));

        spinlock_acquire(&instance->rconn_lock);
        instance->conn_stat = err_num;
        spinlock_release(&instance->rconn_lock);

        if (err_num == AMQP_STATUS_OK)
        {
            /**Message was sent successfully*/
            free(tmp->prop);
            free(tmp->msg);
            free(tmp);

            atomic_add(&instance->stats.n_sent, 1);
            atomic_add(&instance->stats.n_queued, -1);
            spinlock_acquire(&instance->msg_lock);
            tmp = instance->messages;

            if (tmp == NULL)
            {
                spinlock_release(&instance->msg_lock);
                return;
            }

            instance->messages = instance->messages->next;
            spinlock_release(&instance->msg_lock);
        }
        else
        {
            spinlock_acquire(&instance->msg_lock);
            tmp->next = instance->messages;
            instance->messages = tmp;
            spinlock_release(&instance->msg_lock);
            return;
        }
    }
}

/**
 * Push a new message on the stack to be broadcasted later.
 * The message assumes ownership of the memory allocated to the message content and properties.
 * @param prop Message properties
 * @param msg Message content
 */
void pushMessage(MQ_INSTANCE *instance, amqp_basic_properties_t* prop, char* msg)
{

    mqmessage* newmsg = calloc(1, sizeof(mqmessage));
    if (newmsg)
    {
        newmsg->msg = msg;
        newmsg->prop = prop;
    }
    else
    {
        MXS_ERROR("Cannot allocate enough memory.");
        free(prop);
        free(msg);
        return;
    }

    spinlock_acquire(&instance->msg_lock);

    newmsg->next = instance->messages;
    instance->messages = newmsg;

    spinlock_release(&instance->msg_lock);

    atomic_add(&instance->stats.n_msg, 1);
    atomic_add(&instance->stats.n_queued, 1);
}

/**
 * Associate a new session with this instance of the filter and opens
 * a connection to the server and prepares the exchange and the queue for use.
 *
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    MQ_SESSION *my_session;
    MYSQL_session* sessauth;

    if ((my_session = calloc(1, sizeof(MQ_SESSION))) != NULL)
    {
        my_session->was_query = false;
        my_session->uid = NULL;
        my_session->session = session;
        sessauth = my_session->session->data;
        if (sessauth->db && strnlen(sessauth->db, 128) > 0)
        {
            my_session->db = strdup(sessauth->db);
        }
        else
        {
            my_session->db = NULL;
        }
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the MQ filter we do nothing.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static void
closeSession(FILTER *instance, void *session){ }

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
    MQ_SESSION *my_session = (MQ_SESSION *) session;
    free(my_session->uid);
    free(my_session->db);
    free(my_session);
    return;
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
    MQ_SESSION *my_session = (MQ_SESSION *) session;
    my_session->down = *downstream;
}

static void setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
    MQ_SESSION *my_session = (MQ_SESSION *) session;
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
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    MQ_SESSION *my_session = (MQ_SESSION *) session;
    MQ_INSTANCE *my_instance = (MQ_INSTANCE *) instance;
    char *ptr, t_buf[128], *combined, *canon_q, *sesshost, *sessusr;
    bool success = false, src_ok = false, schema_ok = false, obj_ok = false;
    int length, i, j, dbcount = 0;
    char** sesstbls;
    unsigned int plen = 0;
    amqp_basic_properties_t *prop;

    /**The user is changing databases*/
    if (*((char*) (queue->start + 4)) == 0x02)
    {
        if (my_session->db)
        {
            free(my_session->db);
        }
        plen = pktlen(queue->start);
        my_session->db = calloc(plen, sizeof(char));
        memcpy(my_session->db, queue->start + 5, plen - 1);
    }

    if (modutil_is_SQL(queue))
    {

        /**Parse the query*/

        if (!query_is_parsed(queue))
        {
            success = parse_query(queue);
        }

        if (!success)
        {
            MXS_ERROR("Parsing query failed.");
            goto send_downstream;
        }

        if (!my_instance->log_all)
        {
            if (!skygw_is_real_query(queue))
            {
                goto send_downstream;
            }
        }

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
            if (session_isvalid(my_session->session))
            {
                sessusr = session_getUser(my_session->session);
                sesshost = session_get_remote(my_session->session);

                /**Username was configured*/
                if (my_instance->src_trg->usize > 0)
                {
                    for (i = 0; i < my_instance->src_trg->usize; i++)
                    {
                        if (strcmp(my_instance->src_trg->user[i], sessusr) == 0)
                        {
                            MXS_INFO("Trigger is TRG_SOURCE: user: %s = %s", my_instance->src_trg->user[i], sessusr);
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
                            MXS_INFO("Trigger is TRG_SOURCE: host: %s = %s", my_instance->src_trg->host[i], sesshost);
                            src_ok = true;
                            break;
                        }
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
            char** tblnames = skygw_get_table_names(queue, &tbsz, true);
            char* tmp;
            bool all_remotes = true;

            for (z = 0; z < tbsz; z++)
            {
                if ((tmp = strchr(tblnames[z], '.')) != NULL)
                {
                    char *lasts;
                    tmp = strtok_r(tblnames[z], ".", &lasts);
                    for (i = 0; i < my_instance->shm_trg->size; i++)
                    {

                        if (strcmp(tmp, my_instance->shm_trg->objects[i]) == 0)
                        {

                            MXS_INFO("Trigger is TRG_SCHEMA: %s = %s", tmp, my_instance->shm_trg->objects[i]);

                            schema_ok = true;
                            break;
                        }
                    }
                }
                else
                {
                    all_remotes = false;
                }
                free(tblnames[z]);
            }
            free(tblnames);

            if (!schema_ok && !all_remotes && my_session->db && strlen(my_session->db) > 0)
            {

                for (i = 0; i < my_instance->shm_trg->size; i++)
                {

                    if (strcmp(my_session->db, my_instance->shm_trg->objects[i]) == 0)
                    {

                        MXS_INFO("Trigger is TRG_SCHEMA: %s = %s", my_session->db, my_instance->shm_trg->objects[i]);

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

            sesstbls = skygw_get_table_names(queue, &dbcount, false);

            for (j = 0; j < dbcount; j++)
            {
                char* tbnm = NULL;

                if ((strchr(sesstbls[j], '.')) != NULL)
                {
                    char *lasts;
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
                        MXS_INFO("Trigger is TRG_OBJECT: %s = %s", my_instance->obj_trg->objects[i], sesstbls[j]);
                        break;
                    }

                }

            }
            if (dbcount > 0)
            {
                for (j = 0; j < dbcount; j++)
                {
                    free(sesstbls[j]);
                }
                free(sesstbls);
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

            MXS_INFO("Routing message to: %s:%d %s as %s/%s, exchange: %s<%s> key:%s queue:%s",
                     my_instance->hostname, my_instance->port,
                     my_instance->vhost, my_instance->username,
                     my_instance->password, my_instance->exchange,
                     my_instance->exchange_type, my_instance->key,
                     my_instance->queue);

            if (my_session->uid == NULL)
            {

                my_session->uid = calloc(33, sizeof(char));

                if (!my_session->uid)
                {
                    MXS_ERROR("Out of memory.");
                }
                else
                {
                    genkey(my_session->uid, 32);
                }

            }

            if (queue->next != NULL)
            {
                queue = gwbuf_make_contiguous(queue);
            }

            if (modutil_extract_SQL(queue, &ptr, &length))
            {

                my_session->was_query = true;

                if ((prop = malloc(sizeof(amqp_basic_properties_t))))
                {
                    prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                        AMQP_BASIC_DELIVERY_MODE_FLAG |
                        AMQP_BASIC_MESSAGE_ID_FLAG |
                        AMQP_BASIC_CORRELATION_ID_FLAG;
                    prop->content_type = amqp_cstring_bytes("text/plain");
                    prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
                    prop->correlation_id = amqp_cstring_bytes(my_session->uid);
                    prop->message_id = amqp_cstring_bytes("query");
                }



                if (success)
                {

                    /**Try to convert to a canonical form and use the plain query if unsuccessful*/
                    if ((canon_q = skygw_get_canonical(queue)) == NULL)
                    {
                        MXS_ERROR("Cannot form canonical query.");
                    }

                }

                memset(t_buf, 0, 128);
                sprintf(t_buf, "%lu|", (unsigned long) time(NULL));

                int qlen = strnlen(canon_q, length) + strnlen(t_buf, 128);
                if ((combined = malloc((qlen + 1) * sizeof(char))) == NULL)
                {
                    MXS_ERROR("Out of memory");
                }
                strcpy(combined, t_buf);
                strncat(combined, canon_q, length);

                pushMessage(my_instance, prop, combined);
                free(canon_q);
            }

        }

        /** Pass the query downstream */
    }
send_downstream:
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
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
    if (*ptr < 0xfb) return sz;
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
 * Converts length-encoded strings to character strings and advanced the pointer to the next unrelated byte.
 * The caller is responsible for freeing the allocated memory.
 * @param c Pointer to the first byte of a valid packet.
 * @return The newly allocated string or NULL of an error occurred
 */
char* consume_lestr(unsigned char** c)
{
    unsigned int slen = consume_leitoi(c);
    char *str = calloc((slen + 1), sizeof(char));
    if (str)
    {
        memcpy(str, *c, slen);
        *c += slen;
    }
    return str;
}

/**
 *Checks whether the packet is an EOF packet.
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
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param reply		The response data
 */
static int clientReply(FILTER* instance, void *session, GWBUF *reply)
{
    MQ_SESSION *my_session = (MQ_SESSION *) session;
    MQ_INSTANCE *my_instance = (MQ_INSTANCE *) instance;
    char t_buf[128], *combined;
    unsigned int pkt_len = pktlen(reply->sbuf->data), offset = 0;
    amqp_basic_properties_t *prop;

    if (my_session->was_query)
    {

        int packet_ok = 0, was_last = 0;

        my_session->was_query = false;

        if (pkt_len > 0)
        {
            if ((prop = malloc(sizeof(amqp_basic_properties_t))))
            {
                prop->_flags = AMQP_BASIC_CONTENT_TYPE_FLAG |
                    AMQP_BASIC_DELIVERY_MODE_FLAG |
                    AMQP_BASIC_MESSAGE_ID_FLAG |
                    AMQP_BASIC_CORRELATION_ID_FLAG;
                prop->content_type = amqp_cstring_bytes("text/plain");
                prop->delivery_mode = AMQP_DELIVERY_PERSISTENT;
                prop->correlation_id = amqp_cstring_bytes(my_session->uid);
                prop->message_id = amqp_cstring_bytes("reply");
            }
            if (!(combined = calloc(GWBUF_LENGTH(reply) + 256, sizeof(char))))
            {
                MXS_ERROR("Out of memory");
            }

            memset(t_buf, 0, 128);
            sprintf(t_buf, "%lu|", (unsigned long) time(NULL));


            memcpy(combined + offset, t_buf, strnlen(t_buf, 40));
            offset += strnlen(t_buf, 40);

            if (*(reply->sbuf->data + 4) == 0x00)
            { /**OK packet*/
                unsigned int aff_rows = 0, l_id = 0, s_flg = 0, wrn = 0;
                unsigned char *ptr = (unsigned char*) (reply->sbuf->data + 5);
                pkt_len = pktlen(reply->sbuf->data);
                aff_rows = consume_leitoi(&ptr);
                l_id = consume_leitoi(&ptr);
                s_flg |= *ptr++;
                s_flg |= (*ptr++ << 8);
                wrn |= *ptr++;
                wrn |= (*ptr++ << 8);
                sprintf(combined + offset, "OK - affected_rows: %d "
                        " last_insert_id: %d "
                        " status_flags: %#0x "
                        " warnings: %d ",
                        aff_rows, l_id, s_flg, wrn);
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
            { /**ERR packet*/

                sprintf(combined + offset, "ERROR - message: %.*s",
                        (int) (reply->end - ((void*) (reply->sbuf->data + 13))),
                        (char *) reply->sbuf->data + 13);
                packet_ok = 1;
                was_last = 1;

            }
            else if (*(reply->sbuf->data + 4) == 0xfb)
            { /**LOCAL_INFILE request packet*/

                unsigned char *rset = (unsigned char*) reply->sbuf->data;
                strcpy(combined + offset, "LOCAL_INFILE: ");
                strncat(combined + offset, (const char*) rset + 5, pktlen(rset));
                packet_ok = 1;
                was_last = 1;

            }
            else
            { /**Result set*/

                unsigned char *rset = (unsigned char*) (reply->sbuf->data + 4);
                char *tmp;
                unsigned int col_cnt = consume_leitoi(&rset);

                tmp = calloc(256, sizeof(char));
                sprintf(tmp, "Columns: %d", col_cnt);
                memcpy(combined + offset, tmp, strnlen(tmp, 256));
                offset += strnlen(tmp, 256);
                memcpy(combined + offset, "\n", 1);
                offset++;
                free(tmp);

                packet_ok = 1;
                was_last = 1;

            }
            if (packet_ok)
            {

                pushMessage(my_instance, prop, combined);

                if (was_last)
                {

                    /**Successful reply received and sent, releasing uid*/

                    free(my_session->uid);
                    my_session->uid = NULL;

                }
            }
        }

    }

    return my_session->up.clientReply(my_session->up.instance,
                                      my_session->up.session, reply);
}

/**
 * Diagnostics routine
 *
 * Prints the connection details and the names of the exchange,
 * queue and the routing key.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    MQ_INSTANCE *my_instance = (MQ_INSTANCE *) instance;

    if (my_instance)
    {
        dcb_printf(dcb, "Connecting to %s:%d as '%s'.\nVhost: %s\tExchange: %s\nKey: %s\tQueue: %s\n\n",
                   my_instance->hostname, my_instance->port,
                   my_instance->username,
                   my_instance->vhost, my_instance->exchange,
                   my_instance->key, my_instance->queue
                   );
        dcb_printf(dcb, "%-16s%-16s%-16s\n",
                   "Messages", "Queued", "Sent");
        dcb_printf(dcb, "%-16d%-16d%-16d\n",
                   my_instance->stats.n_msg,
                   my_instance->stats.n_queued,
                   my_instance->stats.n_sent);
    }
}
