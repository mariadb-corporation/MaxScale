/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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
#include <maxscale/queryclassifier.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include <maxscale/session_command.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>
#include <maxscale/session_stats.hh>
#include <maxscale/workerlocal.hh>
#include <maxscale/config2.hh>

namespace cfg = maxscale::config;
using namespace std::literals::chrono_literals;

constexpr int SLAVE_MAX = 255;

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

enum CausalReadsMode
{
    LOCAL,
    GLOBAL,
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

static const MXS_ENUM_VALUE causal_reads_mode_values[] =
{
    {"local",  (uint64_t)CausalReadsMode::LOCAL },
    {"global", (uint64_t)CausalReadsMode::GLOBAL},
    {NULL}
};

static cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::ROUTER);

static cfg::ParamEnum<mxs_target_t> s_use_sql_variables_in(
    &s_spec, "use_sql_variables_in",
    "Whether to route SQL variable modifications to all servers or only to the master",
{
    {TYPE_ALL, "all"},
    {TYPE_MASTER, "master"},
}, TYPE_ALL, cfg::Param::AT_RUNTIME);

static cfg::ParamEnum<select_criteria_t> s_slave_selection_criteria(
    &s_spec, "slave_selection_criteria", "Slave selection criteria",
{
    {LEAST_GLOBAL_CONNECTIONS, "LEAST_GLOBAL_CONNECTIONS"},
    {LEAST_ROUTER_CONNECTIONS, "LEAST_ROUTER_CONNECTIONS"},
    {LEAST_BEHIND_MASTER, "LEAST_BEHIND_MASTER"},
    {LEAST_CURRENT_OPERATIONS, "LEAST_CURRENT_OPERATIONS"},
    {ADAPTIVE_ROUTING, "ADAPTIVE_ROUTING"}
}, LEAST_CURRENT_OPERATIONS, cfg::Param::AT_RUNTIME);

static cfg::ParamEnum<failure_mode> s_master_failure_mode(
    &s_spec, "master_failure_mode", "Master failure mode behavior",
{
    {RW_FAIL_INSTANTLY, "fail_instantly"},
    {RW_FAIL_ON_WRITE, "fail_on_write"},
    {RW_ERROR_ON_WRITE, "error_on_write"}
}, RW_FAIL_INSTANTLY, cfg::Param::AT_RUNTIME);

static cfg::ParamEnum<CausalReadsMode> s_causal_reads_mode(
    &s_spec, "causal_reads_mode", "Causal reads mode",
{
    {CausalReadsMode::LOCAL, "local"},
    {CausalReadsMode::GLOBAL, "global"},
}, CausalReadsMode::LOCAL, cfg::Param::AT_RUNTIME);

static cfg::ParamSeconds s_max_slave_replication_lag(
    &s_spec, "max_slave_replication_lag", "Maximum allowed slave replication lag",
    cfg::INTERPRET_AS_SECONDS, 0s, cfg::Param::AT_RUNTIME);

static cfg::ParamString s_max_slave_connections(
    &s_spec, "max_slave_connections", "Maximum number of slave connections",
    std::to_string(SLAVE_MAX), cfg::Param::AT_RUNTIME);

static cfg::ParamCount s_slave_connections(
    &s_spec, "slave_connections", "Starting number of slave connections",
    SLAVE_MAX, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_retry_failed_reads(
    &s_spec, "retry_failed_reads", "Automatically retry failed reads outside of transactions",
    true, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_prune_sescmd_history(
    &s_spec, "prune_sescmd_history", "Prune old session command history if the limit is exceeded",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_disable_sescmd_history(
    &s_spec, "disable_sescmd_history", "Disable session command history",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamCount s_max_sescmd_history(
    &s_spec, "max_sescmd_history", "Session command history size",
    50, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_strict_multi_stmt(
    &s_spec, "strict_multi_stmt", "Lock connection to master after multi-statement query",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_strict_sp_calls(
    &s_spec, "strict_sp_calls", "Lock connection to master after a stored procedure is executed",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_master_accept_reads(
    &s_spec, "master_accept_reads", "Use master for reads",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_causal_reads(
    &s_spec, "causal_reads", "Synchronize reads on slaves with the master",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamSeconds s_causal_reads_timeout(
    &s_spec, "causal_reads_timeout", "Timeout for the slave synchronization",
    cfg::INTERPRET_AS_SECONDS, 10s, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_master_reconnection(
    &s_spec, "master_reconnection", "Reconnect to master",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_delayed_retry(
    &s_spec, "delayed_retry", "Retry failed writes outside of transactions",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamSeconds s_delayed_retry_timeout(
    &s_spec, "delayed_retry_timeout", "Timeout for delayed_retry",
    cfg::INTERPRET_AS_SECONDS, 10s, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_transaction_replay(
    &s_spec, "transaction_replay", "Retry failed transactions",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamSize s_transaction_replay_max_size(
    &s_spec, "transaction_replay_max_size", "Maximum size of transaction to retry",
    1024 * 1024 * 1024, cfg::Param::AT_RUNTIME);

static cfg::ParamCount s_transaction_replay_attempts(
    &s_spec, "transaction_replay_attempts", "Maximum number of times to retry a transaction",
    5, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_transaction_replay_retry_on_deadlock(
    &s_spec, "transaction_replay_retry_on_deadlock", "Retry transaction on deadlock",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_optimistic_trx(
    &s_spec, "optimistic_trx", "Optimistically offload transactions to slaves",
    false, cfg::Param::AT_RUNTIME);

static cfg::ParamBool s_lazy_connect(
    &s_spec, "lazy_connect", "Create connections only when needed",
    false, cfg::Param::AT_RUNTIME);

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

#define MARIADB_WAIT_GTID_FUNC "MASTER_GTID_WAIT"
#define MYSQL_WAIT_GTID_FUNC   "WAIT_FOR_EXECUTED_GTID_SET"
static const char gtid_wait_stmt[] =
    "SET @maxscale_secret_variable=(SELECT CASE WHEN %s('%s', %s) = 0 "
    "THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END);";

/** Function that returns a "score" for a server to enable comparison.
 *  Smaller numbers are better.
 */
using BackendSelectFunction = mxs::RWBackend * (*)(mxs::PRWBackends& sBackends);
BackendSelectFunction get_backend_select_function(select_criteria_t);

using std::chrono::seconds;

struct RWSConfig
{
    RWSConfig(const mxs::ConfigParameters& params)
        : slave_selection_criteria(s_slave_selection_criteria.get(params))
        , backend_select_fct(get_backend_select_function(s_slave_selection_criteria.get(params)))
        , use_sql_variables_in(s_use_sql_variables_in.get(params))
        , master_failure_mode(s_master_failure_mode.get(params))
        , max_sescmd_history(s_max_sescmd_history.get(params))
        , prune_sescmd_history(s_prune_sescmd_history.get(params))
        , disable_sescmd_history(s_disable_sescmd_history.get(params))
        , master_accept_reads(s_master_accept_reads.get(params))
        , strict_multi_stmt(s_strict_multi_stmt.get(params))
        , strict_sp_calls(s_strict_sp_calls.get(params))
        , retry_failed_reads(s_retry_failed_reads.get(params))
        , max_slave_replication_lag(s_max_slave_replication_lag.get(params).count())
        , rw_max_slave_conn_percent(0)
        , max_slave_connections(0)
        , slave_connections(s_slave_connections.get(params))
        , causal_reads(s_causal_reads.get(params))
        , causal_reads_timeout(std::to_string(s_causal_reads_timeout.get(params).count()))
        , causal_reads_mode(s_causal_reads_mode.get(params))
        , master_reconnection(s_master_reconnection.get(params))
        , delayed_retry(s_delayed_retry.get(params))
        , delayed_retry_timeout(s_delayed_retry_timeout.get(params).count())
        , transaction_replay(s_transaction_replay.get(params))
        , trx_max_size(s_transaction_replay_max_size.get(params))
        , trx_max_attempts(s_transaction_replay_attempts.get(params))
        , trx_retry_on_deadlock(s_transaction_replay_retry_on_deadlock.get(params))
        , optimistic_trx(s_optimistic_trx.get(params))
        , lazy_connect(s_lazy_connect.get(params))
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

        if (transaction_replay || lazy_connect)
        {
            /**
             * Replaying transactions requires that we are able to do delayed query
             * retries. Both transaction replay and lazy connection creation require
             * fail-on-write failure mode and reconnections to masters.
             */
            if (transaction_replay)
            {
                delayed_retry = true;
            }
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
    bool         prune_sescmd_history;  /**< Prune session command history */
    bool         disable_sescmd_history;/**< Disable session command history */
    bool         master_accept_reads;   /**< Use master for reads */
    bool         strict_multi_stmt;     /**< Force non-multistatement queries to be routed to
                                         * the master after a multistatement query. */
    bool strict_sp_calls;               /**< Lock session to master after an SP call */
    bool retry_failed_reads;            /**< Retry failed reads on other servers */
    int  max_slave_replication_lag;     /**< Maximum replication lag */
    int  rw_max_slave_conn_percent;     /**< Maximum percentage of slaves to use for each connection*/
    int  max_slave_connections;         /**< Maximum number of slaves for each connection*/
    int  slave_connections;             /**< Minimum number of slaves for each connection*/

    bool            causal_reads;           /**< Enable causual read */
    std::string     causal_reads_timeout;   /**< Timeout, second parameter of function master_wait_gtid */
    CausalReadsMode causal_reads_mode;

    bool     master_reconnection;       /**< Allow changes in master server */
    bool     delayed_retry;             /**< Delay routing if no target found */
    uint64_t delayed_retry_timeout;     /**< How long to delay until an error is returned */
    bool     transaction_replay;        /**< Replay failed transactions */
    size_t   trx_max_size;              /**< Max transaction size for replaying */
    int64_t  trx_max_attempts;          /**< Maximum number of transaction replay attempts */
    bool     trx_retry_on_deadlock;     /**< Replay the transaction if it ends up in a deadlock */
    bool     optimistic_trx;            /**< Enable optimistic transactions */
    bool     lazy_connect;              /**< Create connections only when needed */
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

using maxscale::SessionStats;
using maxscale::TargetSessionStats;

class RWSplitSession;

/**
 * The per instance data for the router.
 */
class RWSplit : public mxs::Router<RWSplit, RWSplitSession>
{
    RWSplit(const RWSplit&);
    RWSplit& operator=(const RWSplit&);

public:
    struct gtid
    {
        uint32_t domain;
        uint32_t server_id;
        uint64_t sequence;

        static gtid from_string(const std::string& str);
        std::string to_string() const;
        bool        empty() const;
    };

    RWSplit(SERVICE* service, const RWSConfig& config);
    ~RWSplit();

    SERVICE*            service() const;
    const RWSConfig&    config() const;
    Stats&              stats();
    const Stats&        stats() const;
    TargetSessionStats& local_server_stats();
    TargetSessionStats  all_server_stats() const;
    std::string         last_gtid() const;
    void                set_last_gtid(const std::string& gtid);

    int  max_slave_count() const;
    bool have_enough_servers() const;

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
    static RWSplit* create(SERVICE* pService, mxs::ConfigParameters* params);

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
    RWSplitSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);

    /**
     * @brief JSON diagnostics routine
     *
     * @return The JSON representation of this router instance
     */
    json_t* diagnostics() const;

    /**
     * @brief Get router capabilities
     */
    uint64_t getCapabilities();

    bool configure(mxs::ConfigParameters* params);

private:
    bool check_causal_reads(SERVER* server) const;
    void set_warnings(json_t* json) const;

    SERVICE*                              m_service;    /**< Service where the router belongs*/
    mxs::WorkerGlobal<RWSConfig>          m_config;
    Stats                                 m_stats;
    mxs::WorkerGlobal<TargetSessionStats> m_server_stats;
    std::atomic<gtid>                     m_last_gtid {{0, 0, 0}};
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

/**
 * Get total slave count and connected slave count
 *
 * @param backends List of backend servers
 * @param master   Current master
 *
 * @return Total number of slaves and number of slaves we are connected to
 */
std::pair<int, int> get_slave_counts(mxs::PRWBackends& backends, mxs::RWBackend* master);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void close_all_connections(mxs::PRWBackends& backends);

/**
 * Check if replication lag is below acceptable levels
 */
static inline bool rpl_lag_is_ok(mxs::RWBackend* backend, int max_rlag)
{
    return max_rlag == mxs::Target::RLAG_UNDEFINED || backend->target()->replication_lag() <= max_rlag;
}
