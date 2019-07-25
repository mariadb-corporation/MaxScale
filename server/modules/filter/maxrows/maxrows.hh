/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "maxrows"

#include <maxscale/ccdefs.hh>

#include <maxscale/filter.hh>

/*
 * The EOF packet 2 bytes flags start after:
 * network header (4 bytes) + eof indicator (1) + 2 bytes warnings count)
 */
#define MAXROWS_MYSQL_EOF_PACKET_FLAGS_OFFSET (MYSQL_HEADER_LEN + 1 + 2)

#define MAXROWS_DEBUG_NONE       0
#define MAXROWS_DEBUG_DISCARDING 1
#define MAXROWS_DEBUG_DECISIONS  2

#define MAXROWS_DEBUG_USAGE (MAXROWS_DEBUG_DECISIONS | MAXROWS_DEBUG_DISCARDING)
#define MAXROWS_DEBUG_MIN   MAXROWS_DEBUG_NONE
#define MAXROWS_DEBUG_MAX   MAXROWS_DEBUG_USAGE

// Count
#define MAXROWS_DEFAULT_MAX_RESULTSET_ROWS MXS_MODULE_PARAM_COUNT_MAX
// Bytes
#define MAXROWS_DEFAULT_MAX_RESULTSET_SIZE "65536"
// Integer value
#define MAXROWS_DEFAULT_DEBUG "0"
// Max size of copied input SQL
#define MAXROWS_INPUT_SQL_MAX_LEN 1024


enum Mode
{
    EMPTY, ERR, OK
};

static const MXS_ENUM_VALUE mode_values[] =
{
    {"empty", Mode::EMPTY},
    {"error", Mode::ERR  },
    {"ok",    Mode::OK   },
    {NULL}
};

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

class MaxRows;

class MaxRowsSession : public maxscale::FilterSession
{
public:
    MaxRowsSession(const MaxRowsSession&) = delete;
    MaxRowsSession& operator=(const MaxRowsSession&) = delete;

    // Create a new filter session
    static MaxRowsSession* create(MXS_SESSION* pSession, MaxRows* pFilter)
    {
        return new(std::nothrow) MaxRowsSession(pSession, pFilter);
    }

    // Called when a client session has been closed
    void close()
    {
        gwbuf_free(input_sql);
    }

    // Handle a query from the client
    int routeQuery(GWBUF* pPacket);

    // Handle a reply from server
    int clientReply(GWBUF* pPacket, DCB* dcb);

private:
    // Used in the create function
    MaxRowsSession(MXS_SESSION* pSession, MaxRows* pFilter)
        : FilterSession(pSession)
        , m_instance(pFilter)
    {
    }

    int handle_expecting_fields(MaxRowsSession* csdata);
    int handle_expecting_nothing(MaxRowsSession* csdata);
    int handle_expecting_response(MaxRowsSession* csdata);
    int handle_rows(MaxRowsSession* csdata, GWBUF* buffer, size_t extra_offset);
    int handle_ignoring_response(MaxRowsSession* csdata);
    int send_upstream(MaxRowsSession* csdata);
    int send_ok_upstream(MaxRowsSession* csdata);
    int send_eof_upstream(MaxRowsSession* csdata);
    int send_error_upstream(MaxRowsSession* csdata);
    int send_maxrows_reply_limit(MaxRowsSession* csdata);

    MaxRows*                instance;   /**< The maxrows instance the session is associated with. */
    MAXROWS_RESPONSE_STATE  res {};     /**< The response state. */
    MXS_SESSION*            session;    /**< The session this data is associated with. */
    maxrows_session_state_t state {MAXROWS_EXPECTING_NOTHING};
    bool                    large_packet {false};       /**< Large packet (> 16MB)) indicator */
    bool                    discard_resultset {false};  /**< Discard resultset indicator */
    GWBUF*                  input_sql {nullptr};        /**< Input query */

    MaxRows*    m_instance;
    mxs::Buffer m_buffer;   // Contains the partial resultset
    bool        m_collect {true};
};

class MaxRows : public maxscale::Filter<MaxRows, MaxRowsSession>
{
public:
    MaxRows(const MaxRows&) = delete;
    MaxRows& operator=(const MaxRows&) = delete;

    struct Config
    {
        Config(const MXS_CONFIG_PARAMETER* params)
            : max_rows(params->get_integer("max_resultset_rows"))
            , max_size(params->get_size("max_resultset_size"))
            , debug(params->get_integer("debug"))
            , mode(static_cast<Mode>(params->get_enum("max_resultset_return", mode_values)))
        {
        }

        uint32_t max_rows;
        uint32_t max_size;
        uint32_t debug;
        Mode     mode;
    };

    static constexpr uint64_t CAPABILITIES = RCAP_TYPE_REQUEST_TRACKING;

    // Creates a new filter instance
    static MaxRows* create(const char* name, MXS_CONFIG_PARAMETER* params)
    {
        return new(std::nothrow) MaxRows(name, params);
    }

    // Creates a new session for this filter
    MaxRowsSession* newSession(MXS_SESSION* session)
    {
        return MaxRowsSession::create(session, this);
    }

    // Print diagnostics to a DCB
    void diagnostics(DCB* dcb) const
    {
    }

    // Returns JSON form diagnostic data
    json_t* diagnostics_json() const
    {
        return nullptr;
    }

    // Get filter capabilities
    uint64_t getCapabilities()
    {
        return CAPABILITIES;
    }

    // Return reference to filter config
    const Config& config() const
    {
        return m_config;
    }

private:
    MaxRows(const char* name, const MXS_CONFIG_PARAMETER* params)
        : m_name(name)
        , m_config(params)
    {
    }

private:
    std::string m_name;
    Config      m_config;
};
