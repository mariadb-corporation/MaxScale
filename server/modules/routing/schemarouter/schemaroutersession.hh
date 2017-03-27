/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "schemarouter.hh"

#include <string>

#include <maxscale/protocol/mysql.h>
#include <maxscale/router.hh>

#include "shard_map.hh"
#include "session_command.hh"

/**
 * Bitmask values for the router session's initialization. These values are used
 * to prevent responses from internal commands being forwarded to the client.
 */
typedef enum init_mask
{
    INIT_READY   = 0x00,
    INIT_MAPPING = 0x01,
    INIT_USE_DB  = 0x02,
    INIT_UNINT   = 0x04,
    INIT_FAILED  = 0x08
} init_mask_t;

typedef enum showdb_response
{
    SHOWDB_FULL_RESPONSE,
    SHOWDB_PARTIAL_RESPONSE,
    SHOWDB_DUPLICATE_DATABASES,
    SHOWDB_FATAL_ERROR
} showdb_response_t;

/**
 * The state of the backend server reference
 */
typedef enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /**< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /**< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_DB_MAPPED        = 0x10
} bref_state_t;

#define BREF_IS_NOT_USED(s)         ((s)->state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->state & BREF_CLOSED)
#define BREF_IS_MAPPED(s)           ((s)->mapped)

#define SCHEMA_ERR_DUPLICATEDB 5000
#define SCHEMA_ERRSTR_DUPLICATEDB "DUPDB"
#define SCHEMA_ERR_DBNOTFOUND 1049
#define SCHEMA_ERRSTR_DBNOTFOUND "42000"

/**
 * Route target types
 */
typedef enum
{
    TARGET_UNDEFINED    = (1 << 0),
    TARGET_NAMED_SERVER = (1 << 1),
    TARGET_ALL          = (1 << 2),
    TARGET_ANY          = (1 << 3)
} route_target_t;

/** Helper macros for route target type */
#define TARGET_IS_UNDEFINED(t)    (t == TARGET_UNDEFINED)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_ANY(t)          (t & TARGET_ANY)

/**
 * Reference to BACKEND.
 *
 * Owned by router client session.
 */
typedef struct backend_ref_st
{
    int                n_mapping_eof;
    GWBUF*             map_queue;
    SERVER_REF*        backend;         /**< Backend server */
    DCB*               dcb;             /**< Backend DCB */
    int                state;           /**< State of the backend */
    bool               mapped;          /**< Whether the backend has been mapped */
    int                num_result_wait; /**< Number of not yet received results */
    GWBUF*             pending_cmd;     /**< Pending commands */

    SessionCommandList session_commands;     /**< List of session commands that are
                                              * to be executed on this backend server */
} backend_ref_t;

class SchemaRouter;

/**
 * The client session structure used within this router.
 */
class SchemaRouterSession: public mxs::RouterSession
{
public:

    SchemaRouterSession(MXS_SESSION* session, SchemaRouter& router);

    /**
     * The RouterSession instance will be deleted when a client session
     * has terminated. Will be called only after @c close() has been called.
     */
    ~SchemaRouterSession();

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called when a packet being is routed to the backend. The router should
     * forward the packet to the appropriate server(s).
     *
     * @param pPacket A client packet.
     */
    int32_t routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `MXS_SESSION_ROUTE_REPLY`.
     *
     * @param pPacket  A client packet.
     * @param pBackend The backend the packet is coming from.
     */
    void clientReply(GWBUF* pPacket, DCB* pBackend);

    /**
     *
     * @param pMessage  The rror message.
     * @param pProblem  The DCB on which the error occurred.
     * @param action    The context.
     * @param pSuccess  On output, if false, the session will be terminated.
     */
    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);
private:
    bool                  m_closed;         /**< True if session closed */
    DCB*                  m_client;         /**< The client DCB */
    MYSQL_session*        m_mysql_session;  /**< Session client data (username, password, SHA1). */
    backend_ref_t*        m_backends;       /**< Pointer to backend reference array */
    schemarouter_config_t m_config;         /**< Copied config info from router instance */
    int                   m_backend_count;  /**< Number of backends */
    SchemaRouter&         m_router;         /**< The router instance */
    Shard                 m_shard;          /**< Database to server mapping */
    string                m_connect_db;     /**< Database the user was trying to connect to */
    string                m_current_db;     /**< Current active database */
    int                   m_state;          /**< Initialization state bitmask */
    GWBUF*                m_queue;          /**< Query that was received before the session was ready */
    ROUTER_STATS          m_stats;          /**< Statistics for this router */
    uint64_t              m_sent_sescmd;    /**< The latest session command being executed */
    uint64_t              m_replied_sescmd; /**< The last session command reply that was sent to the client */

    /** Internal functions */
    SERVER* get_shard_target(GWBUF* buffer, uint32_t qtype);
    backend_ref_t* get_bref_from_dcb(DCB* dcb);
    bool execute_sescmd_in_backend(backend_ref_t* backend_ref);
    bool get_shard_dcb(DCB** dcb, char* name);
    bool handle_default_db();
    bool handle_error_new_connection(DCB* backend_dcb, GWBUF* errmsg);
    bool have_servers();
    bool route_session_write(GWBUF* querybuf, uint8_t command);
    bool send_database_list();
    int gen_databaselist();
    int inspect_backend_mapping_states(backend_ref_t *bref, GWBUF** wbuf);
    int process_show_shards();
    showdb_response_t parse_showdb_response(backend_ref_t* bref, GWBUF** buffer);
    void handle_error_reply_client(DCB* backend_dcb, GWBUF* errmsg);
    void route_queued_query();
    void synchronize_shard_map();
};
