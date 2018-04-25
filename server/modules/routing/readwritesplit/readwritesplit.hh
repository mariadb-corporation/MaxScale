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
 * @file Readwritesplit common header
 */

#define MXS_MODULE_NAME "readwritesplit"

#include <maxscale/cppdefs.hh>

#include <tr1/unordered_set>
#include <tr1/unordered_map>
#include <map>
#include <string>

#include <maxscale/dcb.h>
#include <maxscale/hashtable.h>
#include <maxscale/log_manager.h>
#include <maxscale/queryclassifier.hh>
#include <maxscale/router.hh>
#include <maxscale/service.h>
#include <maxscale/session_command.hh>
#include <maxscale/protocol/mysql.h>

#include "rwbackend.hh"

enum backend_type_t
{
    BE_UNDEFINED = -1,
    BE_MASTER,
    BE_JOINED    = BE_MASTER,
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
    UNDEFINED_CRITERIA = 0,
    LEAST_GLOBAL_CONNECTIONS,   /**< all connections established by MaxScale */
    LEAST_ROUTER_CONNECTIONS,   /**< connections established by this router */
    LEAST_BEHIND_MASTER,
    LEAST_CURRENT_OPERATIONS,
    DEFAULT_CRITERIA   = LEAST_CURRENT_OPERATIONS,
    LAST_CRITERIA               /**< not used except for an index */
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
    {"all",    TYPE_ALL},
    {"master", TYPE_MASTER},
    {NULL}
};

static const MXS_ENUM_VALUE slave_selection_criteria_values[] =
{
    {"LEAST_GLOBAL_CONNECTIONS", LEAST_GLOBAL_CONNECTIONS},
    {"LEAST_ROUTER_CONNECTIONS", LEAST_ROUTER_CONNECTIONS},
    {"LEAST_BEHIND_MASTER",      LEAST_BEHIND_MASTER},
    {"LEAST_CURRENT_OPERATIONS", LEAST_CURRENT_OPERATIONS},
    {NULL}
};

static const MXS_ENUM_VALUE master_failure_mode_values[] =
{
    {"fail_instantly", RW_FAIL_INSTANTLY},
    {"fail_on_write",  RW_FAIL_ON_WRITE},
    {"error_on_write", RW_ERROR_ON_WRITE},
    {NULL}
};

#define BREF_IS_NOT_USED(s)         ((s)->bref_state & ~BREF_IN_USE)
#define BREF_IS_IN_USE(s)           ((s)->bref_state & BREF_IN_USE)
#define BREF_IS_WAITING_RESULT(s)   ((s)->bref_num_result_wait > 0)
#define BREF_IS_QUERY_ACTIVE(s)     ((s)->bref_state & BREF_QUERY_ACTIVE)
#define BREF_IS_CLOSED(s)           ((s)->bref_state & BREF_CLOSED)
#define BREF_HAS_FAILED(s)          ((s)->bref_state & BREF_FATAL_FAILURE)

/** default values for rwsplit configuration parameters */
#define CONFIG_MAX_SLAVE_CONN 1
#define CONFIG_MAX_SLAVE_RLAG -1 /**< not used */
#define CONFIG_SQL_VARIABLES_IN TYPE_ALL

#define GET_SELECT_CRITERIA(s)                                                                  \
        (strncmp(s,"LEAST_GLOBAL_CONNECTIONS", strlen("LEAST_GLOBAL_CONNECTIONS")) == 0 ?       \
        LEAST_GLOBAL_CONNECTIONS : (                                                            \
        strncmp(s,"LEAST_BEHIND_MASTER", strlen("LEAST_BEHIND_MASTER")) == 0 ?                  \
        LEAST_BEHIND_MASTER : (                                                                 \
        strncmp(s,"LEAST_ROUTER_CONNECTIONS", strlen("LEAST_ROUTER_CONNECTIONS")) == 0 ?        \
        LEAST_ROUTER_CONNECTIONS : (                                                            \
        strncmp(s,"LEAST_CURRENT_OPERATIONS", strlen("LEAST_CURRENT_OPERATIONS")) == 0 ?        \
        LEAST_CURRENT_OPERATIONS : UNDEFINED_CRITERIA))))

#define BACKEND_TYPE(b) (SERVER_IS_MASTER((b)->backend_server) ? BE_MASTER :    \
        (SERVER_IS_SLAVE((b)->backend_server) ? BE_SLAVE :  BE_UNDEFINED));

#define MARIADB_WAIT_GTID_FUNC "MASTER_GTID_WAIT"
#define MYSQL_WAIT_GTID_FUNC   "WAIT_FOR_EXECUTED_GTID_SET"
static const char gtid_wait_stmt[] =
    "SET @maxscale_secret_variable=(SELECT CASE WHEN %s('%s', %s) = 0 "
    "THEN 1 ELSE (SELECT 1 FROM INFORMATION_SCHEMA.ENGINES) END);";

struct Config
{
    Config(MXS_CONFIG_PARAMETER* params):
        slave_selection_criteria(
            (select_criteria_t)config_get_enum(
                params, "slave_selection_criteria", slave_selection_criteria_values)),
        use_sql_variables_in(
            (mxs_target_t)config_get_enum(
                params, "use_sql_variables_in", use_sql_variables_in_values)),
        master_failure_mode(
            (enum failure_mode)config_get_enum(
                params, "master_failure_mode", master_failure_mode_values)),
        max_sescmd_history(config_get_integer(params, "max_sescmd_history")),
        disable_sescmd_history(config_get_bool(params, "disable_sescmd_history")),
        master_accept_reads(config_get_bool(params, "master_accept_reads")),
        strict_multi_stmt(config_get_bool(params, "strict_multi_stmt")),
        strict_sp_calls(config_get_bool(params, "strict_sp_calls")),
        retry_failed_reads(config_get_bool(params, "retry_failed_reads")),
        connection_keepalive(config_get_integer(params, "connection_keepalive")),
        max_slave_replication_lag(config_get_integer(params, "max_slave_replication_lag")),
        rw_max_slave_conn_percent(0),
        max_slave_connections(0),
        enable_causal_read(config_get_bool(params, "enable_causal_read")),
        causal_read_timeout(config_get_string(params, "causal_read_timeout")),
        master_reconnection(config_get_bool(params, "master_reconnection")),
        delayed_retry(config_get_bool(params, "delayed_retry")),
        delayed_retry_timeout(config_get_integer(params, "delayed_retry_timeout")),
        transaction_replay(config_get_bool(params, "transaction_replay")),
        trx_max_size(config_get_size(params, "transaction_replay_max_size"))
    {
        if (enable_causal_read)
        {
            retry_failed_reads = true;
        }
    }

    select_criteria_t slave_selection_criteria;  /**< The slave selection criteria */
    mxs_target_t      use_sql_variables_in;      /**< Whether to send user variables to
                                                  * master or all nodes */
    failure_mode      master_failure_mode;       /**< Master server failure handling mode */
    uint64_t          max_sescmd_history;        /**< Maximum amount of session commands to store */
    bool              disable_sescmd_history;    /**< Disable session command history */
    bool              master_accept_reads;       /**< Use master for reads */
    bool              strict_multi_stmt;         /**< Force non-multistatement queries to be routed to
                                                  * the master after a multistatement query. */
    bool              strict_sp_calls;           /**< Lock session to master after an SP call */
    bool              retry_failed_reads;        /**< Retry failed reads on other servers */
    int               connection_keepalive;      /**< Send pings to servers that have been idle
                                                  * for too long */
    int               max_slave_replication_lag; /**< Maximum replication lag */
    int               rw_max_slave_conn_percent; /**< Maximum percentage of slaves to use for
                                                  * each connection*/
    int               max_slave_connections;     /**< Maximum number of slaves for each connection*/
    bool              enable_causal_read;        /**< Enable causual read */
    std::string       causal_read_timeout;       /**< Timeout, second parameter of function master_wait_gtid */
    bool              master_reconnection;       /**< Allow changes in master server */
    bool              delayed_retry;             /**< Delay routing if no target found */
    uint64_t          delayed_retry_timeout;     /**< How long to delay until an error is returned */
    bool              transaction_replay;        /**< Replay failed transactions */
    size_t            trx_max_size;               /**< Max transaction size for replaying */
};

/**
 * The statistics for this router instance
 */
struct Stats
{
public:

    Stats():
        n_sessions(0),
        n_queries(0),
        n_master(0),
        n_slave(0),
        n_all(0)
    {
    }

    uint64_t n_sessions;        /**< Number sessions created */
    uint64_t n_queries;         /**< Number of queries forwarded */
    uint64_t n_master;          /**< Number of stmts sent to master */
    uint64_t n_slave;           /**< Number of stmts sent to slave */
    uint64_t n_all;             /**< Number of stmts sent to all */
};

class RWSplitSession;

/**
 * The per instance data for the router.
 */
class RWSplit: public mxs::Router<RWSplit, RWSplitSession>
{
    RWSplit(const RWSplit&);
    RWSplit& operator=(const RWSplit&);

public:
    RWSplit(SERVICE* service, const Config& config);
    ~RWSplit();

    SERVICE* service() const;
    const Config&  config() const;
    Stats&   stats();
    const Stats&   stats() const;
    int max_slave_count() const;
    bool have_enough_servers() const;
    bool select_connect_backend_servers(MXS_SESSION *session,
                                        mxs::SRWBackendList& backends,
                                        mxs::SRWBackend& current_master,
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
    static RWSplit* create(SERVICE* pService, char** pzOptions);

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

private:
    SERVICE* m_service; /**< Service where the router belongs*/
    Config   m_config;
    Stats    m_stats;
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
        ss_dassert(false);
        return "UNDEFINED_MODE";
    }
}

void closed_session_reply(GWBUF *querybuf);
bool send_readonly_error(DCB *dcb);

mxs::SRWBackend get_root_master(const mxs::SRWBackendList& backends);

/**
 * Get total slave count and connected slave count
 *
 * @param backends List of backend servers
 * @param master   Current master
 *
 * @return Total number of slaves and number of slaves we are connected to
 */
std::pair<int, int> get_slave_counts(mxs::SRWBackendList& backends, mxs::SRWBackend& master);

/*
 * The following are implemented in rwsplit_tmp_table_multi.c
 */
void close_all_connections(mxs::SRWBackendList& backends);
