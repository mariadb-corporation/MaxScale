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

#define MXS_MODULE_NAME "cache"
#include <maxscale/alloc.h>
#include <filter.h>
#include <gwdirs.h>
#include <log_manager.h>
#include <modinfo.h>
#include <modutil.h>
#include <mysql_utils.h>
#include <query_classifier.h>
#include "cache.h"
#include "rules.h"
#include "storage.h"

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

#define C_DEBUG(format, ...) MXS_LOG_MESSAGE(LOG_NOTICE,  format, ##__VA_ARGS__)

//
// Global symbols of the Module
//

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_IN_DEVELOPMENT,
    FILTER_VERSION,
    "A caching filter that is capable of caching and returning cached data."
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
        };

    return &object;
};

//
// Implementation
//

typedef struct cache_config
{
    uint32_t    max_resultset_rows;
    uint32_t    max_resultset_size;
    const char* rules;
    const char *storage;
    const char *storage_options;
    uint32_t    ttl;
    uint32_t    debug;
} CACHE_CONFIG;

static const CACHE_CONFIG DEFAULT_CONFIG =
{
    CACHE_DEFAULT_MAX_RESULTSET_ROWS,
    CACHE_DEFAULT_MAX_RESULTSET_SIZE,
    NULL,
    NULL,
    NULL,
    CACHE_DEFAULT_TTL,
    CACHE_DEFAULT_DEBUG
};

typedef struct cache_instance
{
    const char            *name;
    CACHE_CONFIG           config;
    CACHE_RULES           *rules;
    CACHE_STORAGE_MODULE  *module;
    CACHE_STORAGE         *storage;
} CACHE_INSTANCE;

typedef enum cache_session_state
{
    CACHE_EXPECTING_RESPONSE,     // A select has been sent, and we are waiting for the response.
    CACHE_EXPECTING_FIELDS,       // A select has been sent, and we want more fields.
    CACHE_EXPECTING_ROWS,         // A select has been sent, and we want more rows.
    CACHE_EXPECTING_NOTHING,      // We are not expecting anything from the server.
    CACHE_EXPECTING_USE_RESPONSE, // A "USE DB" was issued.
    CACHE_IGNORING_RESPONSE,      // We are not interested in the data received from the server.
} cache_session_state_t;

typedef struct cache_request_state
{
    GWBUF* data; /**< Request data, possibly incomplete. */
} CACHE_REQUEST_STATE;

typedef struct cache_response_state
{
    GWBUF* data;          /**< Response data, possibly incomplete. */
    size_t n_totalfields; /**< The number of fields a resultset contains. */
    size_t n_fields;      /**< How many fields we have received, <= n_totalfields. */
    size_t n_rows;        /**< How many rows we have received. */
    size_t offset;        /**< Where we are in the response buffer. */
} CACHE_RESPONSE_STATE;

static void cache_response_state_reset(CACHE_RESPONSE_STATE *state);

typedef struct cache_session_data
{
    CACHE_INSTANCE      *instance;   /**< The cache instance the session is associated with. */
    CACHE_STORAGE_API   *api;        /**< The storage API to be used. */
    CACHE_STORAGE       *storage;    /**< The storage to be used with this session data. */
    DOWNSTREAM           down;       /**< The previous filter or equivalent. */
    UPSTREAM             up;         /**< The next filter or equivalent. */
    CACHE_REQUEST_STATE  req;        /**< The request state. */
    CACHE_RESPONSE_STATE res;        /**< The response state. */
    SESSION             *session;    /**< The session this data is associated with. */
    char                 key[CACHE_KEY_MAXLEN]; /**< Key storage. */
    char                *default_db; /**< The default database. */
    char                *use_db;     /**< Pending default database. Needs server response. */
    cache_session_state_t state;
} CACHE_SESSION_DATA;

static CACHE_SESSION_DATA *cache_session_data_create(CACHE_INSTANCE *instance, SESSION *session);
static void cache_session_data_free(CACHE_SESSION_DATA *data);

static int handle_expecting_fields(CACHE_SESSION_DATA *csdata);
static int handle_expecting_nothing(CACHE_SESSION_DATA *csdata);
static int handle_expecting_response(CACHE_SESSION_DATA *csdata);
static int handle_expecting_rows(CACHE_SESSION_DATA *csdata);
static int handle_expecting_use_response(CACHE_SESSION_DATA *csdata);
static int handle_ignoring_response(CACHE_SESSION_DATA *csdata);
static bool process_params(char **options, FILTER_PARAMETER **params, CACHE_CONFIG* config);
static bool route_using_cache(CACHE_SESSION_DATA *sdata, const GWBUF *key, GWBUF **value);

static int send_upstream(CACHE_SESSION_DATA *csdata);

static void store_result(CACHE_SESSION_DATA *csdata);

//
// API BEGIN
//

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
    CACHE_INSTANCE *cinstance = NULL;
    CACHE_CONFIG config = DEFAULT_CONFIG;

    if (process_params(options, params, &config))
    {
        CACHE_RULES *rules = NULL;

        if (config.rules)
        {
            rules = cache_rules_load(config.rules, config.debug);
        }
        else
        {
            rules = cache_rules_create(config.debug);
        }

        if (rules)
        {
            if ((cinstance = MXS_CALLOC(1, sizeof(CACHE_INSTANCE))) != NULL)
            {
                CACHE_STORAGE_MODULE *module = cache_storage_open(config.storage);

                if (module)
                {
                    CACHE_STORAGE *storage = module->api->createInstance(name, config.ttl, 0, NULL);

                    if (storage)
                    {
                        cinstance->name = name;
                        cinstance->config = config;
                        cinstance->rules = rules;
                        cinstance->module = module;
                        cinstance->storage = storage;

                        MXS_NOTICE("Cache storage %s opened and initialized.", config.storage);
                    }
                    else
                    {
                        MXS_ERROR("Could not create storage instance for %s.", name);
                        cache_rules_free(rules);
                        cache_storage_close(module);
                        MXS_FREE(cinstance);
                        cinstance = NULL;
                    }
                }
                else
                {
                    MXS_ERROR("Could not load cache storage module %s.", name);
                    cache_rules_free(rules);
                    MXS_FREE(cinstance);
                    cinstance = NULL;
                }
            }
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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = cache_session_data_create(cinstance, session);

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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;
}

/**
 * Free the session data.
 *
 * @param instance  The cache instance data
 * @param sdata     The session data of the session being closed
 */
static void freeSession(FILTER *instance, void *sdata)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    cache_session_data_free(csdata);
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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    csdata->up = *up;
}

/**
 * A request on its way to a backend is delivered to this function.
 *
 * @param instance  The filter instance data
 * @param sdata     The filter session data
 * @param packets   The query data
 */
static int routeQuery(FILTER *instance, void *sdata, GWBUF *data)
{
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    if (csdata->req.data)
    {
        gwbuf_append(csdata->req.data, data);
    }
    else
    {
        csdata->req.data = data;
    }

    GWBUF *packet = modutil_get_next_MySQL_packet(&csdata->req.data);

    int rv;

    if (packet)
    {
        bool use_default = true;

        cache_response_state_reset(&csdata->res);
        csdata->state = CACHE_IGNORING_RESPONSE;

        if (gwbuf_length(packet) > MYSQL_HEADER_LEN + 1) // We need at least a packet with a type.
        {
            uint8_t header[MYSQL_HEADER_LEN + 1];

            gwbuf_copy_data(packet, 0, sizeof(header), header);

            switch ((int)MYSQL_GET_COMMAND(header))
            {
            case MYSQL_COM_INIT_DB:
                {
                    ss_dassert(!csdata->use_db);
                    size_t len = MYSQL_GET_PACKET_LEN(header) - 1; // Remove the command byte.
                    csdata->use_db = MXS_MALLOC(len + 1);

                    if (csdata->use_db)
                    {
                        uint8_t *use_db = (uint8_t*)csdata->use_db;
                        gwbuf_copy_data(packet, MYSQL_HEADER_LEN + 1, len, use_db);
                        csdata->use_db[len] = 0;
                        csdata->state = CACHE_EXPECTING_USE_RESPONSE;
                    }
                    else
                    {
                        // Memory allocation failed. We need to remove the default database to
                        // prevent incorrect cache entries, since we won't know what the
                        // default db is. But we only need to do that if "USE <db>" really
                        // succeeds. The right thing will happen by itself in
                        // handle_expecting_use_response(); if OK is returned, default_db will
                        // become NULL, if ERR, default_db will not be changed.
                    }
                }
                break;

            case MYSQL_COM_QUERY:
                {
                    GWBUF *tmp = gwbuf_make_contiguous(packet);

                    if (tmp)
                    {
                        packet = tmp;

                        // We do not care whether the query was fully parsed or not.
                        // If a query cannot be fully parsed, the worst thing that can
                        // happen is that caching is not used, even though it would be
                        // possible.

                        if (qc_get_operation(packet) == QUERY_OP_SELECT)
                        {
                            if (cache_rules_should_store(cinstance->rules, csdata->default_db, packet))
                            {
                                if (cache_rules_should_use(cinstance->rules, csdata->session))
                                {
                                    GWBUF *result;
                                    use_default = !route_using_cache(csdata, packet, &result);

                                    if (use_default)
                                    {
                                        csdata->state = CACHE_EXPECTING_RESPONSE;
                                    }
                                    else
                                    {
                                        csdata->state = CACHE_EXPECTING_NOTHING;
                                        C_DEBUG("Using data from cache.");
                                        gwbuf_free(packet);
                                        DCB *dcb = csdata->session->client_dcb;

                                        // TODO: This is not ok. Any filters before this filter, will not
                                        // TODO: see this data.
                                        rv = dcb->func.write(dcb, result);
                                    }
                                }
                            }
                            else
                            {
                                csdata->state = CACHE_IGNORING_RESPONSE;
                            }
                        }
                    }
                }
                break;

            default:
                break;
            }
        }

        if (use_default)
        {
            C_DEBUG("Using default processing.");
            rv = csdata->down.routeQuery(csdata->down.instance, csdata->down.session, packet);
        }
    }
    else
    {
        // We need more data before we can do something.
        rv = 1;
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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    int rv;

    if (csdata->res.data)
    {
        gwbuf_append(csdata->res.data, data);
    }
    else
    {
        csdata->res.data = data;
    }

    if (csdata->state != CACHE_IGNORING_RESPONSE)
    {
        if (gwbuf_length(csdata->res.data) > csdata->instance->config.max_resultset_size)
        {
            C_DEBUG("Current size %uB of resultset, at least as much "
                    "as maximum allowed size %uKiB. Not caching.",
                    gwbuf_length(csdata->res.data),
                    csdata->instance->config.max_resultset_size / 1024);

            csdata->state = CACHE_IGNORING_RESPONSE;
        }
    }

    switch (csdata->state)
    {
    case CACHE_EXPECTING_FIELDS:
        rv = handle_expecting_fields(csdata);
        break;

    case CACHE_EXPECTING_NOTHING:
        rv = handle_expecting_nothing(csdata);
        break;

    case CACHE_EXPECTING_RESPONSE:
        rv = handle_expecting_response(csdata);
        break;

    case CACHE_EXPECTING_ROWS:
        rv = handle_expecting_rows(csdata);
        break;

    case CACHE_EXPECTING_USE_RESPONSE:
        rv = handle_expecting_use_response(csdata);
        break;

    case CACHE_IGNORING_RESPONSE:
        rv = handle_ignoring_response(csdata);
        break;

    default:
        MXS_ERROR("Internal cache logic broken, unexpected state: %d", csdata->state);
        ss_dassert(!true);
        rv = send_upstream(csdata);
        cache_response_state_reset(&csdata->res);
        csdata->state = CACHE_IGNORING_RESPONSE;
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
    CACHE_INSTANCE *cinstance = (CACHE_INSTANCE*)instance;
    CACHE_SESSION_DATA *csdata = (CACHE_SESSION_DATA*)sdata;

    dcb_printf(dcb, "Hello World from Cache!\n");
}

//
// API END
//

/**
 * Reset cache response state
 *
 * @param state Pointer to object.
 */
static void cache_response_state_reset(CACHE_RESPONSE_STATE *state)
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
static CACHE_SESSION_DATA *cache_session_data_create(CACHE_INSTANCE *instance,
                                                     SESSION* session)
{
    CACHE_SESSION_DATA *data = (CACHE_SESSION_DATA*)MXS_CALLOC(1, sizeof(CACHE_SESSION_DATA));

    if (data)
    {
        data->instance = instance;
        data->api = instance->module->api;
        data->storage = instance->storage;
        data->session = session;
        data->state = CACHE_EXPECTING_NOTHING;
    }

    return data;
}

/**
 * Free cache session data.
 *
 * @param A cache session data previously allocated using session_data_create().
 */
static void cache_session_data_free(CACHE_SESSION_DATA* data)
{
    if (data)
    {
        ss_dassert(!data->use_db);
        MXS_FREE(data->default_db);
        MXS_FREE(data);
    }
}

/**
 * Called when resultset field information is handled.
 *
 * @param csdata The cache session data.
 */
static int handle_expecting_fields(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_EXPECTING_FIELDS);
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
                csdata->state = CACHE_EXPECTING_ROWS;
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
static int handle_expecting_nothing(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_EXPECTING_NOTHING);
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
static int handle_expecting_response(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_EXPECTING_RESPONSE);
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
            store_result(csdata);

            rv = send_upstream(csdata);
            csdata->state = CACHE_IGNORING_RESPONSE;
            break;

        case 0xfb: // GET_MORE_CLIENT_DATA/SEND_MORE_CLIENT_DATA
            C_DEBUG("GET_MORE_CLIENT_DATA");
            rv = send_upstream(csdata);
            csdata->state = CACHE_IGNORING_RESPONSE;
            break;

        default:
            C_DEBUG("RESULTSET");

            if (csdata->res.n_totalfields != 0)
            {
                // We've seen the header and have figured out how many fields there are.
                csdata->state = CACHE_EXPECTING_FIELDS;
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

                    csdata->state = CACHE_EXPECTING_FIELDS;
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
static int handle_expecting_rows(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_EXPECTING_ROWS);
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

                store_result(csdata);

                rv = send_upstream(csdata);
                csdata->state = CACHE_EXPECTING_NOTHING;
                break;

            case 0xfb: // NULL
            default: // length-encoded-string
                csdata->res.offset += packetlen;
                ++csdata->res.n_rows;

                if (csdata->res.n_rows > csdata->instance->config.max_resultset_rows)
                {
                    C_DEBUG("Max rows %lu reached, not caching result.", csdata->res.n_rows);
                    rv = send_upstream(csdata);
                    csdata->res.offset = buflen; // To abort the loop.
                    csdata->state = CACHE_IGNORING_RESPONSE;
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
static int handle_expecting_use_response(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_EXPECTING_USE_RESPONSE);
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
        csdata->state = CACHE_IGNORING_RESPONSE;
    }

    return rv;
}

/**
 * Called when all data from the server is ignored.
 *
 * @param csdata The cache session data.
 */
static int handle_ignoring_response(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->state == CACHE_IGNORING_RESPONSE);
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
static bool process_params(char **options, FILTER_PARAMETER **params, CACHE_CONFIG* config)
{
    bool error = false;

    for (int i = 0; params[i]; ++i)
    {
        const FILTER_PARAMETER *param = params[i];

        if (strcmp(param->name, "max_resultset_rows") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->max_resultset_rows = v;
            }
            else
            {
                config->max_resultset_rows = CACHE_DEFAULT_MAX_RESULTSET_ROWS;
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
        else if (strcmp(param->name, "rules") == 0)
        {
            if (*param->value == '/')
            {
                config->rules = MXS_STRDUP(param->value);
            }
            else
            {
                const char *datadir = get_datadir();
                size_t len = strlen(datadir) + 1 + strlen(param->value) + 1;

                char *rules = MXS_MALLOC(len);

                if (rules)
                {
                    sprintf(rules, "%s/%s", datadir, param->value);
                    config->rules = rules;
                }
            }

            if (!config->rules)
            {
                error = true;
            }
        }
        else if (strcmp(param->name, "storage_options") == 0)
        {
            config->storage_options = param->value;
        }
        else if (strcmp(param->name, "storage") == 0)
        {
            config->storage = param->value;
        }
        else if (strcmp(param->name, "ttl") == 0)
        {
            int v = atoi(param->value);

            if (v > 0)
            {
                config->ttl = v;
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

            if ((v >= CACHE_DEBUG_MIN) && (v <= CACHE_DEBUG_MAX))
            {
                config->debug = v;
            }
            else
            {
                MXS_ERROR("The value of the configuration entry '%s' must "
                          "be between %d and %d, inclusive.",
                          param->name, CACHE_DEBUG_MIN, CACHE_DEBUG_MAX);
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
 * Route a query via the cache.
 *
 * @param csdata Session data
 * @param key A SELECT packet.
 * @param value The result.
 * @return True if the query was satisfied from the query.
 */
static bool route_using_cache(CACHE_SESSION_DATA *csdata,
                              const GWBUF *query,
                              GWBUF **value)
{
    cache_result_t result = csdata->api->getKey(csdata->storage, query, csdata->key);

    if (result == CACHE_RESULT_OK)
    {
        result = csdata->api->getValue(csdata->storage, csdata->key, value);
    }
    else
    {
        MXS_ERROR("Could not create cache key.");
    }

    return result == CACHE_RESULT_OK;
}

/**
 * Send data upstream.
 *
 * @param csdata Session data
 *
 * @return Whatever the upstream returns.
 */
static int send_upstream(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->res.data != NULL);

    int rv = csdata->up.clientReply(csdata->up.instance, csdata->up.session, csdata->res.data);
    csdata->res.data = NULL;

    return rv;
}

/**
 * Store the data.
 *
 * @param csdata Session data
 */
static void store_result(CACHE_SESSION_DATA *csdata)
{
    ss_dassert(csdata->res.data);

    GWBUF *data = gwbuf_make_contiguous(csdata->res.data);

    if (data)
    {
        csdata->res.data = data;

        cache_result_t result = csdata->api->putValue(csdata->storage,
                                                      csdata->key,
                                                      csdata->res.data);

        if (result != CACHE_RESULT_OK)
        {
            MXS_ERROR("Could not store cache item.");
        }
    }
}
