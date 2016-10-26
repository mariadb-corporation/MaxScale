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

/**
 * @file maxrows.c - Result set limit Filter
 * @verbatim
 *
 *
 * The filter returns a void result set if the number of rows in the result set
 * from backend exceeds the max_rows parameter.
 *
 * Date         Who                   Description
 * 26/10/2016   Massimiliano Pinto    Initial implementation
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "maxrows"
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/mysql_utils.h>
#include <maxscale/query_classifier.h>
#include <stdbool.h>
#include <stdint.h>
#include <maxscale/buffer.h>
#include <maxscale/protocol/mysql.h>
#include <maxscale/debug.h>
#include "maxrows.h"

static char VERSION_STRING[] = "V1.0.0";

static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **);
static void   *newSession(FILTER *instance, SESSION *session);
static void    closeSession(FILTER *instance, void *sdata);
static void    freeSession(FILTER *instance, void *sdata);
static void    setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *downstream);
static void    setUpstream(FILTER *instance, void *sdata, UPSTREAM *upstream);
static int     routeQuery(FILTER *instance, void *sdata, GWBUF *queue);
static int     clientReply(FILTER *instance, void *sdata, GWBUF *queue);
static void    diagnostics(FILTER *instance, void *sdata, DCB *dcb);
static uint64_t getCapabilities(void);

#define C_DEBUG(format, ...) MXS_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)

/* Global symbols of the Module */

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_IN_DEVELOPMENT,
    FILTER_VERSION,
    "A filter that is capable of limiting the resultset number of rows."
};

char *version()
{
    return VERSION_STRING;
}

/**
 * The module initialization functions, called when the module has
 * been loaded.
 */
void ModuleInit()
{
}

/**
 * The module entry point function, called when the module is loaded.
 *
 * @return The module object.
 */
FILTER_OBJECT *GetModuleObject()
{
    static FILTER_OBJECT object =
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
            getCapabilities
        };

    return &object;
};

/* Implementation */

typedef struct maxrows_config
{
    uint32_t    max_resultset_rows;
    uint32_t    max_resultset_size;
    uint32_t    debug;
} MAXROWS_CONFIG;

static const MAXROWS_CONFIG DEFAULT_CONFIG =
{
    MAXROWS_DEFAULT_MAX_RESULTSET_ROWS,
    MAXROWS_DEFAULT_MAX_RESULTSET_SIZE,
    MAXROWS_DEFAULT_DEBUG
};

typedef struct maxrows_instance
{
    const char            *name;
    MAXROWS_CONFIG         config;
} MAXROWS_INSTANCE;

typedef enum maxrows_session_state
{
    MAXROWS_EXPECTING_RESPONSE,     // A select has been sent, and we are waiting for the response.
    MAXROWS_EXPECTING_FIELDS,       // A select has been sent, and we want more fields.
    MAXROWS_EXPECTING_ROWS,         // A select has been sent, and we want more rows.
    MAXROWS_EXPECTING_NOTHING,      // We are not expecting anything from the server.
    MAXROWS_EXPECTING_USE_RESPONSE, // A "USE DB" was issued.
    MAXROWS_IGNORING_RESPONSE,      // We are not interested in the data received from the server.
} maxrows_session_state_t;

typedef struct maxrows_response_state
{
    GWBUF* data;          /**< Response data, possibly incomplete. */
    size_t n_totalfields; /**< The number of fields a resultset contains. */
    size_t n_fields;      /**< How many fields we have received, <= n_totalfields. */
    size_t n_rows;        /**< How many rows we have received. */
    size_t offset;        /**< Where we are in the response buffer. */
} MAXROWS_RESPONSE_STATE;

static void maxrows_response_state_reset(MAXROWS_RESPONSE_STATE *state);

typedef struct cache_session_data
{
    MAXROWS_INSTANCE      *instance;   /**< The cache instance the session is associated with. */
    DOWNSTREAM             down;       /**< The previous filter or equivalent. */
    UPSTREAM               up;         /**< The next filter or equivalent. */
    MAXROWS_RESPONSE_STATE res;        /**< The response state. */
    SESSION               *session;    /**< The session this data is associated with. */
    char                  *default_db; /**< The default database. */
    char                  *use_db;     /**< Pending default database. Needs server response. */
    maxrows_session_state_t state;
} MAXROWS_SESSION_DATA;

static MAXROWS_SESSION_DATA *maxrows_session_data_create(MAXROWS_INSTANCE *instance, SESSION *session);
static void maxrows_session_data_free(MAXROWS_SESSION_DATA *data);

static int handle_expecting_fields(MAXROWS_SESSION_DATA *csdata);
static int handle_expecting_nothing(MAXROWS_SESSION_DATA *csdata);
static int handle_expecting_response(MAXROWS_SESSION_DATA *csdata);
static int handle_expecting_rows(MAXROWS_SESSION_DATA *csdata);
static int handle_expecting_use_response(MAXROWS_SESSION_DATA *csdata);
static int handle_ignoring_response(MAXROWS_SESSION_DATA *csdata);
static bool process_params(char **options, FILTER_PARAMETER **params, MAXROWS_CONFIG* config);

static int send_upstream(MAXROWS_SESSION_DATA *csdata);
static int send_ok_upstream(MAXROWS_SESSION_DATA *csdata);

/* API BEGIN */

/**
 * Create an instance of the cache filter for a particular service
 * within MaxScale.
 *
 * @param name     The name of the instance (as defined in the config file).
 * @param options  The options for this filter
 * @param params   The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *createInstance(const char *name, char **options, FILTER_PARAMETER **params)
{
    MAXROWS_INSTANCE *cinstance = NULL;
    MAXROWS_CONFIG config = DEFAULT_CONFIG;

    if (process_params(options, params, &config))
    {
        cinstance = MXS_CALLOC(1, sizeof(MAXROWS_INSTANCE));
        if (cinstance)
        {
            cinstance->name = name;
            cinstance->config = config;
        }
    }

    return (FILTER*)cinstance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The cache instance data
 * @param session   The session itself
 *
 * @return Session specific data for this session
 */
static void *newSession(FILTER *instance, SESSION *session)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = maxrows_session_data_create(cinstance, session);

    return csdata;
}

/**
 * A session has been closed.
 *
 * @param instance  The cache instance data
 * @param sdata     The session data of the session being closed
 */
static void closeSession(FILTER *instance, void *sdata)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;
}

/**
 * Free the session data.
 *
 * @param instance  The cache instance data
 * @param sdata     The session data of the session being closed
 */
static void freeSession(FILTER *instance, void *sdata)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    maxrows_session_data_free(csdata);
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance    The cache instance data
 * @param sdata       The session data of the session
 * @param down        The downstream filter or router
 */
static void setDownstream(FILTER *instance, void *sdata, DOWNSTREAM *down)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    csdata->down = *down;
}

/**
 * Set the upstream component for this filter.
 *
 * @param instance    The cache instance data
 * @param sdata       The session data of the session
 * @param up          The upstream filter or router
 */
static void setUpstream(FILTER *instance, void *sdata, UPSTREAM *up)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    csdata->up = *up;
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param buffer    Buffer containing an MySQL protocol packet.
 */
static int routeQuery(FILTER *instance, void *sdata, GWBUF *packet)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    uint8_t *data = GWBUF_DATA(packet);

    // All of these should be guaranteed by RCAP_TYPE_TRANSACTION_TRACKING
    ss_dassert(GWBUF_IS_CONTIGUOUS(packet));
    ss_dassert(GWBUF_LENGTH(packet) >= MYSQL_HEADER_LEN + 1);
    ss_dassert(MYSQL_GET_PACKET_LEN(data) + MYSQL_HEADER_LEN == GWBUF_LENGTH(packet));

    bool use_default = true;

    maxrows_response_state_reset(&csdata->res);
    csdata->state = MAXROWS_IGNORING_RESPONSE;

    int rv;

    switch ((int)MYSQL_GET_COMMAND(data))
    {
    case MYSQL_COM_INIT_DB:
        {
            ss_dassert(!csdata->use_db);
            size_t len = MYSQL_GET_PACKET_LEN(data) - 1; // Remove the command byte.
            csdata->use_db = MXS_MALLOC(len + 1);

            if (csdata->use_db)
            {
                memcpy(csdata->use_db, data + MYSQL_HEADER_LEN + 1, len);
                csdata->use_db[len] = 0;
                csdata->state = MAXROWS_EXPECTING_USE_RESPONSE;
            }
            else
            {
                /* Do we need to handle it? */
            }
        }
        break;

    case MYSQL_COM_QUERY:
        {
            /* Detect the SELECT statement only */
            if (qc_get_operation(packet) == QUERY_OP_SELECT)
            {
                SESSION *session = csdata->session;

                if ((session_is_autocommit(session) && !session_trx_is_active(session)) ||
                    session_trx_is_read_only(session))
                {
                    /* Waiting for a reply:
                     * Data will be stored in csdata->res->data via
                     * clientReply routine
                     */
                    csdata->state = MAXROWS_EXPECTING_RESPONSE;
                }
                else
                {
                    C_DEBUG("autocommit = %s and transaction state %s => Not using memory buffer for resultset.",
                            session_is_autocommit(csdata->session) ? "ON" : "OFF",
                            session_trx_state_to_string(session_get_trx_state(csdata->session)));
                }
            }
            break;

        default:
            break;
        }
    }

    if (use_default)
    {
        C_DEBUG("Maxrows filter is sends data.");
        rv = csdata->down.routeQuery(csdata->down.instance, csdata->down.session, packet);
    }

    return rv;
}

/**
 * A response on its way to the client is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param queue     The query data
 */
static int clientReply(FILTER *instance, void *sdata, GWBUF *data)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    int rv;

    if (csdata->res.data)
    {
        gwbuf_append(csdata->res.data, data);
    }
    else
    {
        csdata->res.data = data;
    }

    if (csdata->state != MAXROWS_IGNORING_RESPONSE)
    {
        if (gwbuf_length(csdata->res.data) > csdata->instance->config.max_resultset_size)
        {
            C_DEBUG("Current size %uB of resultset, at least as much "
                    "as maximum allowed size %uKiB. Not caching.",
                    gwbuf_length(csdata->res.data),
                    csdata->instance->config.max_resultset_size / 1024);

            csdata->state = MAXROWS_IGNORING_RESPONSE;
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
        rv = handle_expecting_rows(csdata);
        break;

    case MAXROWS_EXPECTING_USE_RESPONSE:
        rv = handle_expecting_use_response(csdata);
        break;

    case MAXROWS_IGNORING_RESPONSE:
        rv = handle_ignoring_response(csdata);
        break;

    default:
        MXS_ERROR("Internal filter logic broken, unexpected state: %d", csdata->state);
        ss_dassert(!true);
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
static void diagnostics(FILTER *instance, void *sdata, DCB *dcb)
{
    MAXROWS_INSTANCE *cinstance = (MAXROWS_INSTANCE*)instance;
    MAXROWS_SESSION_DATA *csdata = (MAXROWS_SESSION_DATA*)sdata;

    dcb_printf(dcb, "Maxrows filter is working\n");
}


/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_STMT_INPUT;
}

/* API END */

/**
 * Reset cache response state
 *
 * @param state Pointer to object.
 */
static void maxrows_response_state_reset(MAXROWS_RESPONSE_STATE *state)
{
    state->data = NULL;
    state->n_totalfields = 0;
    state->n_fields = 0;
    state->n_rows = 0;
    state->offset = 0;
}

/**
 * Create cache session data
 *
 * @param instance The cache instance this data is associated with.
 *
 * @return Session data or NULL if creation fails.
 */
static MAXROWS_SESSION_DATA *maxrows_session_data_create(MAXROWS_INSTANCE *instance,
                                                     SESSION* session)
{
    MAXROWS_SESSION_DATA *data = (MAXROWS_SESSION_DATA*)MXS_CALLOC(1, sizeof(MAXROWS_SESSION_DATA));

    if (data)
    {
        char *default_db = NULL;

        ss_dassert(session->client_dcb);
        ss_dassert(session->client_dcb->data);
        MYSQL_session *mysql_session = (MYSQL_session*)session->client_dcb->data;

        if (mysql_session->db[0] != 0)
        {
            default_db = MXS_STRDUP(mysql_session->db);
        }

        if ((mysql_session->db[0] == 0) || default_db)
        {
            data->instance = instance;
            data->session = session;
            data->state = MAXROWS_EXPECTING_NOTHING;
            data->default_db = default_db;
        }
        else
        {
            MXS_FREE(data);
            data = NULL;
        }
    }

    return data;
}

/**
 * Free cache session data.
 *
 * @param A cache session data previously allocated using session_data_create().
 */
static void maxrows_session_data_free(MAXROWS_SESSION_DATA* data)
{
    if (data)
    {
        // In normal circumstances, only data->default_db may be non-NULL at
        // this point. However, if the authentication with the backend fails
        // and the session is closed, data->use_db may be non-NULL.
        MXS_FREE(data->use_db);
        MXS_FREE(data->default_db);
        MXS_FREE(data);
    }
}

/**
 * Called when resultset field information is handled.
 *
 * @param csdata The cache session data.
 */
static int handle_expecting_fields(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_EXPECTING_FIELDS);
    ss_dassert(csdata->res.data);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = gwbuf_length(csdata->res.data);

    while (!insufficient && (buflen - csdata->res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(csdata->res.data, csdata->res.offset, MYSQL_HEADER_LEN + 1, header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PACKET_LEN(header);

        if (csdata->res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe: // EOF, the one after the fields.
                csdata->res.offset += packetlen;
                csdata->state = MAXROWS_EXPECTING_ROWS;
                rv = handle_expecting_rows(csdata);
                break;

            default: // Field information.
                csdata->res.offset += packetlen;
                ++csdata->res.n_fields;
                ss_dassert(csdata->res.n_fields <= csdata->res.n_totalfields);
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
 * @param csdata The cache session data.
 */
static int handle_expecting_nothing(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_EXPECTING_NOTHING);
    ss_dassert(csdata->res.data);
    MXS_ERROR("Received data from the backend althoug we were expecting nothing.");
    ss_dassert(!true);

    return send_upstream(csdata);
}

/**
 * Called when a response is received from the server.
 *
 * @param csdata The cache session data.
 */
static int handle_expecting_response(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_EXPECTING_RESPONSE);
    ss_dassert(csdata->res.data);

    int rv = 1;

    size_t buflen = gwbuf_length(csdata->res.data);

    if (buflen >= MYSQL_HEADER_LEN + 1) // We need the command byte.
    {
        // Reserve enough space to accomodate for the largest length encoded integer,
        // which is type field + 8 bytes.
        uint8_t header[MYSQL_HEADER_LEN + 1 + 8];
        gwbuf_copy_data(csdata->res.data, 0, MYSQL_HEADER_LEN + 1, header);

        switch ((int)MYSQL_GET_COMMAND(header))
        {
        case 0x00: // OK
        case 0xff: // ERR
            C_DEBUG("OK or ERR");
            rv = send_upstream(csdata);
            csdata->state = MAXROWS_IGNORING_RESPONSE;
            break;

        case 0xfb: // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
            C_DEBUG("GET_MORE_CLIENT_DATA");
            rv = send_upstream(csdata);
            csdata->state = MAXROWS_IGNORING_RESPONSE;
            break;

        default:
            C_DEBUG("RESULTSET");

            if (csdata->res.n_totalfields != 0)
            {
                // We've seen the header and have figured out how many fields there are.
                csdata->state = MAXROWS_EXPECTING_FIELDS;
                rv = handle_expecting_fields(csdata);
            }
            else
            {
                // leint_bytes() returns the length of the int type field + the size of the
                // integer.
                size_t n_bytes = leint_bytes(&header[4]);

                if (MYSQL_HEADER_LEN + n_bytes <= buflen)
                {
                    // Now we can figure out how many fields there are, but first we
                    // need to copy some more data.
                    gwbuf_copy_data(csdata->res.data,
                                    MYSQL_HEADER_LEN + 1, n_bytes - 1, &header[MYSQL_HEADER_LEN + 1]);

                    csdata->res.n_totalfields = leint_value(&header[4]);
                    csdata->res.offset = MYSQL_HEADER_LEN + n_bytes;

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
 * Called when resultset rows are handled.
 *
 * @param csdata The cache session data.
 */
static int handle_expecting_rows(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_EXPECTING_ROWS);
    ss_dassert(csdata->res.data);

    int rv = 1;

    bool insufficient = false;

    size_t buflen = gwbuf_length(csdata->res.data);

    while (!insufficient && (buflen - csdata->res.offset >= MYSQL_HEADER_LEN))
    {
        uint8_t header[MYSQL_HEADER_LEN + 1];
        gwbuf_copy_data(csdata->res.data, csdata->res.offset, MYSQL_HEADER_LEN + 1, header);

        size_t packetlen = MYSQL_HEADER_LEN + MYSQL_GET_PACKET_LEN(header);

        if (csdata->res.offset + packetlen <= buflen)
        {
            // We have at least one complete packet.
            int command = (int)MYSQL_GET_COMMAND(header);

            switch (command)
            {
            case 0xfe: // EOF, the one after the rows.
                csdata->res.offset += packetlen;
                ss_dassert(csdata->res.offset == buflen);

                /* Send data only if number of rows is below the limit */
                if (csdata->state != MAXROWS_IGNORING_RESPONSE)
                {
                    rv = send_upstream(csdata);
                }

                csdata->state = MAXROWS_EXPECTING_NOTHING;
                break;

            case 0xfb: // NULL
            default: // length-encoded-string
                csdata->res.offset += packetlen;
                ++csdata->res.n_rows;

                if (csdata->res.n_rows > csdata->instance->config.max_resultset_rows)
                {
                    C_DEBUG("Max rows %lu reached, not caching result.", csdata->res.n_rows);
                    /* Just return 0 result set */
                    rv = send_ok_upstream(csdata);
                    csdata->res.offset = buflen; // To abort the loop.
                    csdata->state = MAXROWS_IGNORING_RESPONSE;
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

    return rv;
}

/**
 * Called when a response to a "USE db" is received from the server.
 *
 * @param csdata The cache session data.
 */
static int handle_expecting_use_response(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_EXPECTING_USE_RESPONSE);
    ss_dassert(csdata->res.data);

    int rv = 1;

    size_t buflen = gwbuf_length(csdata->res.data);

    if (buflen >= MYSQL_HEADER_LEN + 1) // We need the command byte.
    {
        uint8_t command;

        gwbuf_copy_data(csdata->res.data, MYSQL_HEADER_LEN, 1, &command);

        switch (command)
        {
        case 0x00: // OK
            // In case csdata->use_db could not be allocated in routeQuery(), we will
            // in fact reset the default db here. That's ok as it will prevent broken
            // entries in the cache.
            MXS_FREE(csdata->default_db);
            csdata->default_db = csdata->use_db;
            csdata->use_db = NULL;
            break;

        case 0xff: // ERR
            MXS_FREE(csdata->use_db);
            csdata->use_db = NULL;
            break;

        default:
            MXS_ERROR("\"USE %s\" received unexpected server response %d.",
                      csdata->use_db ? csdata->use_db : "<db>", command);
            MXS_FREE(csdata->default_db);
            MXS_FREE(csdata->use_db);
            csdata->default_db = NULL;
            csdata->use_db = NULL;
        }

        rv = send_upstream(csdata);
        csdata->state = MAXROWS_IGNORING_RESPONSE;
    }

    return rv;
}

/**
 * Called when all data from the server is ignored.
 *
 * @param csdata The cache session data.
 */
static int handle_ignoring_response(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == MAXROWS_IGNORING_RESPONSE);
    ss_dassert(csdata->res.data);

    return send_upstream(csdata);
}

/**
 * Processes the cache params
 *
 * @param options Options as passed to the filter.
 * @param params  Parameters as passed to the filter.
 * @param config  Pointer to config instance where params will be stored.
 *
 * @return True if all parameters could be processed, false otherwise.
 */
static bool process_params(char **options, FILTER_PARAMETER **params, MAXROWS_CONFIG* config)
{
    bool error = false;

    for (int i = 0; params[i]; ++i)
    {
        const FILTER_PARAMETER *param = params[i];

        /* We could add a new parameter, max_resultset_columns:
         * This way if result has more than max_resultset_columns
         * we return 0 result
         */ 

        if (strcmp(param->name, "max_resultset_rows") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->max_resultset_rows = v;
            }
            else
            {
                config->max_resultset_rows = MAXROWS_DEFAULT_MAX_RESULTSET_ROWS;
            }
        }
        else if (strcmp(param->name, "max_resultset_size") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->max_resultset_size = v * 1024;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be an integer larger than 0.", param->name);
                error = true;
            }
        }
        else if (strcmp(param->name, "debug") == 0)
        {
            int v = atoi(param->value);

            if ((v >= MAXROWS_DEBUG_MIN) && (v <= MAXROWS_DEBUG_MAX))
            {
                config->debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          param->name, MAXROWS_DEBUG_MIN, MAXROWS_DEBUG_MAX);
                error = true;
            }
        }
        else if (!filter_standard_parameter(params[i]->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", param->name);
            error = true;
        }
    }

    return !error;
}

/**
 * Send data upstream.
 *
 * @param csdata Session data
 *
 * @return Whatever the upstream returns.
 */
static int send_upstream(MAXROWS_SESSION_DATA *csdata)
{
    ss_dassert(csdata->res.data != NULL);

    int rv = csdata->up.clientReply(csdata->up.instance, csdata->up.session, csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Send OK packet data upstream.
 *
 * @param csdata Session data
 *
 * @return Whatever the upstream returns.
 */
static int send_ok_upstream(MAXROWS_SESSION_DATA *csdata)
{
    /* Note: sequence id is always 01 (4th byte) */
    uint8_t ok[MAXROWS_OK_PACKET_LEN] = {07,00,00,01,00,00,00,02,00,00,00};
    GWBUF *packet = gwbuf_alloc(MAXROWS_OK_PACKET_LEN);
    uint8_t *ptr = GWBUF_DATA(packet);
    memcpy(ptr, &ok, MAXROWS_OK_PACKET_LEN);

    ss_dassert(csdata->res.data != NULL);

    int rv = csdata->up.clientReply(csdata->up.instance, csdata->up.session, packet);
    gwbuf_free(csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

