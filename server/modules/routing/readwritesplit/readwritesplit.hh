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
 * @file Readwritesplit common header
 */

#define MXS_MODULE_NAME "readwritesplit"

#include <maxscale/ccdefs.hh>

#include <unordered_set>
#include <unordered_map>
#include <map>
#include <string>
#include <mutex>
#include <functional>

#include <maxscale/dcb.hh>
#include <maxscale/log.h>
#include <maxscale/queryclassifier.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include <maxscale/session_command.hh>
#include <maxscale/protocol/mysql.h>
#include <maxscale/routingworker.hh>
#include <maxscale/protocol/rwbackend.hh>
#include <maxscale/session_stats.hh>

enum backend_type_t
{
    BE_UNDEFINED = -1,
    BE_MASTER,
    BE_JOINED = BE_MASTER,
    BE_SLAVE,
    BE_COUNT
};

enum connection_type
{
    ALL,
    SLAVE
};

typedef uint32_t route_target_t;

/**
 * This criteria is used when backends are chosen for a router session's use.
 * Backend servers are sorted to ascending order according to the criteria
 * and top N are chosen.
 */
enum select_criteria_t
{
    LEAST_GLOBAL_CONNECTIONS,   /**< all connections established by MaxScale */
    LEAST_ROUTER_CONNECTIONS,   /**< connections established by this router */
    LEAST_BEHIND_MASTER,
    LEAST_CURRENT_OPERATIONS,
    ADAPTIVE_ROUTING
};

/**
 * Controls how master failure is handled
 */
enum failure_mode
{
    RW_FAIL_INSTANTLY,          /**< Close the connection as soon as the master is lost */
    RW_FAIL_ON_WRITE,           /**< Close the connection when the first write is received */
    RW_ERROR_ON_WRITE           /**< Don't close the connection but send an error for writes */
};

/**
 * Enum values for router parameters
 */
static const MXS_ENUM_VALUE use_sql_variables_in_values[] =
{
    {"all",    TYPE_ALL   },
    {"master", TYPE_MASTER},
    {NULL}
};

static const MXS_ENUM_VALUE slave_selection_criteria_values[] =
{
    {"LEAST_GLOBAL_CONNECTIONS", LEAST_GLOBAL_CONNECTIONS},
    {"LEAST_ROUTER_CONNECTIONS", LEAST_ROUTER_CONNECTIONS},
    {"LEAST_BEHIND_MASTER",      LEAST_BEHIND_MASTER     },
    {"LEAST_CURRENT_OPERATIONS", LEAST_CURRENT_OPERATIONS},
    {"ADAPTIVE_ROUTING",         ADAPTIVE_ROUTING        },
    {NULL}
};

static const MXS_ENUM_VALUE master_failure_mode_values[] =
{
    {"fail_instantly", RW_FAIL_INSTANTLY},
    {"fail_on_write",  RW_FAIL_ON_WRITE },
    {"error_on_write", RW_ERROR_ON_WRITE},
    {NULL}
};

#define BREF_IS_NOT_USED(s)       ((s)->bref_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)         ((s)->bref_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s) ((s)->bref_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)   ((s)->bref_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)         ((s)->bref_state & BREF_CLOSED)
#define BREF_HAS_FAILED(s)        ((s)->bref_state & BREF_FATAL_FAILURE)

/** default values for rwsplit configuration parameters */
#define CONFIG_MAX_SLAVE_CONN   1
#define CONFIG_MAX_SLAVE_RLAG   -1  /**< not used */
#define CONFIG_SQL_VARIABLES_IN TYPE_ALL

#define BACKEND_TYPE(b) \
    (server_is_master((b)->backend_server) ? BE_MASTER      \
                                           : (server_is_slave((b)->backend_server) ? BE_SLAVE :  BE_UNDEFINED));

#define MARIADB_WAIT_GTID_FUNC "MASTER_GTID_WAIT"
#define MYSQL_WAIT_GTID_FUNC   "WAIT_FOR_EXECUTED_GTID_SET"
static const char gtid_wait_stmt[] =
    "SET @maxscale_secret_variable=(SELECT CASE WHEN %s('%s', %s) = 0 "
    "THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END);";

/** Function that returns a "score" for a server to enable comparison.
 *  Smaller numbers are better.
 */
using BackendSelectFunction = std::function<mxs::PRWBackends::iterator (mxs::PRWBackends& sBackends)>;
BackendSelectFunction get_backend_select_function(select_criteria_t);

struct Config
{
    Config(MXS_CONFIG_PARAMETER* params)
        : slave_selection_criteria(
            (select_criteria_t)config_get_enum(
                params, "slave_selection_criteria", slave_selection_criteria_values))
        , backend_select_fct(get_backend_select_function(slave_selection_criteria))
        , use_sql_variables_in(
            (mxs_target_t)config_get_enum(
                params, "use_sql_variables_in", use_sql_variables_in_values))
        , master_failure_mode(
            (enum failure_mode)config_get_enum(
                params, "master_failure_mode", master_failure_mode_values))
        , max_sescmd_history(config_get_integer(params, "max_sescmd_history"))
        , disable_sescmd_history(config_get_bool(params, "disable_sescmd_history"))
        , master_accept_reads(config_get_bool(params, "master_accept_reads"))
        , strict_multi_stmt(config_get_bool(params, "strict_multi_stmt"))
        , strict_sp_calls(config_get_bool(params, "strict_sp_calls"))
        , retry_failed_reads(config_get_bool(params, "retry_failed_reads"))
        , connection_keepalive(config_get_integer(params, "connection_keepalive"))
        , max_slave_replication_lag(config_get_integer(params, "max_slave_replication_lag"))
        , rw_max_slave_conn_percent(0)
        , max_slave_connections(0)
        , causal_reads(config_get_bool(params, "causal_reads"))
        , causal_reads_timeout(config_get_string(params, "causal_reads_timeout"))
        , master_reconnection(config_get_bool(params, "master_reconnection"))
        , delayed_retry(config_get_bool(params, "delayed_retry"))
        , delayed_retry_timeout(config_get_integer(params, "delayed_retry_timeout"))
        , transaction_replay(config_get_bool(params, "transaction_replay"))
        , trx_max_size(config_get_size(params, "transaction_replay_max_size"))
        , optimistic_trx(config_get_bool(params, "optimistic_trx"))
    {
        if (causal_reads)
        {
            retry_failed_reads = true;
        }

        /** These options cancel each other out */
        if (disable_sescmd_history && max_sescmd_history > 0)
        {
            max_sescmd_history = 0;
        }

        if (optimistic_trx)
        {
            // Optimistic transaction routing requires transaction replay
            transaction_replay = true;
        }

        if (transaction_replay)
        {
            /**
             * Replaying transactions requires that we are able to do delayed query
             * retries and reconnect to a master.
             */
            delayed_retry = true;
            master_reconnection = true;
            master_failure_mode = RW_FAIL_ON_WRITE;
        }
    }

    select_criteria_t     slave_selection_criteria;     /**< The slave selection criteria */
    BackendSelectFunction backend_select_fct;

    mxs_target_t use_sql_variables_in;  /**< Whether to send user variables to
                                         * master or all nodes */
    failure_mode master_failure_mode;   /**< Master server failure handling mode */
    uint64_t     max_sescmd_history;    /**< Maximum amount of session commands to store */
    bool         disable_sescmd_history;/**< Disable session command history */
    bool         master_accept_reads;   /**< Use master for reads */
    bool         strict_multi_stmt;     /**< Force non-multistatement queries to be routed to
                                         * the master after a multistatement query. */
    bool strict_sp_calls;               /**< Lock session to master after an SP call */
    bool retry_failed_reads;            /**< Retry failed reads on other servers */
    int  connection_keepalive;          /**< Send pings to servers that have been idle
                                         * for too long */
    int max_slave_replication_lag;      /**< Maximum replication lag */
    int rw_max_slave_conn_percent;      /**< Maximum percentage of slaves to use for
                                         * each connection*/
    int         max_slave_connections;  /**< Maximum number of slaves for each connection*/
    bool        causal_reads;           /**< Enable causual read */
    std::string causal_reads_timeout;   /**< Timeout, second parameter of function master_wait_gtid */
    bool        master_reconnection;    /**< Allow changes in master server */
    bool        delayed_retry;          /**< Delay routing if no target found */
    uint64_t    delayed_retry_timeout;  /**< How long to delay until an error is returned */
    bool        transaction_replay;     /**< Replay failed transactions */
    size_t      trx_max_size;           /**< Max transaction size for replaying */
    bool        optimistic_trx;         /**< Enable optimistic transactions */
};

/**
 * The statistics for this router instance
 */
struct Stats
{
    uint64_t n_sessions = 0;        /**< Number sessions created */
    uint64_t n_queries = 0;         /**< Number of queries forwarded */
    uint64_t n_master = 0;          /**< Number of stmts sent to master */
    uint64_t n_slave = 0;           /**< Number of stmts sent to slave */
    uint64_t n_all = 0;             /**< Number of stmts sent to all */
    uint64_t n_trx_replay = 0;      /**< Number of replayed transactions */
    uint64_t n_ro_trx = 0;          /**< Read-only transaction count */
    uint64_t n_rw_trx = 0;          /**< Read-write transaction count */
};

using maxscale::ServerStats;
using maxscale::SrvStatMap;

class RWSplitSession;

/**
 * The per instance data for the router.
 */
class RWSplit : public mxs::Router<RWSplit, RWSplitSession>
{
    RWSplit(const RWSplit&);
    RWSplit& operator=(const RWSplit&);

public:

    RWSplit(SERVICE* service, const Config& config);
    ~RWSplit();

    SERVICE*      service() const;
    const Config& config() const;
    Stats&        stats();
    const Stats&  stats() const;
    ServerStats&  server_stats(SERVER* server);
    SrvStatMap    all_server_stats() const;

    int  max_slave_count() const;
    bool have_enough_servers() const;
    bool select_connect_backend_servers(MXS_SESSION* session,
                                        mxs::PRWBackends& backends,
                                        mxs::RWBackend**  current_master,
                                        mxs::SessionCommandList* sescmd_list,
                                        int* expected_responses,
                                        connection_type type);
    // API functions

    /**
     * @brief Create a new readwritesplit router instance
     *
     * An instance of the router is required for each service that uses this router.
     * One instance of the router will handle multiple router sessions.
     *
     * @param service The service this router is being create for
     * @param options The options for this query router
     *
     * @return New router instance or NULL on error
     */
    static RWSplit* create(SERVICE* pService, MXS_CONFIG_PARAMETER* params);

    /**
     * @brief Create a new session for this router instance
     *
     * The session is used to store all the data required by the router for a
     * particular client connection. The instance of the router that relates to a
     * particular service is passed as the first parameter. The second parameter is
     * the session that has been created in response to the request from a client
     * for a connection. The passed session contains generic information; this
     * function creates the session structure that holds router specific data.
     * There is often a one to one relationship between sessions and router
     * sessions, although it is possible to create configurations where a
     * connection is handled by multiple routers, one after another.
     *
     * @param session  The MaxScale session (generic connection data)
     *
     * @return New router session or NULL on error
     */
    RWSplitSession* newSession(MXS_SESSION* pSession);

    /**
     * @brief Diagnostics routine
     *
     * Print query router diagnostics to the DCB passed in
     *
     * @param dcb      The DCB for diagnostic output
     */
    void diagnostics(DCB* pDcb);

    /**
     * @brief JSON diagnostics routine
     *
     * @return The JSON representation of this router instance
     */
    json_t* diagnostics_json() const;

    /**
     * @brief Get router capabilities
     */
    uint64_t getCapabilities();

    bool configure(MXS_CONFIG_PARAMETER* params);
private:

    // Update configuration
    void    store_config(const Config& config);
    void    update_local_config() const;
    Config* get_local_config() const;

    // Called when worker local data needs to be updated
    static void update_config(void* data);

    SERVICE*                       m_service;   /**< Service where the router belongs*/
    mxs::rworker_local<Config>     m_config;
    Stats                          m_stats;
    mxs::rworker_local<SrvStatMap> m_server_stats;
};

static inline const char* select_criteria_to_str(select_criteria_t type)
{
    switch (type)
    {
    case LEAST_GLOBAL_CONNECTIONS:
        return "LEAST_GLOBAL_CONNECTIONS";

    case LEAST_ROUTER_CONNECTIONS:
        return "LEAST_ROUTER_CONNECTIONS";

    case LEAST_BEHIND_MASTER:
        return "LEAST_BEHIND_MASTER";

    case LEAST_CURRENT_OPERATIONS:
        return "LEAST_CURRENT_OPERATIONS";

    case ADAPTIVE_ROUTING:
        return "ADAPTIVE_ROUTING";

    default:
        return "UNDEFINED_CRITERIA";
    }
}

static inline const char* failure_mode_to_str(enum failure_mode type)
{
    switch (type)
    {
    case RW_FAIL_INSTANTLY:
        return "fail_instantly";

    case RW_FAIL_ON_WRITE:
        return "fail_on_write";

    case RW_ERROR_ON_WRITE:
        return "error_on_write";

    default:
        mxb_assert(false);
        return "UNDEFINED_MODE";
    }
}

void closed_session_reply(GWBUF* querybuf);
bool send_readonly_error(DCB* dcb);

mxs::RWBackend* get_root_master(const mxs::PRWBackends& backends);

/**
 * Get total slave count and connected slave count
 *
 * @param backends List of backend servers
 * @param master   Current master
 *
 * @return Total number of slaves and number of slaves we are connected to
 */
std::pair<int, int> get_slave_counts(mxs::PRWBackends& backends, mxs::RWBackend* master);

/**
 * Find the best backend by grouping the servers by priority, and then applying
 * selection criteria to the best group.
 *
 * @param backends: vector of RWBackend*
 * @param select:   selection function
 * @param master_accept_reads: NOTE: even if this is false, in some cases a master can
 *                             still be selected for reads.
 *
 * @return Valid iterator into argument backends, or end(backends) if empty
 */
mxs::PRWBackends::iterator find_best_backend(mxs::PRWBackends& backends,
                                             BackendSelectFunction select,
                                             bool masters_accepts_reads);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void close_all_connections(mxs::PRWBackends& backends);
