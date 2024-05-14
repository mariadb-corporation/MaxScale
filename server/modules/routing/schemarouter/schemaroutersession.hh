/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "schemarouter.hh"

#include <string>
#include <list>

#include <maxbase/string.hh>
#include <maxscale/router.hh>
#include <maxscale/protocol/mariadb/client_connection.hh>

#include "shard_map.hh"

namespace schemarouter
{
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

static std::string to_string(init_mask mask)
{
    if (mask == INIT_READY)
    {
        return "INIT_READY";
    }

    std::vector<std::string> values;

    if (mask & INIT_MAPPING)
    {
        values.push_back("INIT_MAPPING");
    }

    if (mask & INIT_USE_DB)
    {
        values.push_back("INIT_USE_DB");
    }

    if (mask & INIT_UNINT)
    {
        values.push_back("INIT_UNINT");
    }

    if (mask & INIT_FAILED)
    {
        values.push_back("INIT_FAILED");
    }

    return mxb::join(values, "|");
}

enum showdb_response
{
    SHOWDB_FULL_RESPONSE,
    SHOWDB_PARTIAL_RESPONSE,
    SHOWDB_DUPLICATE_DATABASES,
    SHOWDB_FATAL_ERROR
};

static inline std::string to_string(showdb_response response)
{
    switch (response)
    {
    case SHOWDB_FULL_RESPONSE:
        return "SHOWDB_FULL_RESPONSE";

    case SHOWDB_PARTIAL_RESPONSE:
        return "SHOWDB_PARTIAL_RESPONSE";

    case SHOWDB_DUPLICATE_DATABASES:
        return "SHOWDB_DUPLICATE_DATABASES";

    case SHOWDB_FATAL_ERROR:
        return "SHOWDB_FATAL_ERROR";
    }

    return "UNKNOWN";
}

#define SCHEMA_ERR_DUPLICATEDB    5000
#define SCHEMA_ERRSTR_DUPLICATEDB "DUPDB"
#define SCHEMA_ERR_DBNOTFOUND     1049
#define SCHEMA_ERRSTR_DBNOTFOUND  "42000"

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

class SchemaRouter;

/**
 * The client session structure used within this router.
 */
class SchemaRouterSession : public mxs::RouterSession
                          , public mariadb::QueryClassifier::Handler
{
public:

    SchemaRouterSession(MXS_SESSION* session, SchemaRouter* router, SRBackendList backends);

    ~SchemaRouterSession();

    bool routeQuery(GWBUF&& packet) override;

    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& pBackend, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, const std::string& message,
                     mxs::Endpoint* pProblem, const mxs::Reply& reply) override final;

    bool lock_to_master() override
    {
        return false;
    }

    bool is_locked_to_master() const override
    {
        return false;
    }

    bool supports_hint(Hint::Type hint_type) const override
    {
        return hint_type == Hint::Type::ROUTE_TO_NAMED_SERVER;
    }

private:
    /**
     * Internal functions
     */

    /** Helper functions */
    bool         wait_for_shard(GWBUF& packet);
    bool         wait_for_init(GWBUF& packet);
    mxs::Target* get_shard_target(const GWBUF& buffer, uint32_t qtype);
    SRBackend*   get_shard_backend(std::string_view name);
    bool         have_servers();
    void         handle_default_db();
    void         handle_default_db_response();
    bool         ignore_duplicate_table(std::string_view data) const;
    mxs::Target* get_query_target(const GWBUF& buffer);
    mxs::Target* get_ps_target(const GWBUF& buffer, uint32_t qtype, mxs::sql::OpCode op);

    /** Routing functions */
    bool       route_session_write(GWBUF&& querybuf, uint8_t command);
    SRBackend* get_any_backend();
    bool       write_session_command(SRBackend* backend, GWBUF&& buffer, uint8_t cmd);
    SRBackend* resolve_query_target(const GWBUF& pPacket,
                                    uint32_t type,
                                    uint8_t command,
                                    enum route_target route_target);

    /** Shard mapping functions */
    void                 send_databases();
    void                 send_shards();
    void                 query_databases();
    bool                 have_duplicates() const;
    int                  inspect_mapping_states(SRBackend* bref, const mxs::Reply& reply);
    enum showdb_response parse_mapping_response(SRBackend* bref, const mxs::Reply& reply);
    void                 route_queued_query();
    bool                 delay_routing();
    void                 synchronize_shards();
    void                 handle_mapping_reply(SRBackend* bref, const mxs::Reply& reply);
    std::string          get_cache_key() const;
    void                 write_error_to_client(int errnum, const char* mysqlstate, const char* errmsg);
    bool                 change_current_db(const GWBUF& buf, uint8_t cmd);
    mxs::Target*         get_valid_target(const std::set<mxs::Target*>& candidates);

    mxs::Target* get_location(const std::vector<mxs::Parser::TableName>& dbs)
    {
        return get_valid_target(m_shard.get_all_locations(dbs));
    }

    mxs::Target* get_location(const mxs::Parser::TableName& db)
    {
        return get_valid_target(m_shard.get_all_locations(db));
    }

    mxs::Target* get_location(const std::string& db, const std::string& tbl)
    {
        return get_valid_target(m_shard.get_all_locations(db, tbl));
    }

    bool         tables_are_on_all_nodes(const std::set<mxs::Target*>& candidates) const;
    mxs::Target* get_query_target_from_locations(const std::set<mxs::Target*>& locations);

    /** Member variables */
    MariaDBClientConnection* m_client {nullptr};    /**< Client connection */

    MYSQL_session*    m_mysql_session;  /**< Session client data (username, password, SHA1). */
    SRBackendList     m_backends;       /**< Backend references */
    Config::Values    m_config;         /**< Session specific configuration */
    SchemaRouter*     m_router;         /**< The router instance */
    std::string       m_key;            /**< Shard cache key */
    Shard             m_shard;          /**< Database to server mapping */
    std::string       m_connect_db;     /**< Database the user was trying to connect to */
    std::string       m_current_db;     /**< Current active database */
    int               m_state;          /**< Initialization state bitmask */
    std::list<GWBUF>  m_queue;          /**< Query that was received before the session was ready */
    mxs::Target*      m_load_target;    /**< Target for LOAD DATA LOCAL INFILE */
    SRBackend*        m_sescmd_replier {nullptr};
    int               m_num_init_db = 0;
    mxb::Worker::DCId m_dcid {0};
    SRBackend*        m_prev_target {nullptr};

    mariadb::QueryClassifier m_qc;
};
}
