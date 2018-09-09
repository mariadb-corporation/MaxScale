/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file include/maxscale/session.h - The public session interface
 */

#include <maxscale/cdefs.h>

#include <time.h>

#include <maxbase/atomic.h>
#include <maxbase/jansson.h>
#include <maxscale/dcb.h>
#include <maxscale/buffer.h>
#include <maxscale/log.h>
#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

struct dcb;
struct service;
struct mxs_filter_def;
struct mxs_filter;
struct mxs_filter_session;
struct mxs_router_session;
struct server;

typedef enum
{
    SESSION_STATE_ALLOC,            /*< for all sessions */
    SESSION_STATE_READY,            /*< for router session */
    SESSION_STATE_ROUTER_READY,     /*< for router session */
    SESSION_STATE_STOPPING,         /*< session and router are being closed */
    SESSION_STATE_LISTENER,         /*< for listener session */
    SESSION_STATE_LISTENER_STOPPED, /*< for listener session */
    SESSION_STATE_TO_BE_FREED,      /*< ready to be freed as soon as there are no references */
    SESSION_STATE_FREE,             /*< for all sessions */
    SESSION_STATE_DUMMY             /*< dummy session for consistency */
} mxs_session_state_t;

#define STRSESSIONSTATE(s) \
    ((s) == SESSION_STATE_ALLOC ? "SESSION_STATE_ALLOC"   \
                                : ((s) == SESSION_STATE_READY ? "SESSION_STATE_READY"   \
                                                              : ((s) \
                                                                 == SESSION_STATE_ROUTER_READY ? \
                                                                 "SESSION_STATE_ROUTER_READY"   \
                                                                                               : ((s) \
                                                                                                  == \
                                                                                                  SESSION_STATE_STOPPING \
                                                                                                  ? \
                                                                                                  "SESSION_STATE_STOPPING"  \
                                                                                                  : (( \
                                                                                                         s) \
                                                                                                     == \
                                                                                                     SESSION_STATE_LISTENER \
                                                                                                     ? \
                                                                                                     "SESSION_STATE_LISTENER"   \
                                                                                                     : (( \
                                                                                                            s) \
                                                                                                        == \
                                                                                                        SESSION_STATE_LISTENER_STOPPED \
                                                                                                        ? \
                                                                                                        "SESSION_STATE_LISTENER_STOPPED"   \
                                                                                                        : (( \
                                                                                                               s) \
                                                                                                           == \
                                                                                                           SESSION_STATE_TO_BE_FREED \
                                                                                                           ? \
                                                                                                           "SESSION_STATE_TO_BE_FREED"   \
                                                                                                           : (( \
                                                                                                                  s) \
                                                                                                              == \
                                                                                                              SESSION_STATE_FREE \
                                                                                                              ? \
                                                                                                              "SESSION_STATE_TO_BE_FREE"   \
                                                                                                              : (( \
                                                                                                                     s) \
                                                                                                                 == \
                                                                                                                 SESSION_STATE_DUMMY \
                                                                                                                 ? \
                                                                                                                 "SESSION_STATE_DUMMY"   \
                                                                                                                 : \
                                                                                                                 "SESSION_STATE_UNKNOWN")))))))))

typedef enum
{
    SESSION_TRX_INACTIVE_BIT   = 0x01,  /* 0b00001 */
    SESSION_TRX_ACTIVE_BIT     = 0x02,  /* 0b00010 */
    SESSION_TRX_READ_ONLY_BIT  = 0x04,  /* 0b00100 */
    SESSION_TRX_READ_WRITE_BIT = 0x08,  /* 0b01000 */
    SESSION_TRX_ENDING_BIT     = 0x10,  /* 0b10000*/
} session_trx_state_bit_t;

typedef enum
{
    /*< There is no on-going transaction. */
    SESSION_TRX_INACTIVE = SESSION_TRX_INACTIVE_BIT,
    /*< A transaction is active. */
    SESSION_TRX_ACTIVE = SESSION_TRX_ACTIVE_BIT,
    /*< An explicit READ ONLY transaction is active. */
    SESSION_TRX_READ_ONLY = (SESSION_TRX_ACTIVE_BIT | SESSION_TRX_READ_ONLY_BIT),
    /*< An explicit READ WRITE transaction is active. */
    SESSION_TRX_READ_WRITE = (SESSION_TRX_ACTIVE_BIT | SESSION_TRX_READ_WRITE_BIT),
    /*< An explicit READ ONLY transaction is ending. */
    SESSION_TRX_READ_ONLY_ENDING = (SESSION_TRX_ENDING_BIT | SESSION_TRX_READ_ONLY),
    /*< An explicit READ WRITE transaction is ending. */
    SESSION_TRX_READ_WRITE_ENDING = (SESSION_TRX_ENDING_BIT | SESSION_TRX_READ_WRITE),
} mxs_session_trx_state_t;

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
struct mxs_filter;
struct mxs_filter_session;

// These are more convenient types
typedef int32_t (* DOWNSTREAMFUNC)(struct mxs_filter* instance,
                                   struct mxs_filter_session* session,
                                   GWBUF* response);
typedef int32_t (* UPSTREAMFUNC)(struct mxs_filter* instance,
                                 struct mxs_filter_session* session,
                                 GWBUF* response);

typedef struct mxs_downstream
{
    struct mxs_filter*         instance;
    struct mxs_filter_session* session;
    DOWNSTREAMFUNC             routeQuery;
} MXS_DOWNSTREAM;

/**
 * The upstream element in the filter chain. This may refer to
 * another filter or to the protocol implementation.
 */
typedef struct mxs_upstream
{
    struct mxs_filter*         instance;
    struct mxs_filter_session* session;
    UPSTREAMFUNC               clientReply;
} MXS_UPSTREAM;

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
typedef struct session
{
    mxs_session_state_t state;              /*< Current descriptor state */
    uint64_t            ses_id;             /*< Unique session identifier */
    struct dcb*         client_dcb;         /*< The client connection */

    struct mxs_router_session* router_session;          /*< The router instance data */
    MXS_SESSION_STATS          stats;                   /*< Session statistics */
    struct service*            service;                 /*< The service this session is using */
    MXS_DOWNSTREAM             head;                    /*< Head of the filter chain */
    MXS_UPSTREAM               tail;                    /*< The tail of the filter chain */
    int                        refcount;                /*< Reference count on the session */
    mxs_session_trx_state_t    trx_state;               /*< The current transaction state. */
    bool                       autocommit;              /*< Whether autocommit is on. */
    intptr_t                   client_protocol_data;    /*< Owned and managed by the client protocol. */
    bool                       qualifies_for_pooling;   /**< Whether this session qualifies for the connection
                                                         * pool */
    struct
    {
        MXS_UPSTREAM up;            /*< Upward component to receive buffer. */
        GWBUF*       buffer;        /*< Buffer to deliver to up. */
    }               response;       /*< Shortcircuited response */
    session_close_t close_reason;   /*< Reason why the session was closed */
    bool            load_active;    /*< Data streaming state (for LOAD DATA LOCAL INFILE) */
} MXS_SESSION;

/**
 * A filter that terminates the request processing and delivers a response
 * directly should specify the response using this function. After having
 * called this function, the module must not deliver the request further
 * in the request processing pipeline.
 *
 * @param session  The session.
 * @param up       The filter that should receive the response.
 * @param buffer   The response.
 */
void session_set_response(MXS_SESSION* session, const MXS_UPSTREAM* up, GWBUF* buffer);

/**
 * Function to be used by protocol module for routing incoming data
 * to the first component in the pipeline of filters and a router.
 *
 * @param session  The session.
 * @param buffer   A buffer.
 *
 * @return True, if the routing should continue, false otherwise.
 */
bool session_route_query(MXS_SESSION* session, GWBUF* buffer);

/**
 * Function to be used by the router module to route the replies to
 * the first element in the pipeline of filters and a protocol.
 *
 * @param session  The session.
 * @param buffer   A buffer.
 *
 * @return True, if the routing should continue, false otherwise.
 */
bool session_route_reply(MXS_SESSION* session, GWBUF* buffer);

/**
 * A convenience macro that can be used by the protocol modules to route
 * the incoming data to the first element in the pipeline of filters and
 * routers.
 */
#define MXS_SESSION_ROUTE_QUERY(sess, buf) session_route_query(sess, buf)

/**
 * A convenience macro that can be used by the router modules to route
 * the replies to the first element in the pipeline of filters and
 * the protocol.
 */
#define MXS_SESSION_ROUTE_REPLY(sess, buf) session_route_reply(sess, buf)

/**
 * Allocate a new session for a new client of the specified service.
 *
 * Create the link to the router session by calling the newSession
 * entry point of the router using the router instance of the
 * service this session is part of.
 *
 * @param service       The service this connection was established by
 * @param client_dcb    The client side DCB
 * @return              The newly created session or NULL if an error occurred
 */
MXS_SESSION* session_alloc(struct service*, struct dcb*);

/**
 * A version of session_alloc() which takes the session id number as parameter.
 * The id should have been generated with session_get_next_id().
 *
 * @param service       The service this connection was established by
 * @param client_dcb    The client side DCB
 * @param id            Id for the new session.
 * @return              The newly created session or NULL if an error occurred
 */
MXS_SESSION* session_alloc_with_id(struct service*, struct dcb*, uint64_t);

MXS_SESSION* session_set_dummy(struct dcb*);

static inline bool session_is_dummy(MXS_SESSION* session)
{
    return session->state == SESSION_STATE_DUMMY;
}

const char* session_get_remote(const MXS_SESSION*);
const char* session_get_user(const MXS_SESSION*);

/**
 * Convert transaction state to string representation.
 *
 * @param state A transaction state.
 * @return String representation of the state.
 */
const char* session_trx_state_to_string(mxs_session_trx_state_t state);

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
 * @param ses The MXS_SESSION object.
 * @return The transaction state.
 */
mxs_session_trx_state_t session_get_trx_state(const MXS_SESSION* ses);

/**
 * Set the transaction state of the session.
 *
 * NOTE: Only the protocol object may call this.
 *
 * @param ses       The MXS_SESSION object.
 * @param new_state The new transaction state.
 *
 * @return The previous transaction state.
 */
mxs_session_trx_state_t session_set_trx_state(MXS_SESSION* ses, mxs_session_trx_state_t new_state);

/**
 * Tells whether an explicit READ ONLY transaction is active.
 *
 * @see session_get_trx_state
 *
 * @note The return value is valid only if either a router or a filter
 *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
 *
 * @return True if an explicit READ ONLY transaction is active,
 *         false otherwise.
 */
static inline bool session_trx_is_read_only(const MXS_SESSION* ses)
{
    return ses->trx_state == SESSION_TRX_READ_ONLY || ses->trx_state == SESSION_TRX_READ_ONLY_ENDING;
}

/**
 * Tells whether an explicit READ WRITE transaction is active.
 *
 * @see session_get_trx_state
 *
 * @note The return value is valid only if either a router or a filter
 *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
 *
 * @return True if an explicit READ WRITE  transaction is active,
 *         false otherwise.
 */
static inline bool session_trx_is_read_write(const MXS_SESSION* ses)
{
    return ses->trx_state == SESSION_TRX_READ_WRITE || ses->trx_state == SESSION_TRX_READ_WRITE_ENDING;
}

/**
 * Tells whether a transaction is ending.
 *
 * @see session_get_trx_state
 *
 * @note The return value is valid only if either a router or a filter
 *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
 *
 * @return True if a transaction that was active is ending either via COMMIT or ROLLBACK.
 */
static inline bool session_trx_is_ending(const MXS_SESSION* ses)
{
    return ses->trx_state & SESSION_TRX_ENDING_BIT;
}

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
 * @see session_get_trx_state
 *
 * @return True if autocommit has been set ON, false otherwise.
 */
static inline bool session_is_autocommit(const MXS_SESSION* ses)
{
    return ses->autocommit;
}

/**
 * Tells whether a transaction is active.
 *
 * @see session_get_trx_state
 *
 * @note The return value is valid only if either a router or a filter
 *       has declared that it needs RCAP_TYPE_TRANSACTION_TRACKING.
 *
 * @return True if a transaction is active, false otherwise.
 */
static inline bool session_trx_is_active(const MXS_SESSION* ses)
{
    return !session_is_autocommit(ses) || (ses->trx_state & SESSION_TRX_ACTIVE_BIT);
}

/**
 * Sets the autocommit state of the session.
 *
 * NOTE: Only the protocol object may call this.
 *
 * @param enable True if autocommit is enabled, false otherwise.
 * @return The previous state.
 */
static inline bool session_set_autocommit(MXS_SESSION* ses, bool autocommit)
{
    bool prev_autocommit = ses->autocommit;
    ses->autocommit = autocommit;
    return prev_autocommit;
}

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
 *
 * @return New JSON object or NULL on error
 */
json_t* session_to_json(const MXS_SESSION* session, const char* host);

/**
 * @brief Convert all sessions to JSON
 *
 * @param host Hostname of this server
 *
 * @return A JSON array with all sessions
 */
json_t* session_list_to_json(const char* host);

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
bool session_add_variable(MXS_SESSION* session,
                          const char*  name,
                          session_variable_handler_t handler,
                          void* context);

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
bool session_remove_variable(MXS_SESSION* session,
                             const char*  name,
                             void** context);
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
                                 const char*  name_begin,
                                 const char*  name_end,
                                 const char*  value_begin,
                                 const char*  value_end);

/**
 * @brief Specify how many statements each session should retain for
 *        debugging purposes.
 *
 * @param n  The number of statements.
 */
void session_set_retain_last_statements(uint32_t n);

/**
 * @brief Retain provided statement, if configured to do so.
 *
 * @param session  The session.
 * @param buffer   Buffer assumed to contain a full statement.
 */
void session_retain_statement(MXS_SESSION* session, GWBUF* buffer);

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

/**
 * @brief Returns in what contexts statements should be dumped.
 *
 * @return Whether and when to dump statements.
 */
session_dump_statements_t session_get_dump_statements();

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
bool session_delay_routing(MXS_SESSION* session, MXS_DOWNSTREAM down, GWBUF* buffer, int seconds);

/**
 * Cast the session's router as a MXS_DOWNSTREAM object
 *
 * @param session The session to use
 *
 * @return The router cast as MXS_DOWNSTREAM
 */
MXS_DOWNSTREAM router_as_downstream(MXS_SESSION* session);

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

MXS_END_DECLS
