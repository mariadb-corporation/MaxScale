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
 * @file maxrows.c - Result set limit Filter
 */

#include "maxrows.hh"

#include <stdbool.h>
#include <stdint.h>

#include <maxbase/assert.h>
#include <maxscale/alloc.h>
#include <maxscale/buffer.hh>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.h>
#include <maxscale/poll.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/query_classifier.hh>

static MXS_FILTER*         createInstance(const char* name, MXS_CONFIG_PARAMETER*);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance,
                                      MXS_SESSION* session);
static void closeSession(MXS_FILTER* instance,
                         MXS_FILTER_SESSION* sdata);
static void freeSession(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* sdata);
static void setDownstream(MXS_FILTER* instance,
                          MXS_FILTER_SESSION* sdata,
                          MXS_DOWNSTREAM* downstream);
static void setUpstream(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* sdata,
                        MXS_UPSTREAM* upstream);
static int routeQuery(MXS_FILTER* instance,
                      MXS_FILTER_SESSION* sdata,
                      GWBUF* queue);
static int clientReply(MXS_FILTER* instance,
                       MXS_FILTER_SESSION* sdata,
                       GWBUF* queue);
static void diagnostics(MXS_FILTER* instance,
                        MXS_FILTER_SESSION* sdata,
                        DCB* dcb);
static json_t* diagnostics_json(const MXS_FILTER* instance,
                                const MXS_FILTER_SESSION* sdata);
static uint64_t getCapabilities(MXS_FILTER* instance);

enum maxrows_return_mode
{
    MAXROWS_RETURN_EMPTY = 0,
    MAXROWS_RETURN_ERR,
    MAXROWS_RETURN_OK
};

static const MXS_ENUM_VALUE return_option_values[] =
{
    {"empty", MAXROWS_RETURN_EMPTY},
    {"error", MAXROWS_RETURN_ERR  },
    {"ok",    MAXROWS_RETURN_OK   },
    {NULL}
};

/* Global symbols of the Module */

extern "C"
{

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static MXS_FILTER_OBJECT object =
        {
            createInstance,
            newSession,
            closeSession,
            freeSession,
            setDownstream,
            setUpstream,
            routeQuery,
            clientReply,
            diagnostics,
            diagnostics_json,
            getCapabilities,
            NULL,   // No destroyInstance
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_FILTER,
            MXS_MODULE_IN_DEVELOPMENT,
            MXS_FILTER_VERSION,
            "A filter that is capable of limiting the resultset number of rows.",
            "V1.0.0",
            RCAP_TYPE_STMT_INPUT | RCAP_TYPE_STMT_OUTPUT,
            &object,
            NULL,   /* Process init. */
            NULL,   /* Process finish. */
            NULL,   /* Thread init. */
            NULL,   /* Thread finish. */
            {
                {
                    "max_resultset_rows",
                    MXS_MODULE_PARAM_COUNT,
                    MAXROWS_DEFAULT_MAX_RESULTSET_ROWS
                },
                {
                    "max_resultset_size",
                    MXS_MODULE_PARAM_SIZE,
                    MAXROWS_DEFAULT_MAX_RESULTSET_SIZE
                },
                {
                    "debug",
                    MXS_MODULE_PARAM_COUNT,
                    MAXROWS_DEFAULT_DEBUG
                },
                {
                    "max_resultset_return",
                    MXS_MODULE_PARAM_ENUM,
                    "empty",
                    MXS_MODULE_OPT_ENUM_UNIQUE,
                    return_option_values
                },
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }
}

/* Implementation */

typedef struct maxrows_config
{
    uint32_t                 max_resultset_rows;
    uint32_t                 max_resultset_size;
    uint32_t                 debug;
    enum maxrows_return_mode m_return;
} MAXROWS_CONFIG;

typedef struct maxrows_instance
{
    const char*    name;
    MAXROWS_CONFIG config;
} MAXROWS_INSTANCE;

typedef enum maxrows_session_state
{
    MAXROWS_EXPECTING_RESPONSE = 1, // A select has been sent, and we are waiting for the response.
    MAXROWS_EXPECTING_FIELDS,       // A select has been sent, and we want more fields.
    MAXROWS_EXPECTING_ROWS,         // A select has been sent, and we want more rows.
    MAXROWS_EXPECTING_NOTHING,      // We are not expecting anything from the server.
    MAXROWS_IGNORING_RESPONSE,      // We are not interested in the data received from the server.
} maxrows_session_state_t;

typedef struct maxrows_response_state
{
    GWBUF* data;            /**< Response data, possibly incomplete. */
    size_t n_totalfields;   /**< The number of fields a resultset contains. */
    size_t n_fields;        /**< How many fields we have received, <= n_totalfields. */
    size_t n_rows;          /**< How many rows we have received. */
    size_t offset;          /**< Where we are in the response buffer. */
    size_t length;          /**< Buffer size. */
    GWBUF* column_defs;     /**< Buffer with result set columns definitions */
} MAXROWS_RESPONSE_STATE;

static void maxrows_response_state_reset(MAXROWS_RESPONSE_STATE* state);

typedef struct maxrows_session_data
{
    MAXROWS_INSTANCE*       instance;           /**< The maxrows instance the session is associated with. */
    MXS_DOWNSTREAM          down;               /**< The previous filter or equivalent. */
    MXS_UPSTREAM            up;                 /**< The next filter or equivalent. */
    MAXROWS_RESPONSE_STATE  res;                /**< The response state. */
    MXS_SESSION*            session;            /**< The session this data is associated with. */
    maxrows_session_state_t state;
    bool                    large_packet;       /**< Large packet (> 16MB)) indicator */
    bool                    discard_resultset;  /**< Discard resultset indicator */
    GWBUF*                  input_sql;          /**< Input query */
} MAXROWS_SESSION_DATA;

static MAXROWS_SESSION_DATA* maxrows_session_data_create(MAXROWS_INSTANCE* instance,
                                                         MXS_SESSION* session);
static void maxrows_session_data_free(MAXROWS_SESSION_DATA* data);

static int  handle_expecting_fields(MAXROWS_SESSION_DATA* csdata);
static int  handle_expecting_nothing(MAXROWS_SESSION_DATA* csdata);
static int  handle_expecting_response(MAXROWS_SESSION_DATA* csdata);
static int  handle_rows(MAXROWS_SESSION_DATA* csdata, GWBUF* buffer, size_t extra_offset);
static int  handle_ignoring_response(MAXROWS_SESSION_DATA* csdata);
static bool process_params(char** options,
                           MXS_CONFIG_PARAMETER* params,
                           MAXROWS_CONFIG* config);

static int send_upstream(MAXROWS_SESSION_DATA* csdata);
static int send_eof_upstream(MAXROWS_SESSION_DATA* csdata);
static int send_error_upstream(MAXROWS_SESSION_DATA* csdata);
static int send_maxrows_reply_limit(MAXROWS_SESSION_DATA* csdata);

/* API BEGIN */

/**
 * Create an instance of the maxrows filter for a particular service
 * within MaxScale.
 *
 * @param name     The name of the instance (as defined in the config file).
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    MAXROWS_INSTANCE* cinstance = static_cast<MAXROWS_INSTANCE*>(MXS_CALLOC(1, sizeof(MAXROWS_INSTANCE)));

    if (cinstance)
    {
        cinstance->name = name;
        cinstance->config.max_resultset_rows = params->get_integer("max_resultset_rows");
        cinstance->config.max_resultset_size = config_get_size(params,
                                                               "max_resultset_size");
        cinstance->config.m_return =
            static_cast<maxrows_return_mode>(config_get_enum(params,
                                                             "max_resultset_return",
                                                             return_option_values));
        cinstance->config.debug = params->get_integer("debug");
    }

    return (MXS_FILTER*)cinstance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The maxrows instance data
 * @param session   The session itself
 *
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = maxrows_session_data_create(cinstance, session);

    return (MXS_FILTER_SESSION*)csdata;
}

/**
 * A session has been closed.
 *
 * @param instance  The maxrows instance data
 * @param sdata     The session data of the session being closed
 */
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* sdata)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;
}

/**
 * Free the session data.
 *
 * @param instance  The maxrows instance data
 * @param sdata     The session data of the session being closed
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* sdata)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    maxrows_session_data_free(csdata);
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance    The maxrowsinstance data
 * @param sdata       The session data of the session
 * @param down        The downstream filter or router
 */
static void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* sdata, MXS_DOWNSTREAM* down)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    csdata->down = *down;
}

/**
 * Set the upstream component for this filter.
 *
 * @param instance    The maxrows instance data
 * @param sdata       The session data of the session
 * @param up          The upstream filter or router
 */
static void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* sdata, MXS_UPSTREAM* up)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    csdata->up = *up;
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param buffer    Buffer containing an MySQL protocol packet.
 */
static int routeQuery(MXS_FILTER* instance,
                      MXS_FILTER_SESSION* sdata,
                      GWBUF* packet)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    uint8_t* data = GWBUF_DATA(packet);

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    mxb_assert(GWBUF_IS_CONTIGUOUS(packet));
    mxb_assert(GWBUF_LENGTH(packet) >= MYSQL_HEADER_LEN + 1);
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(data)
               + MYSQL_HEADER_LEN == GWBUF_LENGTH(packet));

    maxrows_response_state_reset(&csdata->res);
    csdata->state = MAXROWS_IGNORING_RESPONSE;
    csdata->large_packet = false;
    csdata->discard_resultset = false;
    // Set buffer size to 0
    csdata->res.length = 0;

    switch ((int)MYSQL_GET_COMMAND(data))
    {
    case MXS_COM_QUERY:
    case MXS_COM_STMT_EXECUTE:
        {
            /* Set input query only with MAXROWS_RETURN_ERR */
            if (csdata->instance->config.m_return == MAXROWS_RETURN_ERR
                && (csdata->input_sql = gwbuf_clone(packet)) == NULL)
            {
                csdata->state = MAXROWS_EXPECTING_NOTHING;

                /* Abort client connection on copy failure */
                poll_fake_hangup_event(csdata->session->client_dcb);
                gwbuf_free(csdata->res.data);
                gwbuf_free(packet);
                MXS_FREE(csdata);
                csdata->res.data = NULL;
                packet = NULL;
                csdata = NULL;
                return 0;
            }

            csdata->state = MAXROWS_EXPECTING_RESPONSE;
            break;
        }

    default:
        break;
    }

    if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
    {
        MXS_NOTICE("Maxrows filter is sending data.");
    }

    return csdata->down.routeQuery(csdata->down.instance,
                                   csdata->down.session,
                                   packet);
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param queue     The query data
 */
static int clientReply(MXS_FILTER* instance,
                       MXS_FILTER_SESSION* sdata,
                       GWBUF* data)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    int rv;

    if (csdata->res.data)
    {
        if (csdata->discard_resultset
            && csdata->state == MAXROWS_EXPECTING_ROWS)
        {
            gwbuf_free(csdata->res.data);
            csdata->res.data = data;
            csdata->res.length = gwbuf_length(data);
            csdata->res.offset = 0;
        }
        else
        {
            gwbuf_append(csdata->res.data, data);
            csdata->res.length += gwbuf_length(data);
        }
    }
    else
    {
        csdata->res.data = data;
        csdata->res.length = gwbuf_length(data);
    }

    if (csdata->state != MAXROWS_IGNORING_RESPONSE)
    {
        if (!csdata->discard_resultset)
        {
            if (csdata->res.length > csdata->instance->config.max_resultset_size)
            {
                if (csdata->instance->config.debug & MAXROWS_DEBUG_DISCARDING)
                {
                    MXS_NOTICE("Current size %luB of resultset, at least as much "
                               "as maximum allowed size %uKiB. Not returning data.",
                               csdata->res.length,
                               csdata->instance->config.max_resultset_size / 1024);
                }

                csdata->discard_resultset = true;
            }
        }
    }

    switch (csdata->state)
    {
    case MAXROWS_EXPECTING_FIELDS:
        rv = handle_expecting_fields(csdata);
        break;

    case MAXROWS_EXPECTING_NOTHING:
        rv = handle_expecting_nothing(csdata);
        break;

    case MAXROWS_EXPECTING_RESPONSE:
        rv = handle_expecting_response(csdata);
        break;

    case MAXROWS_EXPECTING_ROWS:
        rv = handle_rows(csdata, data, 0);
        break;

    case MAXROWS_IGNORING_RESPONSE:
        rv = handle_ignoring_response(csdata);
        break;

    default:
        MXS_ERROR("Internal filter logic broken, unexpected state: %d",
                  csdata->state);
        mxb_assert(!true);
        rv = send_upstream(csdata);
        maxrows_response_state_reset(&csdata->res);
        csdata->state = MAXROWS_IGNORING_RESPONSE;
    }

    return rv;
}

/**
 * Diagnostics routine
 *
 * If csdata is NULL then print diagnostics on the instance as a whole,
 * otherwise print diagnostics for the particular session.
 *
 * @param instance  The filter instance
 * @param fsession  Filter session, may be NULL
 * @param dcb       The DCB for diagnostic output
 */
static void diagnostics(MXS_FILTER* instance, MXS_FILTER_SESSION* sdata, DCB* dcb)
{
    MAXROWS_INSTANCE* cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA* csdata = (MAXROWS_SESSION_DATA*)sdata;

    dcb_printf(dcb, "Maxrows filter is working\n");
}

/**
 * Diagnostics routine
 *
 * If csdata is NULL then print diagnostics on the instance as a whole,
 * otherwise print diagnostics for the particular session.
 *
 * @param instance  The filter instance
 * @param fsession  Filter session, may be NULL
 * @param dcb       The DCB for diagnostic output
 */
static json_t* diagnostics_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* sdata)
{
    return NULL;
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

/* API END */

/**
 * Reset maxrows response state
 *
 * @param state Pointer to object.
 */
static void maxrows_response_state_reset(MAXROWS_RESPONSE_STATE* state)
{
    state->data = NULL;
    state->n_totalfields = 0;
    state->n_fields = 0;
    state->n_rows = 0;
    state->offset = 0;
    state->column_defs = NULL;
}

/**
 * Create maxrows session data
 *
 * @param instance The maxrows instance this data is associated with.
 *
 * @return Session data or NULL if creation fails.
 */
static MAXROWS_SESSION_DATA* maxrows_session_data_create(MAXROWS_INSTANCE* instance,
                                                         MXS_SESSION* session)
{
    MAXROWS_SESSION_DATA* data = (MAXROWS_SESSION_DATA*)MXS_CALLOC(1, sizeof(MAXROWS_SESSION_DATA));

    if (data)
    {
        mxb_assert(session->client_dcb);
        mxb_assert(session->client_dcb->data);

        MYSQL_session* mysql_session = (MYSQL_session*)session->client_dcb->data;
        data->instance = instance;
        data->session = session;
        data->input_sql = NULL;
        data->state = MAXROWS_EXPECTING_NOTHING;
    }

    return data;
}

/**
 * Free maxrows session data.
 *
 * @param A maxrows session data previously allocated using session_data_create().
 */
static void maxrows_session_data_free(MAXROWS_SESSION_DATA* data)
{
    if (data)
    {
        MXS_FREE(data);
    }
}

/**
 * Called when resultset field information is handled.
 *
 * @param csdata The maxrows session data.
 */
static int handle_expecting_fields(MAXROWS_SESSION_DATA* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_FIELDS);
    mxb_assert(csdata->res.data);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = csdata->res.length;

    while (!insufficient && (buflen - csdata->res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(csdata->res.data,
                        csdata->res.offset,
                        MYSQL_HEADER_LEN + 1,
                        header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (csdata->res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe:      // EOF, the one after the fields.
                csdata->res.offset += packetlen;

                /**
                 * Set the buffer with column definitions.
                 * This will be used only by the empty response handler.
                 */
                if (!csdata->res.column_defs
                    && csdata->instance->config.m_return == MAXROWS_RETURN_EMPTY)
                {
                    csdata->res.column_defs = gwbuf_clone(csdata->res.data);
                }

                csdata->state = MAXROWS_EXPECTING_ROWS;
                rv = handle_rows(csdata, csdata->res.data, csdata->res.offset);
                break;

            default:    // Field information.
                csdata->res.offset += packetlen;
                ++csdata->res.n_fields;
                mxb_assert(csdata->res.n_fields <= csdata->res.n_totalfields);
                break;
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    return rv;
}

/**
 * Called when data is received (even if nothing is expected) from the server.
 *
 * @param csdata The maxrows session data.
 */
static int handle_expecting_nothing(MAXROWS_SESSION_DATA* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_NOTHING);
    mxb_assert(csdata->res.data);
    unsigned long msg_size = gwbuf_length(csdata->res.data);

    if ((int)MYSQL_GET_COMMAND(GWBUF_DATA(csdata->res.data)) == 0xff)
    {
        /**
         * Error text message is after:
         * MYSQL_HEADER_LEN offset + status flag (1) + error code (2) +
         * 6 bytes message status = MYSQL_HEADER_LEN + 9
         */
        MXS_INFO("Error packet received from backend "
                 "(possibly a server shut down ?): [%.*s].",
                 (int)msg_size - (MYSQL_HEADER_LEN + 9),
                 GWBUF_DATA(csdata->res.data) + MYSQL_HEADER_LEN + 9);
    }
    else
    {
        MXS_WARNING("Received data from the backend although "
                    "filter is expecting nothing. "
                    "Packet size is %lu bytes long.",
                    msg_size);
        mxb_assert(!true);
    }

    return send_upstream(csdata);
}

/**
 * Called when a response is received from the server.
 *
 * @param csdata The maxrows session data.
 */
static int handle_expecting_response(MAXROWS_SESSION_DATA* csdata)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_RESPONSE);
    mxb_assert(csdata->res.data);

    int rv = 1;
    size_t buflen = csdata->res.length;

    // Reset field counters
    csdata->res.n_fields = 0;
    csdata->res.n_totalfields = 0;
    // Reset large packet var
    csdata->large_packet = false;

    if (buflen >= MYSQL_HEADER_LEN + 1)     // We need the command byte.
    {
        // Reserve enough space to accomodate for the largest length encoded integer,
        // which is type field + 8 bytes.
        uint8_t header[MYSQL_HEADER_LEN + 1 + 8];

        // Read packet header from buffer at current offset
        gwbuf_copy_data(csdata->res.data,
                        csdata->res.offset,
                        MYSQL_HEADER_LEN + 1,
                        header);

        switch ((int)MYSQL_GET_COMMAND(header))
        {
        case 0x00:      // OK
        case 0xff:      // ERR
            /**
             * This also handles the OK packet that terminates
             * a Multi-Resultset seen in handle_rows()
             */
            if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
            {
                if (csdata->res.n_rows)
                {
                    MXS_NOTICE("OK or ERR seen. The resultset has %lu rows.%s",
                               csdata->res.n_rows,
                               csdata->discard_resultset ? " [Discarded]" : "");
                }
                else
                {
                    MXS_NOTICE("OK or ERR");
                }
            }

            if (csdata->discard_resultset)
            {
                rv = send_maxrows_reply_limit(csdata);
                csdata->state = MAXROWS_EXPECTING_NOTHING;
            }
            else
            {
                rv = send_upstream(csdata);
                csdata->state = MAXROWS_IGNORING_RESPONSE;
            }
            break;

        case 0xfb:      // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
            if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
            {
                MXS_NOTICE("GET_MORE_CLIENT_DATA");
            }
            rv = send_upstream(csdata);
            csdata->state = MAXROWS_IGNORING_RESPONSE;
            break;

        default:
            if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
            {
                MXS_NOTICE("RESULTSET");
            }

            if (csdata->res.n_totalfields != 0)
            {
                // We've seen the header and have figured out how many fields there are.
                csdata->state = MAXROWS_EXPECTING_FIELDS;
                rv = handle_expecting_fields(csdata);
            }
            else
            {
                // mxs_leint_bytes() returns the length of the int type field + the size of the
                // integer.
                size_t n_bytes = mxs_leint_bytes(&header[4]);

                if (MYSQL_HEADER_LEN + n_bytes <= buflen)
                {
                    // Now we can figure out how many fields there are, but first we
                    // need to copy some more data.
                    gwbuf_copy_data(csdata->res.data,
                                    MYSQL_HEADER_LEN + 1,
                                    n_bytes - 1,
                                    &header[MYSQL_HEADER_LEN + 1]);

                    csdata->res.n_totalfields = mxs_leint_value(&header[4]);
                    csdata->res.offset += MYSQL_HEADER_LEN + n_bytes;

                    csdata->state = MAXROWS_EXPECTING_FIELDS;
                    rv = handle_expecting_fields(csdata);
                }
                else
                {
                    // We need more data. We will be called again, when data is available.
                }
            }
            break;
        }
    }

    return rv;
}

/**
 * Called when resultset rows are handled
 *
 * @param csdata       The maxrows session data
 * @param buffer       The buffer containing the packet
 * @param extra_offset Offset into @c buffer where the packet is stored
 *
 * @return The return value of the upstream component
 */
static int handle_rows(MAXROWS_SESSION_DATA* csdata, GWBUF* buffer, size_t extra_offset)
{
    mxb_assert(csdata->state == MAXROWS_EXPECTING_ROWS);
    mxb_assert(csdata->res.data);

    int rv = 1;
    bool insufficient = false;
    size_t offset = extra_offset;
    size_t buflen = gwbuf_length(buffer);

    while (!insufficient && (buflen - offset >= MYSQL_HEADER_LEN))
    {
        bool pending_large_data = csdata->large_packet;
        // header array holds a full EOF packet
        uint8_t header[MYSQL_EOF_PACKET_LEN];
        gwbuf_copy_data(buffer,
                        offset,
                        MYSQL_EOF_PACKET_LEN,
                        header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(header);

        if (offset + packetlen <= buflen)
        {
            /* Check for large packet packet terminator:
             * min is 4 bytes "0x0 0x0 0x0 0xseq_no and
             * max is 1 byte less than EOF_PACKET_LEN
             * If true skip data processing.
             */
            if (pending_large_data
                && (packetlen >= MYSQL_HEADER_LEN
                    && packetlen < MYSQL_EOF_PACKET_LEN))
            {
                // Update offset, number of rows and break
                offset += packetlen;
                csdata->res.n_rows++;
                break;
            }

            /*
             * Check packet size against MYSQL_PACKET_LENGTH_MAX
             * If true then break as received could be not complete
             * EOF or OK packet could be seen after receiving the full large packet
             */
            if (packetlen == (MYSQL_PACKET_LENGTH_MAX + MYSQL_HEADER_LEN))
            {
                // Mark the beginning of a large packet receiving
                csdata->large_packet = true;
                // Just update offset and break
                offset += packetlen;
                break;
            }
            else
            {
                // Reset large packet indicator
                csdata->large_packet = false;
            }

            // We have at least one complete packet and we can process the command byte.
            int command = (int)MYSQL_GET_COMMAND(header);
            int flags = 0;

            switch (command)
            {
            case 0xff:      // ERR packet after the rows.
                offset += packetlen;

                // This is the end of resultset: set big packet var to false
                csdata->large_packet = false;

                if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
                {
                    MXS_NOTICE("Error packet seen while handling result set");
                }

                /*
                 * This is the ERR packet that could terminate a Multi-Resultset.
                 */

                // Send data in buffer or empty resultset
                if (csdata->discard_resultset)
                {
                    rv = send_maxrows_reply_limit(csdata);
                }
                else
                {
                    rv = send_upstream(csdata);
                }

                csdata->state = MAXROWS_EXPECTING_NOTHING;

                break;

            /* OK could the last packet in the Multi-Resultset transmission:
             * this is handled by handle_expecting_response()
             *
             * It could also be sent instead of EOF from as in MySQL 5.7.5
             * if client sends CLIENT_DEPRECATE_EOF capability OK packet could
             * have the SERVER_MORE_RESULTS_EXIST flag.
             * Flags in the OK packet are at the same offset as in EOF.
             *
             * NOTE: not supported right now
             */
            case 0xfe:      // EOF, the one after the rows.
                offset += packetlen;

                /* EOF could be the last packet in the transmission:
                 * check first whether SERVER_MORE_RESULTS_EXIST flag is set.
                 * If so more results set could come. The end of stream
                 * will be an OK packet.
                 */
                if (packetlen < MYSQL_EOF_PACKET_LEN)
                {
                    MXS_ERROR("EOF packet has size of %lu instead of %d",
                              packetlen,
                              MYSQL_EOF_PACKET_LEN);
                    rv = send_maxrows_reply_limit(csdata);
                    csdata->state = MAXROWS_EXPECTING_NOTHING;
                    break;
                }

                flags = gw_mysql_get_byte2(header + MAXROWS_MYSQL_EOF_PACKET_FLAGS_OFFSET);

                // Check whether the EOF terminates the resultset or indicates MORE_RESULTS
                if (!(flags & SERVER_MORE_RESULTS_EXIST))
                {
                    // End of the resultset
                    if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
                    {
                        MXS_NOTICE("OK or EOF packet seen: the resultset has %lu rows.%s",
                                   csdata->res.n_rows,
                                   csdata->discard_resultset ? " [Discarded]" : "");
                    }

                    // Discard data or send data
                    if (csdata->discard_resultset)
                    {
                        rv = send_maxrows_reply_limit(csdata);
                    }
                    else
                    {
                        rv = send_upstream(csdata);
                    }

                    csdata->state = MAXROWS_EXPECTING_NOTHING;
                }
                else
                {
                    /*
                     * SERVER_MORE_RESULTS_EXIST flag is present: additional resultsets will come.
                     *
                     * Note: the OK packet that terminates the Multi-Resultset
                     * is handled by handle_expecting_response()
                     */

                    csdata->state = MAXROWS_EXPECTING_RESPONSE;

                    if (csdata->instance->config.debug & MAXROWS_DEBUG_DECISIONS)
                    {
                        MXS_NOTICE("EOF or OK packet seen with SERVER_MORE_RESULTS_EXIST flag:"
                                   " waiting for more data (%lu rows so far)",
                                   csdata->res.n_rows);
                    }
                }

                break;

            case 0xfb:  // NULL
            default:    // length-encoded-string
                offset += packetlen;
                // Increase res.n_rows counter while not receiving large packets
                if (!csdata->large_packet)
                {
                    csdata->res.n_rows++;
                }

                // Check for max_resultset_rows limit
                if (!csdata->discard_resultset)
                {
                    if (csdata->res.n_rows > csdata->instance->config.max_resultset_rows)
                    {
                        if (csdata->instance->config.debug & MAXROWS_DEBUG_DISCARDING)
                        {
                            MXS_INFO("max_resultset_rows %lu reached, not returning the resultset.",
                                     csdata->res.n_rows);
                        }

                        // Set the discard indicator
                        csdata->discard_resultset = true;
                    }
                }
                break;
            }
        }
        else
        {
            // We need more data
            insufficient = true;
        }
    }

    csdata->res.offset += offset - extra_offset;

    return rv;
}

/**
 * Called when all data from the server is ignored.
 *
 * @param csdata The maxrows session data.
 */
static int handle_ignoring_response(MAXROWS_SESSION_DATA* csdata)
{
    mxb_assert(csdata->state == MAXROWS_IGNORING_RESPONSE);
    mxb_assert(csdata->res.data);

    return send_upstream(csdata);
}

/**
 * Send data upstream.
 *
 * @param csdata Session data
 *
 * @return Whatever the upstream returns.
 */
static int send_upstream(MAXROWS_SESSION_DATA* csdata)
{
    mxb_assert(csdata->res.data != NULL);

    /* Free a saved SQL not freed by send_error_upstream() */
    if (csdata->instance->config.m_return == MAXROWS_RETURN_ERR)
    {
        gwbuf_free(csdata->input_sql);
        csdata->input_sql = NULL;
    }

    /* Free a saved columndefs not freed by send_eof_upstream() */
    if (csdata->instance->config.m_return == MAXROWS_RETURN_EMPTY)
    {
        gwbuf_free(csdata->res.column_defs);
        csdata->res.column_defs = NULL;
    }

    /* Send data to client */
    int rv = csdata->up.clientReply(csdata->up.instance,
                                    csdata->up.session,
                                    csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Send upstream the Response Buffer up to columns def in response
 * including its EOF of the first result set
 * An EOF packet for empty result set with no MULTI flags is added
 * at the end.
 *
 * @param csdata    Session data
 *
 * @return          Non-Zero if successful, 0 on errors
 */
static int send_eof_upstream(MAXROWS_SESSION_DATA* csdata)
{
    int rv = -1;
    /* Sequence byte is #3 */
    uint8_t eof[MYSQL_EOF_PACKET_LEN] = {05, 00, 00, 01, 0xfe, 00, 00, 02, 00};
    GWBUF* new_pkt = NULL;

    mxb_assert(csdata->res.data != NULL);
    mxb_assert(csdata->res.column_defs != NULL);

    /**
     * The offset to server reply pointing to
     * next byte after column definitions EOF
     * of the first result set.
     */
    size_t offset = gwbuf_length(csdata->res.column_defs);

    /* Data to send + added EOF */
    uint8_t* new_result = static_cast<uint8_t*>(MXS_MALLOC(offset + MYSQL_EOF_PACKET_LEN));

    if (new_result)
    {
        /* Get contiguous data from saved columns defintions buffer */
        gwbuf_copy_data(csdata->res.column_defs, 0, offset, new_result);

        /* Increment sequence number for the EOF being added for empty resultset:
         * last one if found in EOF terminating column def
         */
        eof[3] = new_result[offset - (MYSQL_EOF_PACKET_LEN - 3)] + 1;

        /* Copy EOF data */
        memcpy(new_result + offset, &eof, MYSQL_EOF_PACKET_LEN);

        /* Create new packet */
        new_pkt = gwbuf_alloc_and_load(offset + MYSQL_EOF_PACKET_LEN, new_result);

        /* Free intermediate data */
        MXS_FREE(new_result);

        if (new_pkt)
        {
            /* new_pkt will be freed by write routine */
            rv = csdata->up.clientReply(csdata->up.instance,
                                        csdata->up.session,
                                        new_pkt);
        }
    }

    /* Abort client connection */
    if (!(new_result && new_pkt))
    {
        /* Abort client connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        rv = 0;
    }

    /* Free all data buffers */
    gwbuf_free(csdata->res.data);
    gwbuf_free(csdata->res.column_defs);

    csdata->res.data = NULL;
    csdata->res.column_defs = NULL;

    return rv;
}

/**
 * Send OK packet data upstream.
 *
 * @param csdata    Session data
 *
 * @return          Non-Zero if successful, 0 on errors
 */
static int send_ok_upstream(MAXROWS_SESSION_DATA* csdata)
{
    /* Note: sequence id is always 01 (4th byte) */
    const static uint8_t ok[MYSQL_OK_PACKET_MIN_LEN] = {07, 00, 00, 01, 00, 00,
                                                        00, 02, 00, 00, 00};

    mxb_assert(csdata->res.data != NULL);

    GWBUF* packet = gwbuf_alloc(MYSQL_OK_PACKET_MIN_LEN);
    if (!packet)
    {
        /* Abort clienrt connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        gwbuf_free(csdata->res.data);
        csdata->res.data = NULL;
        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(packet);
    memcpy(ptr, &ok, MYSQL_OK_PACKET_MIN_LEN);

    mxb_assert(csdata->res.data != NULL);

    int rv = csdata->up.clientReply(csdata->up.instance,
                                    csdata->up.session,
                                    packet);

    /* Free server result buffer */
    gwbuf_free(csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Send ERR packet data upstream.
 *
 * An error packet is sent to client including
 * a message prefix plus the original SQL input
 *
 * @param   csdata    Session data
 * @return            Non-Zero if successful, 0 on errors
 */
static int send_error_upstream(MAXROWS_SESSION_DATA* csdata)
{
    GWBUF* err_pkt;
    uint8_t hdr_err[MYSQL_ERR_PACKET_MIN_LEN];
    unsigned long bytes_copied;
    const char* err_msg_prefix = "Row limit/size exceeded for query: ";
    int err_prefix_len = strlen(err_msg_prefix);
    unsigned long pkt_len = MYSQL_ERR_PACKET_MIN_LEN + err_prefix_len;
    unsigned long sql_len = gwbuf_length(csdata->input_sql)
        - (MYSQL_HEADER_LEN + 1);
    /**
     * The input SQL statement added in the error message
     * has a limit of MAXROWS_INPUT_SQL_MAX_LEN bytes
     */
    sql_len = (sql_len > MAXROWS_INPUT_SQL_MAX_LEN) ?
        MAXROWS_INPUT_SQL_MAX_LEN : sql_len;
    uint8_t sql[sql_len];

    mxb_assert(csdata->res.data != NULL);

    pkt_len += sql_len;

    bytes_copied = gwbuf_copy_data(csdata->input_sql,
                                   MYSQL_HEADER_LEN + 1,
                                   sql_len,
                                   sql);

    if (!bytes_copied
        || (err_pkt = gwbuf_alloc(MYSQL_HEADER_LEN + pkt_len)) == NULL)
    {
        /* Abort client connection */
        poll_fake_hangup_event(csdata->session->client_dcb);
        gwbuf_free(csdata->res.data);
        gwbuf_free(csdata->input_sql);
        csdata->res.data = NULL;
        csdata->input_sql = NULL;

        return 0;
    }

    uint8_t* ptr = GWBUF_DATA(err_pkt);
    memcpy(ptr, &hdr_err, MYSQL_ERR_PACKET_MIN_LEN);
    unsigned int err_errno = 1415;
    char err_state[7] = "#0A000";

    /* Set the payload length of the whole error message */
    gw_mysql_set_byte3(&ptr[0], pkt_len);
    /* Note: sequence id is always 01 (4th byte) */
    ptr[3] = 1;
    /* Error indicator */
    ptr[4] = 0xff;
    /* MySQL error code: 2 bytes */
    gw_mysql_set_byte2(&ptr[5], err_errno);
    /* Status Message 6 bytes */
    memcpy((char*)&ptr[7], err_state, 6);
    /* Copy error message prefix */
    memcpy(&ptr[13], err_msg_prefix, err_prefix_len);
    /* Copy SQL input */
    memcpy(&ptr[13 + err_prefix_len], sql, sql_len);

    int rv = csdata->up.clientReply(csdata->up.instance,
                                    csdata->up.session,
                                    err_pkt);

    /* Free server result buffer */
    gwbuf_free(csdata->res.data);
    csdata->res.data = NULL;

    /* Free input_sql buffer */
    gwbuf_free(csdata->input_sql);
    csdata->input_sql = NULL;

    return rv;
}

/**
 * Send the proper reply to client when the maxrows
 * limit/size is hit.
 *
 * @param   csdata    Session data
 * @return            Non-Zero if successful, 0 on errors
 */
static int send_maxrows_reply_limit(MAXROWS_SESSION_DATA* csdata)
{
    switch (csdata->instance->config.m_return)
    {
    case MAXROWS_RETURN_EMPTY:
        return send_eof_upstream(csdata);
        break;

    case MAXROWS_RETURN_OK:
        return send_ok_upstream(csdata);
        break;

    case MAXROWS_RETURN_ERR:
        return send_error_upstream(csdata);
        break;

    default:
        MXS_ERROR("MaxRows config value not expected!");
        mxb_assert(!true);
        return 0;
        break;
    }
}
