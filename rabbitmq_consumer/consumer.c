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

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ini.h>
#include <stdint.h>
#include <amqp_tcp_socket.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <mysql.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

typedef struct delivery_t
{
    uint64_t           dtag;
    amqp_message_t*    message;
    struct delivery_t* next, * prev;
} DELIVERY;

typedef struct consumer_t
{
    char*     hostname, * vhost, * user, * passwd, * queue, * dbserver, * dbname, * dbuser, * dbpasswd;
    DELIVERY* query_stack;
    int       port, dbport;
} CONSUMER;

static int all_ok;
static FILE* out_fd;
static CONSUMER* c_inst;
static char* DB_DATABASE = "CREATE DATABASE IF NOT EXISTS %s;";
static char* DB_TABLE
    =
        "CREATE TABLE IF NOT EXISTS pairs (tag VARCHAR(64) PRIMARY KEY NOT NULL, query VARCHAR(2048), reply VARCHAR(2048), date_in DATETIME NOT NULL, date_out DATETIME DEFAULT NULL, counter INT DEFAULT 1)";
static char* DB_INSERT = "INSERT INTO pairs(tag, query, date_in) VALUES ('%s','%s',FROM_UNIXTIME(%s))";
static char* DB_UPDATE = "UPDATE pairs SET reply='%s', date_out=FROM_UNIXTIME(%s) WHERE tag='%s'";
static char* DB_INCREMENT =
    "UPDATE pairs SET counter = counter+1, date_out=FROM_UNIXTIME(%s) WHERE query='%s'";

void sighndl(int signum)
{
    if (signum == SIGINT)
    {
        all_ok = 0;
        alarm(1);
    }
}

int handler(void* user,
            const char* section,
            const char* name,
            const char* value)
{
    if (strcmp(section, "consumer") == 0)
    {

        if (strcmp(name, "hostname") == 0)
        {
            c_inst->hostname = strdup(value);
        }
        else if (strcmp(name, "vhost") == 0)
        {
            c_inst->vhost = strdup(value);
        }
        else if (strcmp(name, "port") == 0)
        {
            c_inst->port = atoi(value);
        }
        else if (strcmp(name, "user") == 0)
        {
            c_inst->user = strdup(value);
        }
        else if (strcmp(name, "passwd") == 0)
        {
            c_inst->passwd = strdup(value);
        }
        else if (strcmp(name, "queue") == 0)
        {
            c_inst->queue = strdup(value);
        }
        else if (strcmp(name, "dbserver") == 0)
        {
            c_inst->dbserver = strdup(value);
        }
        else if (strcmp(name, "dbport") == 0)
        {
            c_inst->dbport = atoi(value);
        }
        else if (strcmp(name, "dbname") == 0)
        {
            c_inst->dbname = strdup(value);
        }
        else if (strcmp(name, "dbuser") == 0)
        {
            c_inst->dbuser = strdup(value);
        }
        else if (strcmp(name, "dbpasswd") == 0)
        {
            c_inst->dbpasswd = strdup(value);
        }
        else if (strcmp(name, "logfile") == 0)
        {
            out_fd = fopen(value, "ab");
        }
    }

    return 1;
}

int isPair(amqp_message_t* a, amqp_message_t* b)
{
    int keylen = a->properties.correlation_id.len
        >= b->properties.correlation_id.len ?
        a->properties.correlation_id.len :
        b->properties.correlation_id.len;

    return strncmp(a->properties.correlation_id.bytes,
                   b->properties.correlation_id.bytes,
                   keylen) == 0 ? 1 : 0;
}

int connectToServer(MYSQL* server)
{


    mysql_init(server);

    mysql_options(server, MYSQL_READ_DEFAULT_GROUP, "client");
    mysql_options(server, MYSQL_OPT_USE_REMOTE_CONNECTION, 0);
    my_bool tr = 1;
    mysql_options(server, MYSQL_OPT_RECONNECT, &tr);


    MYSQL* result = mysql_real_connect(server,
                                       c_inst->dbserver,
                                       c_inst->dbuser,
                                       c_inst->dbpasswd,
                                       NULL,
                                       c_inst->dbport,
                                       NULL,
                                       0);


    if (result == NULL)
    {
        fprintf(out_fd, "\33[31;1mError\33[0m: Could not connect to MySQL server: %s\n", mysql_error(server));
        return 0;
    }

    int bsz = 1024;
    char* qstr = calloc(bsz, sizeof(char));


    if (!qstr)
    {
        fprintf(stderr, "Fatal Error: Cannot allocate enough memory.\n");
        return 0;
    }


    /**Connection ok, check that the database and table exist*/

    memset(qstr, 0, bsz);
    sprintf(qstr, DB_DATABASE, c_inst->dbname);
    if (mysql_query(server, qstr))
    {
        fprintf(stderr, "\33[31;1mError\33[0m: Could not send query MySQL server: %s\n", mysql_error(server));
    }
    memset(qstr, 0, bsz);
    sprintf(qstr, "USE %s;", c_inst->dbname);
    if (mysql_query(server, qstr))
    {
        fprintf(stderr, "\33[31;1mError\33[0m: Could not send query MySQL server: %s\n", mysql_error(server));
    }

    memset(qstr, 0, bsz);
    sprintf(qstr, "%s", DB_TABLE);
    if (mysql_query(server, qstr))
    {
        fprintf(stderr, "\33[31;1mError\33[0m: Could not send query MySQL server: %s\n", mysql_error(server));
    }

    free(qstr);
    return 1;
}

int sendMessage(MYSQL* server, amqp_message_t* msg)
{
    int buffsz = (int)((msg->body.len + 1) * 2 + 1)
        + (int)((msg->properties.correlation_id.len + 1) * 2 + 1)
        + strlen(DB_INSERT),
        rval = 0;
    char* saved;
    char* qstr = calloc(buffsz, sizeof(char)),
        * rawmsg = calloc((msg->body.len + 1), sizeof(char)),
        * clnmsg = calloc(((msg->body.len + 1) * 2 + 1), sizeof(char)),
        * rawdate = calloc((msg->body.len + 1), sizeof(char)),
        * clndate = calloc(((msg->body.len + 1) * 2 + 1), sizeof(char)),
        * rawtag = calloc((msg->properties.correlation_id.len + 1), sizeof(char)),
        * clntag = calloc(((msg->properties.correlation_id.len + 1) * 2 + 1), sizeof(char));



    sprintf(qstr, "%.*s", (int)msg->body.len, (char*)msg->body.bytes);
    fprintf(out_fd, "Received: %s\n", qstr);
    char* ptr = strtok_r(qstr, "|", &saved);
    sprintf(rawdate, "%s", ptr);
    ptr = strtok_r(NULL, "\n\0", &saved);
    if (ptr == NULL)
    {
        fprintf(out_fd, "Message content not valid.\n");
        rval = 1;
        goto cleanup;
    }
    sprintf(rawmsg, "%s", ptr);
    sprintf(rawtag,
            "%.*s",
            (int)msg->properties.correlation_id.len,
            (char*)msg->properties.correlation_id.bytes);
    memset(qstr, 0, buffsz);

    mysql_real_escape_string(server, clnmsg, rawmsg, strnlen(rawmsg, msg->body.len + 1));
    mysql_real_escape_string(server, clndate, rawdate, strnlen(rawdate, msg->body.len + 1));
    mysql_real_escape_string(server, clntag, rawtag, strnlen(rawtag, msg->properties.correlation_id.len + 1));

    if (strncmp(msg->properties.message_id.bytes,
                "query",
                msg->properties.message_id.len) == 0)
    {

        sprintf(qstr, DB_INCREMENT, clndate, clnmsg);
        rval = mysql_query(server, qstr);

        if (mysql_affected_rows(server) == 0)
        {
            memset(qstr, 0, buffsz);
            sprintf(qstr, DB_INSERT, clntag, clnmsg, clndate);
            rval = mysql_query(server, qstr);
        }
    }
    else if (strncmp(msg->properties.message_id.bytes,
                     "reply",
                     msg->properties.message_id.len) == 0)
    {

        sprintf(qstr, DB_UPDATE, clnmsg, clndate, clntag);
        rval = mysql_query(server, qstr);
    }
    else
    {
        rval = 1;
        goto cleanup;
    }


    if (rval)
    {
        fprintf(stderr, "Could not send query to SQL server:%s\n", mysql_error(server));
        goto cleanup;
    }

cleanup:
    free(qstr);
    free(rawmsg);
    free(clnmsg);
    free(rawdate);
    free(clndate);
    free(rawtag);
    free(clntag);

    return rval;
}

int sendToServer(MYSQL* server, amqp_message_t* a, amqp_message_t* b)
{

    amqp_message_t* msg, * reply;
    int buffsz = 2048;
    char* qstr = calloc(buffsz, sizeof(char));

    if (!qstr)
    {
        fprintf(out_fd, "Fatal Error: Cannot allocate enough memory.\n");
        free(qstr);
        return 0;
    }

    if (a->properties.message_id.len == strlen("query")
        && strncmp(a->properties.message_id.bytes,
                   "query",
                   a->properties.message_id.len) == 0)
    {

        msg = a;
        reply = b;
    }
    else
    {

        msg = b;
        reply = a;
    }


    printf("pair: %.*s\nquery: %.*s\nreply: %.*s\n",
           (int)msg->properties.correlation_id.len,
           (char*)msg->properties.correlation_id.bytes,
           (int)msg->body.len,
           (char*)msg->body.bytes,
           (int)reply->body.len,
           (char*)reply->body.bytes);

    if ((int)msg->body.len
        + (int)reply->body.len
        + (int)msg->properties.correlation_id.len + 50 >= buffsz)
    {
        char* qtmp = calloc(buffsz * 2, sizeof(char));
        free(qstr);

        if (qtmp)
        {
            qstr = qtmp;
            buffsz *= 2;
        }
        else
        {
            fprintf(stderr, "Fatal Error: Cannot allocate enough memory.\n");
            return 0;
        }
    }

    char* rawmsg = calloc((msg->body.len + 1), sizeof(char)),
        * clnmsg = calloc(((msg->body.len + 1) * 2 + 1), sizeof(char)),
        * rawrpl = calloc((reply->body.len + 1), sizeof(char)),
        * clnrpl = calloc(((reply->body.len + 1) * 2 + 1), sizeof(char)),
        * rawtag = calloc((msg->properties.correlation_id.len + 1), sizeof(char)),
        * clntag = calloc(((msg->properties.correlation_id.len + 1) * 2 + 1), sizeof(char));

    sprintf(rawmsg, "%.*s", (int)msg->body.len, (char*)msg->body.bytes);
    sprintf(rawrpl, "%.*s", (int)reply->body.len, (char*)reply->body.bytes);
    sprintf(rawtag,
            "%.*s",
            (int)msg->properties.correlation_id.len,
            (char*)msg->properties.correlation_id.bytes);

    char* ptr;
    while ((ptr = strchr(rawmsg, '\n')))
    {
        *ptr = ' ';
    }
    while ((ptr = strchr(rawrpl, '\n')))
    {
        *ptr = ' ';
    }
    while ((ptr = strchr(rawtag, '\n')))
    {
        *ptr = ' ';
    }

    mysql_real_escape_string(server, clnmsg, rawmsg, strnlen(rawmsg, msg->body.len + 1));
    mysql_real_escape_string(server, clnrpl, rawrpl, strnlen(rawrpl, reply->body.len + 1));
    mysql_real_escape_string(server, clntag, rawtag, strnlen(rawtag, msg->properties.correlation_id.len + 1));



    sprintf(qstr, "INSERT INTO pairs VALUES ('%s','%s','%s');", clnmsg, clnrpl, clntag);
    free(rawmsg);
    free(clnmsg);
    free(rawrpl);
    free(clnrpl);
    free(rawtag);
    free(clntag);

    if (mysql_query(server, qstr))
    {
        fprintf(stderr, "Could not send query to SQL server:%s\n", mysql_error(server));
        free(qstr);
        return 0;
    }

    free(qstr);
    return 1;
}
int main(int argc, char** argv)
{
    int channel = 1, status = AMQP_STATUS_OK, cnfnlen;
    amqp_socket_t* socket = NULL;
    amqp_connection_state_t conn;
    amqp_rpc_reply_t ret;
    amqp_message_t* reply = NULL;
    amqp_frame_t frame;
    struct timeval timeout;
    MYSQL db_inst;
    char ch, * cnfname = NULL, * cnfpath = NULL;
    static const char* fname = "consumer.cnf";
    const char* default_path = "@CMAKE_INSTALL_PREFIX@/etc";

    if ((c_inst = calloc(1, sizeof(CONSUMER))) == NULL)
    {
        fprintf(stderr, "Fatal Error: Cannot allocate enough memory.\n");
        return 1;
    }

    if (signal(SIGINT, sighndl) == SIG_IGN)
    {
        signal(SIGINT, SIG_IGN);
    }

    while ((ch = getopt(argc, argv, "c:")) != -1)
    {
        switch (ch)
        {
        case 'c':
            cnfnlen = strlen(optarg);
            cnfpath = strdup(optarg);
            break;

        default:

            break;
        }
    }

    if (cnfpath == NULL)
    {
        cnfpath = strdup(default_path);
        cnfnlen = strlen(default_path);
    }

    cnfname = calloc(cnfnlen + strlen(fname) + 1, sizeof(char));

    if (cnfpath)
    {

        /**Config file path as argument*/
        strcpy(cnfname, cnfpath);
        if (cnfpath[cnfnlen - 1] != '/')
        {
            strcat(cnfname, "/");
        }
    }

    strcat(cnfname, fname);

    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    all_ok = 1;
    out_fd = NULL;



    /**Parse the INI file*/
    if (ini_parse(cnfname, handler, NULL) < 0)
    {

        /**Try to parse a config in the same directory*/
        if (ini_parse(fname, handler, NULL) < 0)
        {
            fprintf(stderr, "Fatal Error: Error parsing configuration file!\n");
            goto fatal_error;
        }
    }

    if (out_fd == NULL)
    {
        out_fd = stdout;
    }

    fprintf(out_fd, "\n--------------------------------------------------------------\n");

    /**Confirm that all parameters were in the configuration file*/
    if (!c_inst->hostname || !c_inst->vhost || !c_inst->user
        || !c_inst->passwd || !c_inst->dbpasswd || !c_inst->queue
        || !c_inst->dbserver || !c_inst->dbname || !c_inst->dbuser)
    {
        fprintf(stderr, "Fatal Error: Inadequate configuration file!\n");
        goto fatal_error;
    }

    connectToServer(&db_inst);

    if ((conn = amqp_new_connection()) == NULL
        || (socket = amqp_tcp_socket_new(conn)) == NULL)
    {
        fprintf(stderr, "Fatal Error: Cannot create connection object or socket.\n");
        goto fatal_error;
    }

    if (amqp_socket_open(socket, c_inst->hostname, c_inst->port))
    {
        fprintf(stderr, "\33[31;1mRabbitMQ Error\33[0m: Cannot open socket.\n");
        goto error;
    }

    ret = amqp_login(conn, c_inst->vhost, 0, 131072, 0, AMQP_SASL_METHOD_PLAIN, c_inst->user, c_inst->passwd);

    if (ret.reply_type != AMQP_RESPONSE_NORMAL)
    {
        fprintf(stderr, "\33[31;1mRabbitMQ Error\33[0m: Cannot login to server.\n");
        goto error;
    }

    amqp_channel_open(conn, channel);
    ret = amqp_get_rpc_reply(conn);

    if (ret.reply_type != AMQP_RESPONSE_NORMAL)
    {
        fprintf(stderr, "\33[31;1mRabbitMQ Error\33[0m: Cannot open channel.\n");
        goto error;
    }

    reply = malloc(sizeof(amqp_message_t));
    if (!reply)
    {
        fprintf(stderr, "Error: Cannot allocate enough memory.\n");
        goto error;
    }
    amqp_basic_consume(conn,
                       channel,
                       amqp_cstring_bytes(c_inst->queue),
                       amqp_empty_bytes,
                       0,
                       0,
                       0,
                       amqp_empty_table);

    while (all_ok)
    {

        status = amqp_simple_wait_frame_noblock(conn, &frame, &timeout);

        /**No frames to read from server, possibly out of messages*/
        if (status == AMQP_STATUS_TIMEOUT)
        {
            sleep(timeout.tv_sec);
            continue;
        }

        if (frame.payload.method.id == AMQP_BASIC_DELIVER_METHOD)
        {

            amqp_basic_deliver_t* decoded = (amqp_basic_deliver_t*)frame.payload.method.decoded;

            amqp_read_message(conn, channel, reply, 0);

            if (sendMessage(&db_inst, reply))
            {

                fprintf(stderr, "\33[31;1mRabbitMQ Error\33[0m: Received malformed message.\n");
                amqp_basic_reject(conn, channel, decoded->delivery_tag, 0);
                amqp_destroy_message(reply);
            }
            else
            {

                amqp_basic_ack(conn, channel, decoded->delivery_tag, 0);
                amqp_destroy_message(reply);
            }
        }
        else
        {
            fprintf(stderr,
                    "\33[31;1mRabbitMQ Error\33[0m: Received method from server: %s\n",
                    amqp_method_name(frame.payload.method.id));
            all_ok = 0;
            goto error;
        }
    }

    fprintf(out_fd, "Shutting down...\n");
error:

    mysql_close(&db_inst);
    mysql_library_end();
    if (c_inst && c_inst->query_stack)
    {

        while (c_inst->query_stack)
        {
            DELIVERY* d = c_inst->query_stack->next;
            amqp_destroy_message(c_inst->query_stack->message);
            free(c_inst->query_stack);
            c_inst->query_stack = d;
        }
    }

    amqp_channel_close(conn, channel, AMQP_REPLY_SUCCESS);
    amqp_connection_close(conn, AMQP_REPLY_SUCCESS);
    amqp_destroy_connection(conn);
fatal_error:

    if (out_fd)
    {
        fclose(out_fd);
    }


    if (c_inst)
    {

        free(c_inst->hostname);
        free(c_inst->vhost);
        free(c_inst->user);
        free(c_inst->passwd);
        free(c_inst->queue);
        free(c_inst->dbserver);
        free(c_inst->dbname);
        free(c_inst->dbuser);
        free(c_inst->dbpasswd);
        free(c_inst);
    }



    return all_ok;
}
