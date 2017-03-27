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

#define BREF_IS_NOT_USED(s)         ((s)->bref_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->bref_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->bref_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->bref_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->bref_state & BREF_CLOSED)
#define BREF_IS_MAPPED(s)           ((s)->bref_mapped)

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
    SERVER_REF*        bref_backend;         /*< Backend server */
    DCB*               bref_dcb;             /*< Backend DCB */
    int                bref_state;           /*< State of the backend */
    bool               bref_mapped;          /*< Whether the backend has been mapped */
    bool               last_sescmd_replied;
    int                bref_num_result_wait; /*< Number of not yet received results */
    GWBUF*             bref_pending_cmd;     /*< Pending commands */

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
    bool                   closed;             /*< true when closeSession is called      */
    DCB*                   rses_client_dcb;
    MYSQL_session*         rses_mysql_session; /*< Session client data (username, password, SHA1). */
    backend_ref_t*         rses_backend_ref;   /*< Pointer to backend reference array */
    schemarouter_config_t  rses_config;        /*< Copied config info from router instance */
    int                    rses_nbackends;     /*< Number of backends */
    SchemaRouter&          m_router;             /*< The router instance */
    Shard                  shardmap;           /**< Database hash containing names of the databases
                                                * mapped to the servers that contain them */
    string                 connect_db;         /*< Database the user was trying to connect to */
    string                 current_db;         /*< Current active database */
    int                    state;              /*< Initialization state bitmask */
    GWBUF*                 queue;              /*< Query that was received before the session was ready */
    ROUTER_STATS           stats;              /*< Statistics for this router         */

    uint64_t               sent_sescmd;        /**< The latest session command being executed */
    uint64_t               replied_sescmd;     /**< The last session command reply that was sent to the client */

    /** Internal functions */
    void synchronize_shard_map();

    int inspect_backend_mapping_states(backend_ref_t *bref, GWBUF** wbuf);

    bool route_session_write(GWBUF* querybuf, uint8_t command);

    bool handle_error_new_connection(DCB* backend_dcb, GWBUF* errmsg);
    void handle_error_reply_client(DCB* backend_dcb, GWBUF* errmsg);
    int process_show_shards();
    bool handle_default_db();
    void route_queued_query();
    bool get_shard_dcb(DCB** dcb, char* name);
    SERVER* get_shard_target(GWBUF* buffer, uint32_t qtype);
    backend_ref_t* get_bref_from_dcb(DCB* dcb);
    bool have_servers();
    bool send_database_list();
    int gen_databaselist();
    showdb_response_t parse_showdb_response(backend_ref_t* bref, GWBUF** buffer);
    bool execute_sescmd_in_backend(backend_ref_t* backend_ref);
};
