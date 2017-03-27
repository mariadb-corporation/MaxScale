#pragma once
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

#include "schemarouter.hh"

#include <string>
#include <list>

#include <maxscale/protocol/mysql.h>
#include <maxscale/router.hh>

#include "shard_map.hh"
#include "session_command.hh"

using std::string;
using std::list;
using schemarouter::Config;
using schemarouter::Stats;

/**
 * Bitmask values for the router session's initialization. These values are used
 * to prevent responses from internal commands being forwarded to the client.
 */
enum init_mask
{
    INIT_READY   = 0x00,
    INIT_MAPPING = 0x01,
    INIT_USE_DB  = 0x02,
    INIT_UNINT   = 0x04,
    INIT_FAILED  = 0x08
};

enum showdb_response
{
    SHOWDB_FULL_RESPONSE,
    SHOWDB_PARTIAL_RESPONSE,
    SHOWDB_DUPLICATE_DATABASES,
    SHOWDB_FATAL_ERROR
};

/**
 * The state of the backend server reference
 */
enum bref_state
{
    BREF_IN_USE           = 0x01,
    BREF_WAITING_RESULT   = 0x02, /**< for session commands only */
    BREF_QUERY_ACTIVE     = 0x04, /**< for other queries */
    BREF_CLOSED           = 0x08,
    BREF_DB_MAPPED        = 0x10
};

#define SCHEMA_ERR_DUPLICATEDB 5000
#define SCHEMA_ERRSTR_DUPLICATEDB "DUPDB"
#define SCHEMA_ERR_DBNOTFOUND 1049
#define SCHEMA_ERRSTR_DBNOTFOUND "42000"

/**
 * Route target types
 */
enum route_target
{
    TARGET_UNDEFINED,
    TARGET_NAMED_SERVER,
    TARGET_ALL,
    TARGET_ANY
};

/** Helper macros for route target type */
#define TARGET_IS_UNDEFINED(t)    (t == TARGET_UNDEFINED)
#define TARGET_IS_NAMED_SERVER(t) (t == TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t == TARGET_ALL)
#define TARGET_IS_ANY(t)          (t == TARGET_ANY)

/**
 * Reference to BACKEND.
 *
 * Owned by router client session.
 */
class Backend
{
public:
    Backend(SERVER_REF *ref);
    ~Backend();
    bool execute_sescmd();
    void clear_state(enum bref_state state);
    void set_state(enum bref_state state);

    SERVER_REF*        m_backend;         /**< Backend server */
    DCB*               m_dcb;             /**< Backend DCB */
    GWBUF*             m_map_queue;
    bool               m_mapped;          /**< Whether the backend has been mapped */
    int                m_num_mapping_eof;
    int                m_num_result_wait; /**< Number of not yet received results */
    GWBUF*             m_pending_cmd;     /**< Pending commands */
    int                m_state;           /**< State of the backend */
    SessionCommandList m_session_commands;     /**< List of session commands that are
                                              * to be executed on this backend server */
};

typedef list<Backend> BackendList;

// TODO: Move these as member functions, currently they operate on iterators
#define BREF_IS_NOT_USED(s)         ((s)->m_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->m_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->m_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->m_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->m_state & BREF_CLOSED)
#define BREF_IS_MAPPED(s)           ((s)->m_mapped)

class SchemaRouter;

/**
 * The client session structure used within this router.
 */
class SchemaRouterSession: public mxs::RouterSession
{
public:

    SchemaRouterSession(MXS_SESSION* session, SchemaRouter* router);

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
    /** Internal functions */
    SERVER* get_shard_target(GWBUF* buffer, uint32_t qtype);
    Backend* get_bref_from_dcb(DCB* dcb);
    bool get_shard_dcb(DCB** dcb, char* name);
    bool handle_default_db();
    bool handle_error_new_connection(DCB* backend_dcb, GWBUF* errmsg);
    bool have_servers();
    bool route_session_write(GWBUF* querybuf, uint8_t command);
    bool send_database_list();
    int gen_databaselist();
    int inspect_backend_mapping_states(Backend *bref, GWBUF** wbuf);
    bool process_show_shards();
    enum showdb_response parse_showdb_response(Backend* bref, GWBUF** buffer);
    void handle_error_reply_client(DCB* backend_dcb, GWBUF* errmsg);
    void route_queued_query();
    void synchronize_shard_map();
    void handle_mapping_reply(Backend* bref, GWBUF** pPacket);
    void process_response(Backend* bref, GWBUF** ppPacket);
    SERVER* resolve_query_target(GWBUF* pPacket, uint32_t type, uint8_t command,
                                 enum route_target& route_target);
    bool ignore_duplicate_database(const char* data);

    /** Member variables */
    bool                  m_closed;         /**< True if session closed */
    DCB*                  m_client;         /**< The client DCB */
    MYSQL_session*        m_mysql_session;  /**< Session client data (username, password, SHA1). */
    BackendList           m_backends;       /**< Backend references */
    Config*               m_config;         /**< Pointer to router config */
    SchemaRouter*         m_router;         /**< The router instance */
    Shard                 m_shard;          /**< Database to server mapping */
    string                m_connect_db;     /**< Database the user was trying to connect to */
    string                m_current_db;     /**< Current active database */
    int                   m_state;          /**< Initialization state bitmask */
    list<Buffer>          m_queue;          /**< Query that was received before the session was ready */
    Stats                 m_stats;          /**< Statistics for this router */
    uint64_t              m_sent_sescmd;    /**< The latest session command being executed */
    uint64_t              m_replied_sescmd; /**< The last session command reply that was sent to the client */
    Backend*              m_load_target;    /**< Target for LOAD DATA LOCAL INFILE */
};
