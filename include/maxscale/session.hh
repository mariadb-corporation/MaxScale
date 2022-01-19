/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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
#include <maxscale/routing.hh>
#include <maxscale/utils.hh>

class DCB;
class SERVICE;
class SERVER;
namespace maxscale
{
struct Routable;
class ListenerData;
}

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
    time_t connect;         /**< Time when the session was started */
} MXS_SESSION_STATS;

/**
 * The downstream element in the filter chain. This may refer to
 * another filter or to a router.
 */
namespace maxscale
{
struct Routable;
class RoutingWorker;
}

/* Specific reasons why a session was closed */
typedef enum
{
    SESSION_CLOSE_NONE = 0,             // No special reason
    SESSION_CLOSE_TIMEOUT,              // Connection timed out
    SESSION_CLOSE_HANDLEERROR_FAILED,   // Router returned an error from handleError
    SESSION_CLOSE_ROUTING_FAILED,       // Router closed DCB
    SESSION_CLOSE_KILLED,               // Killed by another connection
    SESSION_CLOSE_TOO_MANY_CONNECTIONS, // Too many connections
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
typedef char* (* session_variable_handler_t)(void* context,
                                             const char* name,
                                             const char* value_begin,
                                             const char* value_end);

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
        CREATED,    /*< Session created but not started */
        STARTED,    /*< Session is fully functional */
        STOPPING,   /*< Session and router are being closed */
        FAILED,     /*< Creation failed */
        FREE,       /*< The session is freed, only for completeness sake */
    };

    /**
     * If a protocol wants to define custom session-level data, the data should inherit from this class.
     */
    class ProtocolData
    {
    public:
        virtual ~ProtocolData() = default;

        /**
         * Tells whether a transaction is starting. Exact meaning depends on the protocol.
         *
         * @return True if a transaction is starting
         */
        virtual bool is_trx_starting() const = 0;

        /**
         * Tells whether a transaction is active. Exact meaning depends on the protocol.
         *
         * @return True if a transaction is active
         */
        virtual bool is_trx_active() const = 0;

        /**
         * Tells whether a read-only transaction is active. Exact meaning depends on the protocol.
         *
         * @return True if a read-only transaction is active
         */
        virtual bool is_trx_read_only() const = 0;

        /**
         * Tells whether a transaction is ending. Exact meaning depends on the protocol.
         *
         * @return True if a transaction is ending
         */
        virtual bool is_trx_ending() const = 0;
    };

    class EventSubscriber
    {
    public:
        EventSubscriber(MXS_SESSION* session);
        ~EventSubscriber();

        /**
         * This is called by the session when the protocol notifies it of a user data change. Only objects
         * which have registered themselves as being interested in this event are called.
         */
        virtual void userdata_changed() = 0;

    private:
        MXS_SESSION* m_session {nullptr};
    };

    virtual ~MXS_SESSION();

    maxscale::RoutingWorker* worker() const
    {
        return m_worker;
    }

    State state() const
    {
        return m_state;
    }

    uint64_t id() const
    {
        return m_id;
    }

    const std::string& user() const
    {
        return m_user;
    }

    void set_user(const std::string& user)
    {
        m_user = user;
    }

    bool can_pool_backends() const
    {
        return m_can_pool_backends;
    }

    void set_can_pool_backends(bool value)
    {
        m_can_pool_backends = value;
    }

    bool normal_quit() const
    {
        return m_normal_quit;
    }

    void set_normal_quit()
    {
        m_normal_quit = true;
    }

    /**
     * Abruptly stop the session
     *
     * This method should be used to stop the session when an abnormal failure has occurred.
     *
     * @param error An optionl error message that is sent to the client before the session is terminated
     */
    void kill(GWBUF* error = nullptr);

    // Convenience function for client identification
    std::string user_and_host() const
    {
        return "'" + m_user + "'@'" + m_host + "'";
    }

    const std::string& client_remote() const
    {
        return m_host;
    }

    virtual mxs::ClientConnection*       client_connection() = 0;
    virtual const mxs::ClientConnection* client_connection() const = 0;
    virtual void                         set_client_connection(mxs::ClientConnection* client_conn) = 0;

    virtual const mxs::ListenerData* listener_data() = 0;

    /**
     * Start the session. Called after the session is initialized and authentication is complete.
     * This creates the router and filter sessions.
     *
     * @return True on success
     */
    virtual bool start() = 0;

    /**
     * Calling this function will start the session shutdown process. The shutdown
     * closes all related backend DCBs by calling the closeSession entry point
     * of the router session.
     */
    virtual void close() = 0;

    /**
     *  Notify the session that client data has changed. This is supposed to be called by the protocol
     *  when deemed necessary. The exact call conditions are left unspecified.
     */
    virtual void notify_userdata_change() = 0;

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
     * @param name      The name of the variable, must start with "@MAXSCALE.".
     * @param handler   The handler function for the variable.
     * @param context   Context that will be passed to the handler function.
     *
     * @return True, if the variable could be added, false otherwise.
     */
    virtual bool add_variable(const char* name, session_variable_handler_t handler, void* context) = 0;

    /**
     * @brief Set value of maxscale session variable.
     *
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
    virtual char* set_variable_value(const char* name_begin, const char* name_end,
                                     const char* value_begin, const char* value_end) = 0;
    /**
     * @brief Remove MaxScale specific user variable from the session.
     *
     * With this function a particular MaxScale specific user variable
     * can be removed. Note that it is *not* mandatory to remove a
     * variable when a session is closed, but have to be done in case
     * the context object must manually be deleted.
     *
     * @param name      The name of the variable.
     * @param context   On successful return, if non-NULL, the context object
     *                  that was provided when the variable was added.
     *
     * @return True, if the variable existed, false otherwise.
     */
    virtual bool remove_variable(const char* name, void** context) = 0;

    /**
     * Check if log level has been explicitly enabled for this session
     *
     * @return True if the log is enabled
     */
    bool log_is_enabled(int level) const;

    virtual void append_session_log(const std::string& msg) = 0;
    virtual void dump_session_log() = 0;

    /**
     * @brief Retain provided statement, if configured to do so.
     *
     * @param buffer   Buffer assumed to contain a full statement.
     */
    virtual void retain_statement(GWBUF* pBuffer) = 0;

    /**
     * @brief Dump the last statements, if statements have been retained.
     *
     * @param session  The session.
     */
    virtual void dump_statements() const = 0;

    /**
     * @brief Book a server response for the statement currently being handled.
     *
     * @param server          The server having returned a response.
     * @param final_response  True if this was the final server to respond, false otherwise.
     */
    virtual void book_server_response(SERVER* pServer, bool final_response) = 0;

    /**
     * @brief Reset the server bookkeeping for the current statement.
     *
     * To be called, e.g., after a transaction is rolled back (possibly with
     * results having been reported) and before it is replayed.
     */
    virtual void reset_server_bookkeeping() = 0;

protected:
    State                    m_state;   /**< Current descriptor state */
    uint64_t                 m_id;      /**< Unique session identifier */
    maxscale::RoutingWorker* m_worker;
    std::string              m_user;    /**< The session user. */
    std::string              m_host;
    int                      m_log_level = 0;

    MXS_SESSION(const std::string& host, SERVICE* service);

public:

    ClientDCB* client_dcb;      /*< The client connection */

    MXS_SESSION_STATS stats;                    /*< Session statistics */
    SERVICE*          service;                  /*< The service this session is using */
    int               refcount;                 /*< Reference count on the session */

    struct
    {
        mxs::Routable* up;          /*< Upward component to receive buffer. */
        GWBUF*         buffer;      /*< Buffer to deliver to up. */
        SERVICE*       service;     /*< Service where the response originated */
    }               response;       /*< Shortcircuited response */
    session_close_t close_reason;   /*< Reason why the session was closed */

    bool load_active;           /*< Data streaming state (for LOAD DATA LOCAL INFILE) */

    ProtocolData* protocol_data() const;
    void          set_protocol_data(std::unique_ptr<ProtocolData> new_data);

private:
    std::unique_ptr<ProtocolData> m_protocol_data;

    bool m_killed {false};

    /**
     * Is the session shutting down "normally" e.g. via COM_QUIT? If session ended abnormally,
     * last statements can be logged. */
    bool m_normal_quit {false};

    /**
     * Is session in a state where backend connections can be donated to pool and reattached to session?
     * Updated by protocol code. */
    bool m_can_pool_backends {false};

    virtual void add_userdata_subscriber(MXS_SESSION::EventSubscriber* obj) = 0;
    virtual void remove_userdata_subscriber(MXS_SESSION::EventSubscriber* obj) = 0;
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
void session_set_response(MXS_SESSION* session, SERVICE* service, mxs::Routable* up, GWBUF* buffer);


const char* session_state_to_string(MXS_SESSION::State);

/**
 * Get the next available unique (assuming no overflow) session id number.
 *
 * @return An unused session id.
 */
uint64_t session_get_next_id();

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
 * @brief Specify whether statements should be dumped or not.
 *
 * @param value    Whether and when to dump statements.
 */
void session_set_dump_statements(session_dump_statements_t value);

void session_set_session_trace(uint32_t value);

uint32_t session_get_session_trace();

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
 */
void session_delay_routing(MXS_SESSION* session, mxs::Routable* down, GWBUF* buffer, int seconds);

/**
 * @brief Route the query again but using a custom function
 *
 * This version of the function can be used to
 *
 * @param session The current session
 * @param buffer  The buffer to route
 * @param seconds Number of seconds to wait before routing the query. Use 0 for immediate re-routing.
 * @param fn      The function to call
 */
void session_delay_routing(MXS_SESSION* session, GWBUF* buffer, int seconds, std::function<bool(GWBUF*)> fn);

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
    typedef uint64_t     id_type;
    typedef MXS_SESSION* entry_type;

    static id_type get_id(entry_type entry)
    {
        return entry->id();
    }
    static entry_type null_entry()
    {
        return NULL;
    }
};
}
