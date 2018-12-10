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

#define MXS_MODULE_NAME "insertstream"

#include <maxscale/ccdefs.hh>

#include <strings.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.h>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/query_classifier.h>

/**
 * @file datastream.c - Streaming of bulk inserts
 */

static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER* params);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session);
static void                closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void                setDownstream(MXS_FILTER* instance,
                                         MXS_FILTER_SESSION* fsession,
                                         MXS_DOWNSTREAM* downstream);
static void setUpstream(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* session,
                        MXS_UPSTREAM* upstream);
static int32_t  routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static void     diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb);
static json_t*  diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);
static int32_t  clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* reply);
static bool     extract_insert_target(GWBUF* buffer, char* target, int len);
static GWBUF*   create_load_data_command(const char* target);
static GWBUF*   convert_to_stream(GWBUF* buffer, uint8_t packet_num);

/**
 * Instance structure
 */
typedef struct
{
    char* source;   /**< Source address to restrict matches */
    char* user;     /**< User name to restrict matches */
} DS_INSTANCE;

enum ds_state
{
    DS_STREAM_CLOSED,   /**< Initial state */
    DS_REQUEST_SENT,    /**< Request for stream sent */
    DS_REQUEST_ACCEPTED,/**< Stream request accepted */
    DS_STREAM_OPEN,     /**< Stream is open */
    DS_CLOSING_STREAM   /**< Stream is about to be closed */
};

/**
 * The session structure for this regex filter
 */
typedef struct
{
    MXS_DOWNSTREAM down;                                            /**< Downstream filter */
    MXS_UPSTREAM   up;                                              /**< Upstream filter*/
    GWBUF*         queue;                                           /**< Queue containing a stored
                                                                     * query */
    bool active;                                                    /**< Whether the session is active
                                                                     * */
    uint8_t packet_num;                                             /**< If stream is open, the
                                                                     * current packet sequence number
                                                                     * */
    DCB*          client_dcb;                                       /**< Client DCB */
    enum ds_state state;                                            /**< The current state of the
                                                                     * stream */
    char target[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 1];    /**< Current target table */
} DS_SESSION;

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
            NULL,
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_EXPERIMENTAL,
            MXS_FILTER_VERSION,
            "Data streaming filter",
            "1.0.0",
            RCAP_TYPE_TRANSACTION_TRACKING,
            &MyObject,
            NULL,
            NULL,
            NULL,
            NULL,
            {
                {"source",                 MXS_MODULE_PARAM_STRING },
                {"user",                   MXS_MODULE_PARAM_STRING },
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/**
 * Free a insertstream instance.
 * @param instance instance to free
 */
void free_instance(DS_INSTANCE* instance)
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
                                         "INTO TABLE %s FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n'";

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    DS_INSTANCE* my_instance;

    if ((my_instance = static_cast<DS_INSTANCE*>(MXS_CALLOC(1, sizeof(DS_INSTANCE)))) != NULL)
    {
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");
    }

    return (MXS_FILTER*) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    DS_INSTANCE* my_instance = (DS_INSTANCE*) instance;
    DS_SESSION* my_session;

    if ((my_session = static_cast<DS_SESSION*>(MXS_CALLOC(1, sizeof(DS_SESSION)))) != NULL)
    {
        my_session->target[0] = '\0';
        my_session->state = DS_STREAM_CLOSED;
        my_session->active = true;
        my_session->client_dcb = session->client_dcb;

        if (my_instance->source
            && strcmp(session->client_dcb->remote, my_instance->source) != 0)
        {
            my_session->active = false;
        }

        if (my_instance->user
            && strcmp(session->client_dcb->user, my_instance->user) != 0)
        {
            my_session->active = false;
        }
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    MXS_FREE(session);
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    DS_SESSION* my_session = (DS_SESSION*) session;
    my_session->down = *downstream;
}

/**
 * Set the filter upstream
 * @param instance Filter instance
 * @param session Filter session
 * @param upstream Upstream filter
 */
static void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_UPSTREAM* upstream)
{
    DS_SESSION* my_session = (DS_SESSION*) session;
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
 *
 * @return 1 on success, 0 on error
 */
static int32_t routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    DS_SESSION* my_session = (DS_SESSION*) session;
    char target[MYSQL_TABLE_MAXLEN + MYSQL_DATABASE_MAXLEN + 1];
    bool send_ok = false;
    bool send_error = false;
    int rc = 0;
    mxb_assert(GWBUF_IS_CONTIGUOUS(queue));

    if (session_trx_is_active(my_session->client_dcb->session)
        && extract_insert_target(queue, target, sizeof(target)))
    {
        switch (my_session->state)
        {
        case DS_STREAM_CLOSED:
            /** We're opening a new stream */
            strcpy(my_session->target, target);
            my_session->queue = queue;
            my_session->state = DS_REQUEST_SENT;
            my_session->packet_num = 0;
            queue = create_load_data_command(target);
            break;

        case DS_REQUEST_ACCEPTED:
            my_session->state = DS_STREAM_OPEN;
        /** Fallthrough */

        case DS_STREAM_OPEN:
            if (strcmp(target, my_session->target) == 0)
            {
                /**
                 * Stream is open and targets match, convert the insert into
                 * a data stream
                 */
                uint8_t packet_num = ++my_session->packet_num;
                send_ok = true;
                queue = convert_to_stream(queue, packet_num);
            }
            else
            {
                /**
                 * Target mismatch
                 *
                 * TODO: Instead of sending an error, we could just open a new stream
                 */
                gwbuf_free(queue);
                send_error = true;
            }
            break;

        default:
            MXS_ERROR("Unexpected state: %d", my_session->state);
            mxb_assert(false);
            break;
        }
    }
    else
    {
        /** Transaction is not active or this is not an insert */
        bool send_empty = false;
        uint8_t packet_num;
        *my_session->target = '\0';

        switch (my_session->state)
        {
        case DS_STREAM_OPEN:
            /** Stream is open, we need to close it */
            my_session->state = DS_CLOSING_STREAM;
            send_empty = true;
            packet_num = ++my_session->packet_num;
            my_session->queue = queue;
            break;

        case DS_REQUEST_ACCEPTED:
            my_session->state = DS_STREAM_OPEN;
            send_ok = true;
            break;

        default:
            mxb_assert(my_session->state == DS_STREAM_CLOSED);
            break;
        }

        if (send_empty)
        {
            char empty_packet[] = {0, 0, 0, static_cast<char>(packet_num)};
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
                                         my_session->down.session,
                                         queue);
    }

    return rc;
}

/**
 * Extract inserted values
 */
static char* get_value(char* data, uint32_t datalen, char** dest, uint32_t* destlen)
{
    char* value_start = strnchr_esc_mysql(data, '(', datalen);

    if (value_start)
    {
        value_start++;
        char* value_end = strnchr_esc_mysql(value_start, ')', datalen - (value_start - data));

        if (value_end)
        {
            *destlen = value_end - value_start;
            *dest = value_start;
            return value_end;
        }
    }

    return NULL;
}

/**
 * @brief Convert an INSERT query into a CSV stream
 *
 * @param buffer     Buffer containing the query
 * @param packet_num The current packet sequence number
 *
 * @return The modified buffer
 */
static GWBUF* convert_to_stream(GWBUF* buffer, uint8_t packet_num)
{
    /** Remove the INSERT INTO ... from the buffer */
    char* dataptr = (char*)GWBUF_DATA(buffer);
    char* modptr = strnchr_esc_mysql(dataptr + MYSQL_HEADER_LEN + 1, '(', GWBUF_LENGTH(buffer));

    /** Leave some space for the header so we don't have to allocate a new one */
    buffer = gwbuf_consume(buffer, (modptr - dataptr) - MYSQL_HEADER_LEN);
    char* header_start = (char*)GWBUF_DATA(buffer);
    char* store_end = dataptr = header_start + MYSQL_HEADER_LEN;
    char* end = static_cast<char*>(buffer->end);
    char* value;
    uint32_t valuesize;

    /**
     * Remove the parentheses from the insert, add newlines between values and
     * recalculate the packet length
     */
    while ((dataptr = get_value(dataptr, end - dataptr, &value, &valuesize)))
    {
        memmove(store_end, value, valuesize);
        store_end += valuesize;
        *store_end++ = '\n';
    }

    gwbuf_rtrim(buffer, (char*)buffer->end - store_end);
    uint32_t len = gwbuf_length(buffer) - MYSQL_HEADER_LEN;

    *header_start++ = len;
    *header_start++ = len >> 8;
    *header_start++ = len >> 16;
    *header_start = packet_num;

    return buffer;
}

/**
 * @brief Handle replies from the backend
 *
 *
 * @param instance Filter instance
 * @param session  Filter session
 * @param reply    The reply from the backend
 *
 * @return 1 on success, 0 on error
 */
static int32_t clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* reply)
{
    DS_SESSION* my_session = (DS_SESSION*) session;
    int rc = 1;

    if (my_session->state == DS_CLOSING_STREAM
        || (my_session->state == DS_REQUEST_SENT
            && !MYSQL_IS_ERROR_PACKET((uint8_t*)GWBUF_DATA(reply))))
    {
        gwbuf_free(reply);
        mxb_assert(my_session->queue);

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
        rc = my_session->up.clientReply(my_session->up.instance,
                                        my_session->up.session,
                                        reply);
    }

    return rc;
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance The filter instance
 * @param   fsession Filter session, may be NULL
 * @param   dcb      The DCB for diagnostic output
 */
static void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    DS_INSTANCE* my_instance = (DS_INSTANCE*) instance;

    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tReplacement limited to connections from     %s\n", my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb, "\t\tReplacement limit to user           %s\n", my_instance->user);
    }
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param   instance The filter instance
 * @param   fsession Filter session, may be NULL
 */
static json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    DS_INSTANCE* my_instance = (DS_INSTANCE*)instance;

    json_t* rval = json_object();

    if (my_instance->source)
    {
        json_object_set_new(rval, "source", json_string(my_instance->source));
    }

    if (my_instance->user)
    {
        json_object_set_new(rval, "user", json_string(my_instance->user));
    }

    return rval;
}

/**
 * @brief Get filter capabilities
 *
 * @return Filter capabilities
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}

/**
 * @brief Check if an insert statement has implicitly ordered values
 *
 * @param buffer Buffer to check
 *
 * @return True if the insert does not define the order of the values
 */
static bool only_implicit_values(GWBUF* buffer)
{
    bool rval = false;
    char* data = (char*)GWBUF_DATA(buffer);
    char* ptr = strnchr_esc_mysql(data + MYSQL_HEADER_LEN + 1, '(', GWBUF_LENGTH(buffer));

    if (ptr && (ptr = strnchr_esc_mysql(ptr, ')', GWBUF_LENGTH(buffer) - (ptr - data))))
    {
        /** Skip the closing parenthesis and any whitespace */
        ptr++;

        while (ptr < (char*)buffer->end && isspace(*ptr))
        {
            ptr++;
        }

        if (ptr >= (char*)buffer->end || !isalnum(*ptr))
        {
            /**
             * The first pair of parentheses was followed by a non-alphanumeric
             * character. We can be fairly certain that the INSERT statement
             * implicitly defines the order of the column values as is done in
             * the following example:
             *
             * INSERT INTO test.t1 VALUES (1, "hello"), (2, "world");
             */
            rval = true;
        }
    }

    return rval;
}

/**
 * @brief Extract insert target
 *
 * @param   buffer Buffer to analyze
 *
 * @return  True if the buffer contains an insert statement and the target table
 * was successfully extracted
 */
static bool extract_insert_target(GWBUF* buffer, char* target, int len)
{
    bool rval = false;

    if (MYSQL_GET_COMMAND(GWBUF_DATA(buffer)) == MXS_COM_QUERY
        && qc_get_operation(buffer) == QUERY_OP_INSERT
        && only_implicit_values(buffer))
    {
        int n_tables = 0;
        char** tables = qc_get_table_names(buffer, &n_tables, true);

        if (n_tables == 1)
        {
            /** Only one table in an insert */
            snprintf(target, len, "%s", tables[0]);
            rval = true;
        }

        if (tables)
        {
            for (int i = 0; i < n_tables; ++i)
            {
                MXS_FREE(tables[i]);
            }
            MXS_FREE(tables);
        }
    }

    return rval;
}

/**
 * @brief Create a LOAD DATA LOCAL INFILE statement
 *
 * @param target The table name where the data is loaded
 *
 * @return Buffer containing the statement or NULL if memory allocation failed
 */
static GWBUF* create_load_data_command(const char* target)
{
    char str[sizeof(load_data_template) + strlen(target) + 1];
    snprintf(str, sizeof(str), load_data_template, target);
    uint32_t payload = strlen(str) + 1;

    GWBUF* rval = gwbuf_alloc(payload + MYSQL_HEADER_LEN);
    if (rval)
    {
        uint8_t* ptr = GWBUF_DATA(rval);
        *ptr++ = payload;
        *ptr++ = payload >> 8;
        *ptr++ = payload >> 16;
        *ptr++ = 0;
        *ptr++ = 0x03;
        memcpy(ptr, str, payload - 1);
    }

    return rval;
}
