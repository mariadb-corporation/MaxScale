/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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
#include <maxscale/protocol/mysql.hh>
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

#define MARIADB_WAIT_GTID_FUNC "MASTER_GTID_WAIT"
#define MYSQL_WAIT_GTID_FUNC   "WAIT_FOR_EXECUTED_GTID_SET"
static const char gtid_wait_stmt[] =
    "SET @maxscale_secret_variable=(SELECT CASE WHEN %s('%s', %s) = 0 "
    "THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END);";

/** Function that returns a "score" for a server to enable comparison.
 *  Smaller numbers are better.
 */
using BackendSelectFunction = mxs::PRWBackends::iterator (*)(mxs::PRWBackends& sBackends);
BackendSelectFunction get_backend_select_function(select_criteria_t);

using std::chrono::seconds;

struct Config
{
    Config(MXS_CONFIG_PARAMETER* params)
        : slave_selection_criteria(
            (select_criteria_t)params->get_enum("slave_selection_criteria", slave_selection_criteria_values))
        , backend_select_fct(get_backend_select_function(slave_selection_criteria))
        , use_sql_variables_in(
            (mxs_target_t)params->get_enum("use_sql_variables_in", use_sql_variables_in_values))
        , master_failure_mode(
            (enum failure_mode)params->get_enum("master_failure_mode", master_failure_mode_values))
        , max_sescmd_history(params->get_integer("max_sescmd_history"))
        , prune_sescmd_history(params->get_bool("prune_sescmd_history"))
        , disable_sescmd_history(params->get_bool("disable_sescmd_history"))
        , master_accept_reads(params->get_bool("master_accept_reads"))
        , strict_multi_stmt(params->get_bool("strict_multi_stmt"))
        , strict_sp_calls(params->get_bool("strict_sp_calls"))
        , retry_failed_reads(params->get_bool("retry_failed_reads"))
        , connection_keepalive(params->get_duration<seconds>("connection_keepalive").count())
        , max_slave_replication_lag(params->get_duration<seconds>("max_slave_replication_lag").count())
        , rw_max_slave_conn_percent(0)
        , max_slave_connections(0)
        , causal_reads(params->get_bool("causal_reads"))
        , causal_reads_timeout(std::to_string(params->get_duration<seconds>("causal_reads_timeout").count()))
        , master_reconnection(params->get_bool("master_reconnection"))
        , delayed_retry(params->get_bool("delayed_retry"))
        , delayed_retry_timeout(params->get_duration<seconds>("delayed_retry_timeout").count())
        , transaction_replay(params->get_bool("transaction_replay"))
        , trx_max_size(params->get_size("transaction_replay_max_size"))
        , trx_max_attempts(params->get_integer("transaction_replay_attempts"))
        , trx_retry_on_deadlock(params->get_bool("transaction_replay_retry_on_deadlock"))
        , optimistic_trx(params->get_bool("optimistic_trx"))
        , lazy_connect(params->get_bool("lazy_connect"))
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
    int64_t     trx_max_attempts;       /**< Maximum number of transaction replay attempts */
    bool        trx_retry_on_deadlock;  /**< Replay the transaction if it ends up in a deadlock */
    bool        optimistic_trx;         /**< Enable optimistic transactions */
    bool        lazy_connect;           /**< Create connections only when needed */
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
    SrvStatMap&   local_server_stats();
    SrvStatMap    all_server_stats() const;

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

/**
 * See if the current master is still a valid TARGET_MASTER candidate
 *
 * The master is valid if it's in `Master, Running` state or if it is still in but in maintenance mode while a
 * transaction is open. If a transaction is open to a master in maintenance mode, the connection is closed on
 * the next COMMIT or ROLLBACK.
 *
 * @see RWSplitSession::close_stale_connections()
 */
inline bool can_continue_using_master(const mxs::RWBackend* current_master)
{
    constexpr uint64_t bits = SERVER_MASTER | SERVER_RUNNING | SERVER_MAINT;
    auto server = current_master->server();

    return server->is_master() || (current_master->in_use()
                                   && (server->status & bits) == bits
                                   && session_trx_is_active(current_master->dcb()->session));
}

mxs::RWBackend* get_root_master(const mxs::PRWBackends& backends, mxs::RWBackend* current_master);

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
 * Extract the SQL state from an error packet.
 *
 * @param pBuffer  Pointer to an error packet.
 * @param ppState  On return will point to the state in @c pBuffer.
 * @param pnState  On return the pointed to value will be 6.
 */
inline void extract_error_state(uint8_t* pBuffer, uint8_t** ppState, uint16_t* pnState)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state. In this context
    // the marker and the state itself are combined.
    *ppState = pBuffer + MYSQL_HEADER_LEN + 1 + 2;
    *pnState = 6;
}

/**
 * Extract the message from an error packet.
 *
 * @param pBuffer    Pointer to an error packet.
 * @param ppMessage  On return will point to the start of the message in @c pBuffer.
 * @param pnMessage  On return the pointed to value will be the length of the message.
 */
inline void extract_error_message(uint8_t* pBuffer, uint8_t** ppMessage, uint16_t* pnMessage)
{
    mxb_assert(MYSQL_IS_ERROR_PACKET(pBuffer));

    int packet_len = MYSQL_HEADER_LEN + MYSQL_GET_PAYLOAD_LEN(pBuffer);

    // The payload starts with a one byte command followed by a two byte error code,
    // followed by a 1 byte sql state marker and 5 bytes of sql state, followed by
    // a message until the end of the packet.
    *ppMessage = pBuffer + MYSQL_HEADER_LEN + 1 + 2 + 1 + 5;
    *pnMessage = packet_len - MYSQL_HEADER_LEN - 1 - 2 - 1 - 5;
}

/**
 * Utility function for extracting error messages from buffers
 *
 * @param buffer Buffer containing an error
 *
 * @return String representation of the error
 */
std::string extract_error(GWBUF* buffer);

/**
 * Check if replication lag is below acceptable levels
 */
static inline bool rpl_lag_is_ok(mxs::RWBackend* backend, int max_rlag)
{
    auto rlag = backend->server()->rlag;
    return max_rlag == SERVER::RLAG_UNDEFINED || (rlag != SERVER::RLAG_UNDEFINED && rlag <= max_rlag);
}
