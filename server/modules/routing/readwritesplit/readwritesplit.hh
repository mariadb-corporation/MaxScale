/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
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

#include <maxbase/shared_mutex.hh>
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

enum CausalReads
{
    NONE,   // No causal reads, default
    LOCAL,  // Causal reads are done on a session level with MASTER_GTID_WAIT
    GLOBAL, // Causal reads are done globally with MASTER_GTID_WAIT
    FAST    // Causal reads use GTID position to pick an up-to-date server
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

static cfg::ParamEnum<CausalReads> s_causal_reads(
    &s_spec, "causal_reads", "Causal reads mode",
{
    // Legacy values for causal_reads
    {CausalReads::NONE, "false"},
    {CausalReads::NONE, "off"},
    {CausalReads::NONE, "0"},
    {CausalReads::LOCAL, "true"},
    {CausalReads::LOCAL, "on"},
    {CausalReads::LOCAL, "1"},

    {CausalReads::NONE, "none"},
    {CausalReads::LOCAL, "local"},
    {CausalReads::GLOBAL, "global"},
    {CausalReads::FAST, "fast"},
}, CausalReads::NONE, cfg::Param::AT_RUNTIME);

static cfg::ParamSeconds s_max_slave_replication_lag(
    &s_spec, "max_slave_replication_lag", "Maximum allowed slave replication lag",
    cfg::INTERPRET_AS_SECONDS, std::chrono::seconds(0),
    cfg::Param::AT_RUNTIME);

static cfg::ParamString s_max_slave_connections(
    &s_spec, "max_slave_connections", "Maximum number of slave connections",
    std::to_string(SLAVE_MAX), cfg::ParamString::Quotes::IGNORED, cfg::Param::AT_RUNTIME);

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
using std::chrono::seconds;

struct RWSConfig
{
    RWSConfig() = default;

    static std::pair<bool, RWSConfig> create(const mxs::ConfigParameters& params);

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

    CausalReads causal_reads;
    std::string causal_reads_timeout;   /**< Timeout, second parameter of function master_wait_gtid */

    bool     master_reconnection;       /**< Allow changes in master server */
    bool     delayed_retry;             /**< Delay routing if no target found */
    uint64_t delayed_retry_timeout;     /**< How long to delay until an error is returned */
    bool     transaction_replay;        /**< Replay failed transactions */
    size_t   trx_max_size;              /**< Max transaction size for replaying */
    int64_t  trx_max_attempts;          /**< Maximum number of transaction replay attempts */
    bool     trx_retry_on_deadlock;     /**< Replay the transaction if it ends up in a deadlock */
    bool     optimistic_trx;            /**< Enable optimistic transactions */
    bool     lazy_connect;              /**< Create connections only when needed */

private:
    RWSConfig(const mxs::ConfigParameters& params);
    static BackendSelectFunction get_backend_select_function(select_criteria_t sc);
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
    gtid                                  m_last_gtid {0, 0, 0};
    mutable mxb::shared_mutex             m_last_gtid_lock;
};
