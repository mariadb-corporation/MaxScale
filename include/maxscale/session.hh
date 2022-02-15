/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <time.h>

#include <maxbase/atomic.h>
#include <maxbase/jansson.h>
#include <maxscale/dcb.hh>
#include <maxscale/buffer.hh>
#include <maxscale/utils.hh>
#include "query_classifier.hh"

class DCB;
class SERVICE;
struct mxs_filter;
struct mxs_filter_session;
class SERVER;

namespace maxscale
{
class ListenerSessionData;
}

static constexpr uint32_t SESSION_TRX_INACTIVE  = 0;
static constexpr uint32_t SESSION_TRX_ACTIVE    = 1 << 0; /* 0b0001 */
static constexpr uint32_t SESSION_TRX_READ_ONLY = 1 << 1; /* 0b0010 */
static constexpr uint32_t SESSION_TRX_ENDING    = 1 << 2; /* 0b0100*/
static constexpr uint32_t SESSION_TRX_STARTING  = 1 << 3; /* 0b1000*/

typedef enum
{
    SESSION_DUMP_STATEMENTS_NEVER,
    SESSION_DUMP_STATEMENTS_ON_CLOSE,
    SESSION_DUMP_STATEMENTS_ON_ERROR,
} session_dump_statements_t;

/**
 * The session statistics structure
 */
typedef struct
{
    time_t connect; /**< Time when the session was started */
} MXS_SESSION_STATS;

/**
 * The downstream element in the filter chain. This may refer to
 * another filter or to a router.
 */
struct mxs_filter;
struct mxs_filter_session;

namespace maxscale
{

class RoutingWorker;

// These are more convenient types
typedef int32_t (*DOWNSTREAMFUNC)(
    struct mxs_filter* instance, struct mxs_filter_session* session, GWBUF* response);
typedef int32_t (*UPSTREAMFUNC)(struct mxs_filter* instance,
    struct mxs_filter_session* session,
    GWBUF* response,
    const mxs::ReplyRoute& down,
    const mxs::Reply& reply);

struct Downstream
{
    mxs_filter* instance {nullptr};
    mxs_filter_session* session {nullptr};
    DOWNSTREAMFUNC routeQuery {nullptr};
};

/**
 * The upstream element in the filter chain. This may refer to
 * another filter or to the protocol implementation.
 */
struct Upstream
{
    mxs_filter* instance {nullptr};
    mxs_filter_session* session {nullptr};
    UPSTREAMFUNC clientReply {nullptr};
};
}  // namespace maxscale

/* Specific reasons why a session was closed */
typedef enum
{
    SESSION_CLOSE_NONE = 0,              // No special reason
    SESSION_CLOSE_TIMEOUT,               // Connection timed out
    SESSION_CLOSE_HANDLEERROR_FAILED,    // Router returned an error from handleError
    SESSION_CLOSE_ROUTING_FAILED,        // Router closed DCB
    SESSION_CLOSE_KILLED,                // Killed by another connection
    SESSION_CLOSE_TOO_MANY_CONNECTIONS,  // Too many connections
} session_close_t;

/**
 * Handler function for MaxScale specific session variables.
 *
 * Note that the provided value string is exactly as it appears in
 * the received SET-statement. Only leading and trailing whitespace
 * has been removed. The handler must itself parse the value string.
 *
 * @param context      Context provided when handler was registered.
 * @param name         The variable that is being set. Note that it
 *                     will always be in all lower-case irrespective
 *                     of the case used when registering.
 * @param value_begin  The beginning of the value as specified in the
 *                     "set @maxscale.x.y = VALUE" statement.
 * @param value_end    One past the end of the VALUE.
 *
 * @return  NULL if successful, otherwise a dynamically allocated string
 *          containing an end-user friendly error message.
 */
typedef char* (*session_variable_handler_t)(
    void* context, const char* name, const char* value_begin, const char* value_end);

/**
 * The session status block
 *
 * A session status block is created for each user (client) connection
 * to the database, it links the descriptors, routing implementation
 * and originating service together for the client session.
 *
 * Note that the first few fields (up to and including "entry_is_ready") must
 * precisely match the LIST_ENTRY structure defined in the list manager.
 */
class MXS_SESSION
{
public:
    enum class State
    {
        CREATED,  /*< Session created but not started */
        STARTED,  /*< Session is fully functional */
        STOPPING, /*< Session and router are being closed */
        FAILED,   /*< Creation failed */
        FREE,     /*< The session is freed, only for completeness sake */
    };

    /**
     * If a protocol wants to define custom session-level data, the data should inherit from this class.
     */
    class ProtocolData
    {
    public:
        virtual ~ProtocolData() = default;
    };

    virtual ~MXS_SESSION();

    maxscale::RoutingWorker* worker() const { return m_worker; }

    State state() const { return m_state; }

    uint64_t id() const { return m_id; }

    const std::string& user() const { return m_user; }

    void set_user(const std::string& user) { m_user = user; }

    /**
     * Abruptly stop the session
     *
     * This method should be used to stop the session when an abnormal failure has occurred.
     *
     * @param error An optionl error message that is sent to the client before the session is terminated
     */
    void kill(GWBUF* error = nullptr);

    // Convenience function for client identification
    std::string user_and_host() const { return "'" + m_user + "'@'" + m_host + "'"; }

    const std::string& client_remote() const { return m_host; }

    // The current active default database (i.e. USE <database>)
    const std::string& database() const { return m_database; }

    void start_database_change(const std::string& database) { m_pending_database = database; }

    void set_database(const std::string& database) { m_database = database; }

    virtual mxs::ClientConnection* client_connection()                     = 0;
    virtual const mxs::ClientConnection* client_connection() const         = 0;
    virtual void set_client_connection(mxs::ClientConnection* client_conn) = 0;
    virtual mxs::ListenerSessionData* listener_data()                      = 0;

    /**
     * Get the transaction state of the session.
     *
     * Note that this tells only the state of @e explicitly started transactions.
     * That is, if @e autocommit is OFF, which means that there is always an
     * active transaction that is ended with an explicit COMMIT or ROLLBACK,
     * at which point a new transaction is started, this function will still
     * return SESSION_TRX_INACTIVE, unless a transaction has explicitly been
     * started with START TRANSACTION.
     *
     * Likewise, if @e autocommit is ON, which means that every statement is
     * executed in a transaction of its own, this will return false, unless a
     * transaction has explicitly been started with START TRANSACTION.
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return The transaction state.
     */
    uint32_t get_trx_state() const { return m_trx_state; }

    /**
     * Set the transaction state of the session.
     *
     * NOTE: Only the protocol object may call this.
     *
     * @param new_state The new transaction state.
     *
     * @return The previous transaction state.
     */
    void set_trx_state(uint32_t new_state) { m_trx_state = new_state; }

    /**
     * Tells whether an explicit READ ONLY transaction is active.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if an explicit READ ONLY transaction is active,
     *         false otherwise.
     */
    bool is_trx_read_only() const { return m_trx_state & SESSION_TRX_READ_ONLY; }

    /**
     * Tells whether an explicit READ WRITE transaction is active.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if an explicit READ WRITE  transaction is active,
     *         false otherwise.
     */
    bool is_trx_read_write() const { return !is_trx_read_only(); }

    /**
     * Tells whether a transaction is ending.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a transaction that was active is ending either via COMMIT or ROLLBACK.
     */
    bool is_trx_ending() const { return m_trx_state & SESSION_TRX_ENDING; }

    /**
     * Tells whether a transaction is starting.
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a new transaction is currently starting
     */
    bool is_trx_starting() const { return m_trx_state & SESSION_TRX_STARTING; }

    /**
     * Tells whether a transaction is active.
     *
     * @see get_trx_state
     *
     * @note The return value is valid only if either a router or a filter
     *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
     *
     * @return True if a transaction is active, false otherwise.
     */
    bool is_trx_active() const { return m_trx_state & SESSION_TRX_ACTIVE; }

    /**
     * Tells whether autocommit is ON or not.
     *
     * Note that the returned value effectively only tells the last value
     * of the statement "set autocommit=...".
     *
     * That is, if the statement "set autocommit=1" has been executed, then
     * even if a transaction has been started, which implicitly will cause
     * autocommit to be set to 0 for the duration of the transaction, this
     * function will still return true.
     *
     * Note also that by default autocommit is ON.
     *
     * @see get_trx_state
     *
     * @return True if autocommit has been set ON, false otherwise.
     */
    bool is_autocommit() const { return m_autocommit; }

    /**
     * Sets the autocommit state of the session.
     *
     * NOTE: Only the protocol object may call this.
     *
     * @param enable True if autocommit is enabled, false otherwise.
     */
    void set_autocommit(bool autocommit) { m_autocommit = autocommit; }

    /**
     * Get session capabilities
     *
     * @return The capabilities required the services and filters used by this session
     */
    uint64_t capabilities() const { return m_capabilities; }

protected:
    State m_state; /**< Current descriptor state */
    uint64_t m_id; /**< Unique session identifier */
    maxscale::RoutingWorker* m_worker;
    std::string m_user; /**< The session user. */
    std::string m_host;
    std::string m_database;
    std::string m_pending_database;

    MXS_SESSION(const std::string& host, SERVICE* service);

public:
    ClientDCB* client_dcb; /*< The client connection */

    MXS_SESSION_STATS stats;    /*< Session statistics */
    SERVICE* service;           /*< The service this session is using */
    int refcount;               /*< Reference count on the session */
    bool qualifies_for_pooling; /*< Whether this session qualifies for the connection pool */

    struct
    {
        mxs::Upstream up; /*< Upward component to receive buffer. */
        GWBUF* buffer;    /*< Buffer to deliver to up. */
        SERVICE* service; /*< Service where the response originated */
    } response;           /*< Shortcircuited response */

    session_close_t close_reason; /*< Reason why the session was closed */

    bool load_active;          /*< Data streaming state (for LOAD DATA LOCAL INFILE) */
    bool m_autocommit {false}; /*< Whether autocommit is on. */

    ProtocolData* protocol_data() const;
    void set_protocol_data(std::unique_ptr<ProtocolData> new_data);

private:
    std::unique_ptr<ProtocolData> m_protocol_data;
    uint32_t m_trx_state {SESSION_TRX_INACTIVE};
    bool m_killed {false};
    uint64_t m_capabilities;
};

/**
 * A filter that terminates the request processing and delivers a response
 * directly should specify the response using this function. After having
 * called this function, the module must not deliver the request further
 * in the request processing pipeline.
 *
 * @param session  The session.
 * @param service  The source of the response
 * @param up       The filter that should receive the response.
 * @param buffer   The response.
 */
void session_set_response(MXS_SESSION* session, SERVICE* service, const mxs::Upstream* up, GWBUF* buffer);

/**
 * Function to be used by protocol module for routing incoming data
 * to the first component in the pipeline of filters and a router.
 *
 * @param session  The session.
 * @param buffer   A buffer.
 *
 * @return True, if the routing should continue, false otherwise.
 */
bool mxs_route_query(MXS_SESSION* session, GWBUF* buffer);

/**
 * Function to be used by the router module to route the replies to
 * the first element in the pipeline of filters and a protocol.
 *
 * @param session  The upstream component
 * @param buffer   A buffer.
 * @param dcb      The DCB where the response came from
 *
 * @return True, if the routing should continue, false otherwise.
 */
bool mxs_route_reply(mxs::Upstream* up, GWBUF* buffer, DCB* dcb);

/**
 * Start the session
 *
 * Called after the session is initialized and authentication is complete. This creates the router and filter
 * sessions.
 *
 * @param session Session to start
 *
 * @return True if session was started successfully
 */
bool session_start(MXS_SESSION* session);

const char* session_get_remote(const MXS_SESSION*);
const char* session_get_user(const MXS_SESSION*);
const char* session_state_to_string(MXS_SESSION::State);

/**
 * Convert transaction state to string representation.
 *
 * @param state A transaction state.
 * @return String representation of the state.
 */
const char* session_trx_state_to_string(uint32_t state);

/**
 * @brief Get a session reference by ID
 *
 * This creates an additional reference to a session whose unique ID matches @c id.
 *
 * @param id Unique session ID
 * @return Reference to a MXS_SESSION or NULL if the session was not found
 *
 * @note The caller must free the session reference by calling session_put_ref
 */
MXS_SESSION* session_get_by_id(uint64_t id);

/**
 * Get the next available unique (assuming no overflow) session id number.
 *
 * @return An unused session id.
 */
uint64_t session_get_next_id();

/**
 * @brief Close a session
 *
 * Calling this function will start the session shutdown process. The shutdown
 * closes all related backend DCBs by calling the closeSession entry point
 * of the router session.
 *
 * @param session The session to close
 */
void session_close(MXS_SESSION* session);

/**
 * @brief Get a session reference
 *
 * This creates an additional reference to a session which allows it to live
 * as long as it is needed.
 *
 * @param session Session reference to get
 * @return Reference to a MXS_SESSION
 *
 * @note The caller must free the session reference by calling session_put_ref
 */
MXS_SESSION* session_get_ref(MXS_SESSION* sessoin);

/**
 * @brief Release a session reference
 *
 * This function is public only because the tee-filter uses it.
 *
 * @param session Session reference to release
 */
void session_put_ref(MXS_SESSION* session);

/**
 * @brief Convert a session to JSON
 *
 * @param session Session to convert
 * @param host    Hostname of this server
 * @param rdns    Attempt reverse DNS on client ip address
 * @return New JSON object or NULL on error
 */
json_t* session_to_json(const MXS_SESSION* session, const char* host, bool rdns);

/**
 * @brief Convert all sessions to JSON
 *
 * @param host Hostname of this server
 * @param rdns Attempt reverse DNS on client ip addresses
 * @return A JSON array with all sessions
 */
json_t* session_list_to_json(const char* host, bool rdns);

/**
 * Qualify the session for connection pooling
 *
 * @param session Session to qualify
 */
void session_qualify_for_pool(MXS_SESSION* session);

/**
 * Check if the session qualifies for connection pooling
 *
 * @param session
 */
bool session_valid_for_pool(const MXS_SESSION* session);

/**
 * @brief Return the session of the dcb currently being processed
 *        by the calling thread.
 *
 * @return A session, or NULL if the calling thread is not currently handling
 *         a dcb or if the calling thread is not a polling/worker thread.
 **/
MXS_SESSION* session_get_current();

/**
 * @brief Return the id of the session of the dcb currently being processed
 *        by the calling thread.
 *
 * @return The id of the current session or 0 if there is no current session.
 **/
uint64_t session_get_current_id();

/**
 * @brief Add new MaxScale specific user variable to the session.
 *
 * The name of the variable must be of the following format:
 *
 *     "@maxscale\.[a-zA-Z_]+(\.[a-zA-Z_])*"
 *
 * e.g. "@maxscale.cache.enabled". A strong suggestion is that the first
 * sub-scope is the same as the module name of the component registering the
 * variable. The sub-scope "core" is reserved by MaxScale.
 *
 * The variable name will be converted to all lowercase when added.
 *
 * @param session   The session in question.
 * @param name      The name of the variable, must start with "@MAXSCALE.".
 * @param handler   The handler function for the variable.
 * @param context   Context that will be passed to the handler function.
 *
 * @return True, if the variable could be added, false otherwise.
 */
bool session_add_variable(
    MXS_SESSION* session, const char* name, session_variable_handler_t handler, void* context);

/**
 * @brief Remove MaxScale specific user variable from the session.
 *
 * With this function a particular MaxScale specific user variable
 * can be removed. Note that it is *not* mandatory to remove a
 * variable when a session is closed, but have to be done in case
 * the context object must manually be deleted.
 *
 * @param session   The session in question.
 * @param name      The name of the variable.
 * @param context   On successful return, if non-NULL, the context object
 *                  that was provided when the variable was added.
 *
 * @return True, if the variable existed, false otherwise.
 */
bool session_remove_variable(MXS_SESSION* session, const char* name, void** context);
/**
 * @brief Set value of maxscale session variable.
 *
 * @param session      The session.
 * @param name_begin   Should point to the beginning of the variable name.
 * @param name_end     Should point one past the end of the variable name.
 * @param value_begin  Should point to the beginning of the value.
 * @param value_end    Should point one past the end of the value.
 *
 * @return NULL if successful, otherwise a dynamically allocated string
 *         containing an end-user friendly error message.
 *
 * @note Should only be called from the protocol module that scans
 *       incoming statements.
 */
char* session_set_variable_value(MXS_SESSION* session,
    const char* name_begin,
    const char* name_end,
    const char* value_begin,
    const char* value_end);

/**
 * @brief Specify how many statements each session should retain for
 *        debugging purposes.
 *
 * @param n  The number of statements.
 */
void session_set_retain_last_statements(uint32_t n);

/**
 * Get retain_last_statements
 */
uint32_t session_get_retain_last_statements();

/**
 * @brief Retain provided statement, if configured to do so.
 *
 * @param session  The session.
 * @param buffer   Buffer assumed to contain a full statement.
 */
void session_retain_statement(MXS_SESSION* session, GWBUF* buffer);

/**
 * @brief Book a server response for the statement currently being handled.
 *
 * @param session         The session.
 * @param server          The server having returned a response.
 * @param final_response  True if this was the final server to respond,
 *                        false otherwise.
 */
void session_book_server_response(MXS_SESSION* session, struct SERVER* server, bool final_response);

/**
 * @brief Reset the server bookkeeping for the current statement.
 *
 * To be called, e.g., after a transaction is rolled back (possibly with
 * results having been reported) and before it is replayed.
 *
 * @param session  The session.
 */
void session_reset_server_bookkeeping(MXS_SESSION* session);

/**
 * @brief Dump the last statements, if statements have been retained.
 *
 * @param session  The session.
 */
void session_dump_statements(MXS_SESSION* pSession);

/**
 * @brief Specify whether statements should be dumped or not.
 *
 * @param value    Whether and when to dump statements.
 */
void session_set_dump_statements(session_dump_statements_t value);

void session_set_session_trace(uint32_t value);

uint32_t session_get_session_trace();

std::string session_get_session_log(MXS_SESSION* pSession);

void session_append_log(MXS_SESSION* pSession, std::string log);

void session_dump_log(MXS_SESSION* pSession);

/**
 * @brief Returns in what contexts statements should be dumped.
 *
 * @return Whether and when to dump statements.
 */
session_dump_statements_t session_get_dump_statements();

/**
 * String version of session_get_dump_statements
 */
const char* session_get_dump_statements_str();

/**
 * @brief Route the query again after a delay
 *
 * @param session The current Session
 * @param down    The downstream component, either a filter or a router
 * @param buffer  The buffer to route
 * @param seconds Number of seconds to wait before routing the query. Use 0 for immediate re-routing.
 *
 * @return True if queuing of the query was successful
 */
bool session_delay_routing(MXS_SESSION* session, mxs::Downstream down, GWBUF* buffer, int seconds);

/**
 * Get the reason why a session was closed
 *
 * @param session Session to inspect
 *
 * @return String representation of the reason why the session was closed. If
 *         the session was closed normally, an empty string is returned.
 */
const char* session_get_close_reason(const MXS_SESSION* session);

static inline void session_set_load_active(MXS_SESSION* session, bool value)
{
    session->load_active = value;
}

static inline bool session_is_load_active(const MXS_SESSION* session)
{
    return session->load_active;
}

namespace maxscale
{
/**
 * Specialization of RegistryTraits for the session registry.
 */
template<>
struct RegistryTraits<MXS_SESSION>
{
    typedef uint64_t id_type;
    typedef MXS_SESSION* entry_type;

    static id_type get_id(entry_type entry) { return entry->id(); }

    static entry_type null_entry() { return NULL; }
};
}  // namespace maxscale
