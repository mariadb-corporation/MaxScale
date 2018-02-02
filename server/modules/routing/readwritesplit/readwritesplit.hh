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
#include <maxscale/router.h>
#include <maxscale/service.h>
#include <maxscale/backend.hh>
#include <maxscale/session_command.hh>
#include <maxscale/protocol/mysql.h>

enum backend_type_t
{
    BE_UNDEFINED = -1,
    BE_MASTER,
    BE_JOINED    = BE_MASTER,
    BE_SLAVE,
    BE_COUNT
};

enum route_target_t
{
    TARGET_UNDEFINED    = 0x00,
    TARGET_MASTER       = 0x01,
    TARGET_SLAVE        = 0x02,
    TARGET_NAMED_SERVER = 0x04,
    TARGET_ALL          = 0x08,
    TARGET_RLAG_MAX     = 0x10
};

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

/** States of a LOAD DATA LOCAL INFILE */
enum ld_state
{
    LOAD_DATA_INACTIVE,         /**< Not active */
    LOAD_DATA_START,            /**< Current query starts a load */
    LOAD_DATA_ACTIVE,           /**< Load is active */
    LOAD_DATA_END               /**< Current query contains an empty packet that ends the load */
};

#define TARGET_IS_MASTER(t)       (t & TARGET_MASTER)
#define TARGET_IS_SLAVE(t)        (t & TARGET_SLAVE)
#define TARGET_IS_NAMED_SERVER(t) (t & TARGET_NAMED_SERVER)
#define TARGET_IS_ALL(t)          (t & TARGET_ALL)
#define TARGET_IS_RLAG_MAX(t)     (t & TARGET_RLAG_MAX)
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
        causal_read_timeout(config_get_string(params, "causal_read_timeout"))
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
    std::string       causal_read_timeout;       /**< Timetout, second parameter of function master_wait_gtid */

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

/**
 * The per instance data for the router.
 */
class RWSplit
{
    RWSplit(const RWSplit&);
    RWSplit& operator=(const RWSplit&);

public:
    RWSplit(SERVICE* service, const Config& config);
    ~RWSplit();

    SERVICE* service() const;
    const Config&  config() const;
    Stats&   stats();
    int max_slave_count() const;
    bool have_enough_servers() const;

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
