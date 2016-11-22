/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>

#include <strings.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/filter.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/poll.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/query_classifier.h>

/**
 * @file datastream.c - Streaming of bulk inserts
 */

static FILTER *createInstance(const char *name, char **options, CONFIG_PARAMETER*params);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static void setUpstream(FILTER *instance, void *session, UPSTREAM *upstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);
static uint64_t getCapabilities(void);
static int clientReply(FILTER* instance, void *session, GWBUF *reply);
static bool extract_insert_target(GWBUF *buffer, char* target, int len);
static GWBUF* create_load_data_command(const char *target);
static GWBUF* convert_to_stream(GWBUF* buffer, uint8_t packet_num);

/**
 * Instance structure
 */
typedef struct
{
    char *source; /*< Source address to restrict matches */
    char *user; /*< User name to restrict matches */
} DS_INSTANCE;

enum ds_state
{
    DS_STREAM_CLOSED,
    DS_REQUEST_SENT,
    DS_REQUEST_ACCEPTED,
    DS_STREAM_OPEN,
    DS_CLOSING_STREAM
};

/**
 * The session structure for this regex filter
 */
typedef struct
{
    DOWNSTREAM down; /* The downstream filter */
    UPSTREAM up;
    GWBUF *queue;
    GWBUF *writebuf;
    bool active;
    char target[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 1]; /**< Current target table */
    uint8_t packet_num;
    DCB* client_dcb;
    enum ds_state state; /*< Whether a LOAD DATA LOCAL INFILE was sent or not */
} DS_SESSION;

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
        getCapabilities,
        NULL,
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_EXPERIMENTAL,
        FILTER_VERSION,
        "Data streaming filter",
        "1.0.0",
        &MyObject,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {"source", MXS_MODULE_PARAM_STRING},
            {"user", MXS_MODULE_PARAM_STRING},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

/**
 * Free a insertstream instance.
 * @param instance instance to free
 */
void free_instance(DS_INSTANCE *instance)
{
    if (instance)
    {
        MXS_FREE(instance->source);
        MXS_FREE(instance->user);
        MXS_FREE(instance);
    }
}

/**
 * This the SQL command that starts the streaming
 */
static const char load_data_template[] = "LOAD DATA LOCAL INFILE 'maxscale.data' "
                                         "INTO TABLE %s FIELDS TERMINATED BY ',' LINES TERMINATED BY '\n'";

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *
createInstance(const char *name, char **options, CONFIG_PARAMETER *params)
{
    DS_INSTANCE *my_instance;

    if ((my_instance = MXS_CALLOC(1, sizeof(DS_INSTANCE))) != NULL)
    {
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");
    }

    return (FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    DS_INSTANCE *my_instance = (DS_INSTANCE *) instance;
    DS_SESSION *my_session;

    if ((my_session = MXS_CALLOC(1, sizeof(DS_SESSION))) != NULL)
    {
        my_session->target[0] = '\0';
        my_session->state = DS_STREAM_CLOSED;
        my_session->active = true;
        my_session->client_dcb = session->client_dcb;

        if (my_instance->source &&
            strcmp(session->client_dcb->remote, my_instance->source) != 0)
        {
            my_session->active = false;
        }

        if (my_instance->user &&
            strcmp(session->client_dcb->user, my_instance->user) != 0)
        {
            my_session->active = false;
        }
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
freeSession(FILTER *instance, void *session)
{
    MXS_FREE(session);
    return;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    DS_SESSION *my_session = (DS_SESSION*) session;
    my_session->down = *downstream;
}

/**
 * Set the filter upstream
 * @param instance Filter instance
 * @param session Filter session
 * @param upstream Upstream filter
 */
static void setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
    DS_SESSION *my_session = (DS_SESSION*) session;
    my_session->up = *upstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    DS_SESSION *my_session = (DS_SESSION *) session;
    char target[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 1];
    bool send_ok = false;
    bool send_error = false;
    int rc = 0;

    if (session_trx_is_active(my_session->client_dcb->session) &&
        extract_insert_target(queue, target, sizeof(target)))
    {
        if (my_session->state == DS_STREAM_CLOSED)
        {
            /** We're opening a new stream */
            strcpy(my_session->target, target);
            my_session->queue = queue;
            my_session->state = DS_REQUEST_SENT;
            my_session->packet_num = 0;
            queue = create_load_data_command(target);
        }
        else
        {
            if (my_session->state == DS_REQUEST_ACCEPTED)
            {
                my_session->state = DS_STREAM_OPEN;
            }

            if (my_session->state == DS_STREAM_OPEN)
            {
                if (strcmp(target, my_session->target) == 0)
                {
                    /** Stream is open and targets match, convert the insert into
                     * a data stream */
                    uint8_t packet_num = ++my_session->packet_num;
                    send_ok = true;
                    queue = convert_to_stream(queue, packet_num);
                }
                else
                {
                    /** Target mismatch */
                    gwbuf_free(queue);
                    send_error = true;
                }
            }
        }
    }
    else
    {
        /** Transaction is not active or this is not an insert */
        bool send_empty = false;
        uint8_t packet_num;
        *my_session->target = '\0';

        if (my_session->state == DS_STREAM_OPEN)
        {
            /** Stream is open, we need to close it */
            my_session->state = DS_CLOSING_STREAM;
            send_empty = true;
            packet_num = ++my_session->packet_num;
            my_session->queue = queue;
        }
        else if (my_session->state == DS_REQUEST_ACCEPTED)
        {
            my_session->state = DS_STREAM_OPEN;
            send_ok = true;
        }

        if (send_empty)
        {
            char empty_packet[] = {0, 0, 0, packet_num};
            queue = gwbuf_alloc_and_load(sizeof(empty_packet), &empty_packet[0]);
        }
    }

    if (send_ok)
    {
        rc = mxs_mysql_send_ok(my_session->client_dcb, 1, 0, NULL);
    }

    if (send_error)
    {
        rc = mysql_send_custom_error(my_session->client_dcb, 1, 0, "Invalid insert target");
    }
    else
    {
        rc = my_session->down.routeQuery(my_session->down.instance,
                                         my_session->down.session, queue);
    }

    return rc;
}

static char* get_value(char* data, uint32_t datalen, char** dest, uint32_t* destlen)
{
    char *value_start = strnchr_esc_mysql(data, '(', datalen);

    if (value_start)
    {
        value_start++;
        char *value_end = strnchr_esc_mysql(value_start, ')', datalen - (value_start - data));

        if (value_end)
        {
            *destlen = value_end - value_start;
            *dest = value_start;
            return value_end;
        }
    }

    return NULL;
}

static GWBUF* convert_to_stream(GWBUF* buffer, uint8_t packet_num)
{
    /** Remove the INSERT INTO ... from the buffer */
    char *dataptr = (char*) GWBUF_DATA(buffer);
    char *modptr = strnchr_esc_mysql(dataptr + 5, '(', GWBUF_LENGTH(buffer));

    /** Leave some space for the header so we don't have to allocate a new one */
    buffer = gwbuf_consume(buffer, (modptr - dataptr) - 4);
    char* header_start = (char*)GWBUF_DATA(buffer);
    char* store_end = dataptr = header_start + 4;
    char* end = buffer->end;
    char* value;
    uint32_t valuesize;

    /** Remove the parentheses from the insert and recalculate the packet length */
    while ((dataptr = get_value(dataptr, end - dataptr, &value, &valuesize)))
    {
        // TODO: Don't move everything, only move the needed parts
        memmove(store_end, value, valuesize);
        store_end += valuesize;
        *store_end++ = '\n';
    }

    gwbuf_rtrim(buffer, (char*)buffer->end - store_end);
    uint32_t len = gwbuf_length(buffer) - 4;

    *header_start++ = len;
    *header_start++ = len >> 8;
    *header_start++ = len >> 16;
    *header_start = packet_num;

    return buffer;
}

static int clientReply(FILTER* instance, void *session, GWBUF *reply)
{
    DS_SESSION *my_session = (DS_SESSION*) session;

    if (my_session->state == DS_CLOSING_STREAM ||
        (my_session->state == DS_REQUEST_SENT &&
         !MYSQL_IS_ERROR_PACKET((uint8_t*)GWBUF_DATA(reply))))
    {
        gwbuf_free(reply);
        ss_dassert(my_session->queue);

        my_session->state = my_session->state == DS_CLOSING_STREAM ?
                            DS_STREAM_CLOSED : DS_REQUEST_ACCEPTED;

        GWBUF* queue = my_session->queue;
        my_session->queue = NULL;

        if (my_session->state == DS_REQUEST_ACCEPTED)
        {
            /** The request is packet 0 and the response is packet 1 so we'll
             * have to send the data in packet number 2 */
            my_session->packet_num++;
        }

        poll_add_epollin_event_to_dcb(my_session->client_dcb, queue);
    }
    else
    {
        return my_session->up.clientReply(my_session->up.instance,
                                          my_session->up.session, reply);
    }

    return 0;
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    DS_INSTANCE *my_instance = (DS_INSTANCE *) instance;

    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tReplacement limited to connections from     %s\n", my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb, "\t\tReplacement limit to user           %s\n", my_instance->user);
    }
}

static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_TRANSACTION_TRACKING;
}

/**
 * Check if a buffer contains an insert statement
 *
 * @param   buffer Buffer to analyze
 * @return  True if the buffer contains an insert statement
 */
static bool extract_insert_target(GWBUF *buffer, char* target, int len)
{
    bool rval = false;
    int n_tables = 0;
    char **tables = qc_get_table_names(buffer, &n_tables, true);

    if (n_tables == 1)
    {
        /** Only one table in an insert */
        snprintf(target, len, "%s", tables[0]);
        rval = true;
    }

    if (tables)
    {
        for (int i = 0; i < (size_t)n_tables; ++i)
        {
            MXS_FREE(tables[i]);
        }
        MXS_FREE(tables);
    }

    return rval;
}

static GWBUF* create_load_data_command(const char *target)
{
    char str[sizeof(load_data_template) + strlen(target) + 1];
    snprintf(str, sizeof(str), load_data_template, target);
    uint32_t payload = strlen(str) + 1;

    GWBUF *rval = gwbuf_alloc(payload + 4);
    if (rval)
    {
        uint8_t *ptr = GWBUF_DATA(rval);
        *ptr++ = payload;
        *ptr++ = payload >> 8;
        *ptr++ = payload >> 16;
        *ptr++ = 0;
        *ptr++ = 0x03;
        memcpy(ptr, str, payload - 1);
    }

    return rval;
}
