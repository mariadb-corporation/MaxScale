#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file include/maxscale/session.h - The public session interface
 */

#include <maxscale/cdefs.h>

#include <time.h>

#include <maxscale/atomic.h>
#include <maxscale/buffer.h>
#include <maxscale/log_manager.h>
#include <maxscale/resultset.h>
#include <maxscale/spinlock.h>
#include <maxscale/jansson.h>

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

typedef enum
{
    SESSION_TRX_INACTIVE_BIT   = 0x01, /* 0b00001 */
    SESSION_TRX_ACTIVE_BIT     = 0x02, /* 0b00010 */
    SESSION_TRX_READ_ONLY_BIT  = 0x04, /* 0b00100 */
    SESSION_TRX_READ_WRITE_BIT = 0x08, /* 0b01000 */
    SESSION_TRX_ENDING_BIT     = 0x10, /* 0b10000*/
} session_trx_state_bit_t;

typedef enum
{
    /*< There is no on-going transaction. */
    SESSION_TRX_INACTIVE          = SESSION_TRX_INACTIVE_BIT,
    /*< A transaction is active. */
    SESSION_TRX_ACTIVE            = SESSION_TRX_ACTIVE_BIT,
    /*< An explicit READ ONLY transaction is active. */
    SESSION_TRX_READ_ONLY         = (SESSION_TRX_ACTIVE_BIT | SESSION_TRX_READ_ONLY_BIT),
    /*< An explicit READ WRITE transaction is active. */
    SESSION_TRX_READ_WRITE        = (SESSION_TRX_ACTIVE_BIT | SESSION_TRX_READ_WRITE_BIT),
    /*< An explicit READ ONLY transaction is ending. */
    SESSION_TRX_READ_ONLY_ENDING  = (SESSION_TRX_ENDING_BIT | SESSION_TRX_READ_ONLY),
    /*< An explicit READ WRITE transaction is ending. */
    SESSION_TRX_READ_WRITE_ENDING = (SESSION_TRX_ENDING_BIT | SESSION_TRX_READ_WRITE),
} mxs_session_trx_state_t;

/**
 * The session statistics structure
 */
typedef struct
{
    time_t          connect;        /**< Time when the session was started */
} MXS_SESSION_STATS;

/**
 * Structure used to track the filter instances and sessions of the filters
 * that are in use within a session.
 */
typedef struct
{
    struct mxs_filter_def *filter;
    struct mxs_filter *instance;
    struct mxs_filter_session *session;
} SESSION_FILTER;

/**
 * The downstream element in the filter chain. This may refer to
 * another filter or to a router.
 */
struct mxs_filter;
struct mxs_filter_session;

typedef struct mxs_downstream
{
    struct mxs_filter *instance;
    struct mxs_filter_session *session;
    int32_t (*routeQuery)(struct mxs_filter *instance, struct mxs_filter_session *session, GWBUF *request);
} MXS_DOWNSTREAM;

/**
 * The upstream element in the filter chain. This may refer to
 * another filter or to the protocol implementation.
 */
typedef struct mxs_upstream
{
    struct mxs_filter *instance;
    struct mxs_filter_session *session;
    int32_t (*clientReply)(struct mxs_filter *instance, struct mxs_filter_session *session, GWBUF *response);
    int32_t (*error)(void *instance, void *session, void *);
} MXS_UPSTREAM;

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
    skygw_chk_t             ses_chk_top;
    mxs_session_state_t     state;            /*< Current descriptor state */
    uint64_t                ses_id;           /*< Unique session identifier */
    struct dcb              *client_dcb;      /*< The client connection */
    struct 
    {
        struct dcb *head;
    } backends;                                /*< The list of backend's DCBs */

    struct mxs_router_session *router_session;  /*< The router instance data */
    MXS_SESSION_STATS       stats;            /*< Session statistics */
    struct service          *service;         /*< The service this session is using */
    int                     n_filters;        /*< Number of filter sessions */
    SESSION_FILTER          *filters;         /*< The filters in use within this session */
    MXS_DOWNSTREAM          head;             /*< Head of the filter chain */
    MXS_UPSTREAM            tail;             /*< The tail of the filter chain */
    int                     refcount;         /*< Reference count on the session */
    mxs_session_trx_state_t trx_state;        /*< The current transaction state. */
    bool                    autocommit;       /*< Whether autocommit is on. */
    intptr_t                client_protocol_data; /*< Owned and managed by the client protocol. */
    struct
    {
        GWBUF *buffer; /**< Buffer containing the statement */
        const struct server *target; /**< Where the statement was sent */
    } stmt;  /**< Current statement being executed */
    bool qualifies_for_pooling; /**< Whether this session qualifies for the connection pool */
    skygw_chk_t     ses_chk_tail;
} MXS_SESSION;

/**
 * A convenience macro that can be used by the protocol modules to route
 * the incoming data to the first element in the pipeline of filters and
 * routers.
 */
#define MXS_SESSION_ROUTE_QUERY(sess, buf)                          \
    ((sess)->head.routeQuery)((sess)->head.instance,            \
                              (sess)->head.session, (buf))
/**
 * A convenience macro that can be used by the router modules to route
 * the replies to the first element in the pipeline of filters and
 * the protocol.
 */
#define MXS_SESSION_ROUTE_REPLY(sess, buf)                          \
    ((sess)->tail.clientReply)((sess)->tail.instance,           \
                               (sess)->tail.session, (buf))

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
MXS_SESSION *session_alloc(struct service *, struct dcb *);

/**
 * A version of session_alloc() which takes the session id number as parameter.
 * The id should have been generated with session_get_next_id().
 *
 * @param service       The service this connection was established by
 * @param client_dcb    The client side DCB
 * @param id            Id for the new session.
 * @return              The newly created session or NULL if an error occurred
 */
MXS_SESSION *session_alloc_with_id(struct service *, struct dcb *, uint64_t);

MXS_SESSION *session_set_dummy(struct dcb *);

static inline bool session_is_dummy(MXS_SESSION* session)
{
    return session->state == SESSION_STATE_DUMMY;
}

const char *session_get_remote(const MXS_SESSION *);
const char *session_get_user(const MXS_SESSION *);

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
void session_close(MXS_SESSION *session);

/**
 * @brief Release a session reference
 *
 * This function is public only because the tee-filter uses it.
 *
 * @param session Session reference to release
 */
void session_put_ref(MXS_SESSION *session);

/**
 * @brief Store the current statement into session
 *
 * This creates an additional reference to the buffer. If an old statement is stored,
 * it will be replaced with a clone of @c buf.
 *
 * @param session Session where statement is stored
 * @param buf Buffer containing the current statement
 * @param server Server where the statement is being executed
 * @return True if statement was successfully stored, false if the cloning of @c buf failed.
 */
bool session_store_stmt(MXS_SESSION *session, GWBUF *buf, const struct server *server);

/**
 * @brief Fetch stored statement
 *
 * The value returned by this call must be freed by the caller with gwbuf_free().
 *
 * @param session Session with a stored statement
 * @param buffer Pointer where the buffer is stored
 * @param target Pointer where target server is stored
 * @return True if a statement was stored
 */
bool session_take_stmt(MXS_SESSION *session, GWBUF **buffer, const struct server **target);

/**
 * @brief Check if the session has a stored statement
 *
 * @param session Session to check
 *
 * @return True if the session has a stored statement
 */
static inline bool session_have_stmt(MXS_SESSION *session)
{
    return session->stmt.buffer;
}

/**
 * Clear the stored statement
 *
 * @param session Session to clear
 */
void session_clear_stmt(MXS_SESSION *session);

/**
 * @brief Convert a session to JSON
 *
 * @param session Session to convert
 * @param host    Hostname of this server
 *
 * @return New JSON object or NULL on error
 */
json_t* session_to_json(const MXS_SESSION *session, const char* host);

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
 * @brief DCB callback for upstream throtting
 * Called by any backend dcb when its writeq is above high water mark or
 * it has reached high water mark and now it is below low water mark,
 * Calling `poll_remove_dcb` or `poll_add_dcb' on client dcb to throttle
 * network traffic from client to mxs.
 *
 * @param dcb      Backend dcb
 * @param reason   Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
int session_upstream_throttle_callback(DCB *dcb, DCB_REASON reason, void *userdata);

/**
 * @brief DCB callback for downstream throtting
 * Called by client dcb when its writeq is above high water mark or
 * it has reached high water mark and now it is below low water mark,
 * Calling `poll_remove_dcb` or `poll_add_dcb' on all backend dcbs to
 * throttle network traffic from server to mxs.
 *
 * @param dcb      client dcb
 * @param reason   Why the callback was called
 * @param userdata Data provided when the callback was added
 * @return Always 0
 */
int session_downstream_throttle_callback(DCB *dcb, DCB_REASON reason, void *userdata);

MXS_END_DECLS
