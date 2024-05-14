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

/**
 * @file Readwritesplit common header
 */

#define MXB_MODULE_NAME "readwritesplit"

#include <maxscale/ccdefs.hh>

#include <maxbase/shared_mutex.hh>
#include <maxbase/small_vector.hh>
#include <maxscale/protocol/mariadb/module_names.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>
#include <maxscale/queryclassifier.hh>
#include <maxscale/router.hh>
#include <maxscale/session_stats.hh>
#include <maxscale/workerlocal.hh>
#include <maxscale/modulecmd.hh>

using namespace std::literals::chrono_literals;

constexpr int SLAVE_MAX = 255;

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

enum class CausalReads
{
    NONE,           // No causal reads, default
    LOCAL,          // Causal reads are done on a session level with MASTER_GTID_WAIT
    GLOBAL,         // Causal reads are done globally with MASTER_GTID_WAIT
    FAST,           // Causal reads use GTID position to pick an up-to-date server
    FAST_GLOBAL,    // Same as FAST except the global GTID is used
    UNIVERSAL,      // Causal reads verifies the GTID before starting the read
    FAST_UNIVERSAL  // Causal reads verifies the GTID and then behaves like FAST
};

enum class TrxChecksum
{
    FULL,           // Checksum all responses
    RESULT_ONLY,    // Checksum resultsets and errors, ignore OK packets
    NO_INSERT_ID,   // Same as RESULT_ONLY but also ignores results from queries that use LAST_INSERT_ID()
};

// The server selection requires some work memory for the candidate selection. The most common case is that
// there is only a handful of candidates to pick from and allocating space for them on the heap is somewhat
// wasteful. Currently 6 values are stored internally which should cover the most common cases.
using Candidates = mxb::small_vector<mxs::RWBackend*, 6>;

/** Function that returns a "score" for a server to enable comparison.
 *  Smaller numbers are better.
 */
using BackendSelectFunction = mxs::RWBackend * (*)(const Candidates& sBackends);
using std::chrono::seconds;

// The exception class thrown by readwritesplit. This should never propagate outside of readwritesplit code.
class RWSException : public std::runtime_error
{
public:
    template<class ... Args>
    RWSException(std::string_view str, Args ... args)
        : std::runtime_error(mxb::cat(str, args ...))
    {
    }

    template<class ... Args>
    RWSException(GWBUF&& buffer, Args ... args)
        : std::runtime_error(mxb::cat(args ...))
        , m_buffer(std::move(buffer))
    {
        mxb_assert(!m_buffer.empty());
    }

    const GWBUF& buffer() const
    {
        return m_buffer;
    }

private:
    GWBUF m_buffer;
};

struct RWSConfig : public mxs::config::Configuration
{
    RWSConfig(SERVICE* service);

    struct Values
    {
        using seconds = std::chrono::seconds;

        select_criteria_t     slave_selection_criteria; /**< The slave selection criteria */
        BackendSelectFunction backend_select_fct;

        mxs_target_t use_sql_variables_in;      /**< Whether to send user variables to master or all nodes */
        failure_mode master_failure_mode;       /**< Master server failure handling mode */
        seconds      max_replication_lag;       /**< Maximum replication lag */
        bool         master_accept_reads;       /**< Use master for reads */
        bool         strict_multi_stmt;         /**< Force non-multistatement queries to be routed to the
                                                 * master after a multistatement query. */
        bool        strict_sp_calls;            /**< Lock session to master after an SP call */
        bool        strict_tmp_tables;          /**< Prevent reconnections if temporary tables exist */
        bool        retry_failed_reads;         /**< Retry failed reads on other servers */
        int64_t     max_slave_connections = 0;  /**< Maximum number of slaves for each connection*/
        int64_t     slave_connections;          /**< Minimum number of slaves for each connection*/
        bool        master_reconnection;        /**< Allow changes in master server */
        bool        optimistic_trx;             /**< Enable optimistic transactions */
        bool        lazy_connect;               /**< Create connections only when needed */
        CausalReads causal_reads;
        seconds     causal_reads_timeout;   /**< Timeout, second parameter of function master_wait_gtid */
        bool        delayed_retry;          /**< Delay routing if no target found */
        seconds     delayed_retry_timeout;  /**< How long to delay until an error is returned */

        bool        transaction_replay;     /**< Replay failed transactions */
        int64_t     trx_max_size;           /**< Max transaction size for replaying */
        seconds     trx_timeout;            /**< How long can the transaction be replayed for */
        int64_t     trx_max_attempts;       /**< Maximum number of transaction replay attempts */
        bool        trx_retry_on_deadlock;  /**< Replay the transaction if it ends up in a deadlock */
        bool        trx_retry_on_mismatch;  /**< Replay the transaction on checksum mismatch */
        bool        trx_retry_safe_commit;  /**< Prevent replay of COMMITs */
        TrxChecksum trx_checksum;           /**< The type of checksum to calculate */

        bool reuse_ps;      /**< Reuse prepared statements */
    };

    const mxs::WorkerGlobal<Values>& values() const
    {
        return m_values;
    }

private:
    Values                    m_v;  // Master copy of the values
    mxs::WorkerGlobal<Values> m_values;
    SERVICE*                  m_service;

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

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
    uint64_t n_trx_too_big = 0;     /**< Number of times that transaction_replay_max_size was exceeded */
    uint64_t n_ro_trx = 0;          /**< Read-only transaction count */
    uint64_t n_rw_trx = 0;          /**< Read-write transaction count */
    uint64_t n_ps_reused = 0;       /**< Number of prepared statements that were reused */
};

using maxscale::SessionStats;
using maxscale::TargetSessionStats;

class RWSplitSession;

/**
 * The per instance data for the router.
 */
class RWSplit : public mxs::Router
{
    RWSplit(const RWSplit&);
    RWSplit& operator=(const RWSplit&);

public:
    struct gtid
    {
        uint32_t domain {0};
        uint32_t server_id {0};
        uint64_t sequence {0};

        static gtid from_string(std::string_view str);
        void        parse(std::string_view str);
        std::string to_string() const;
        bool        empty() const;
    };

    RWSplit(SERVICE* service);
    ~RWSplit();

    SERVICE*                 service() const;
    Stats&                   stats();
    const Stats&             stats() const;
    TargetSessionStats&      local_server_stats();
    TargetSessionStats       all_server_stats() const;
    std::string              last_gtid() const;
    std::map<uint32_t, gtid> last_gtid_map() const;
    void                     set_last_gtid(std::string_view str);
    static bool              reset_last_gtid(const MODULECMD_ARG* argv, json_t** output);

    const mxs::WorkerGlobal<RWSConfig::Values>& config() const;

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
     * @return New router instance or nullptr on error
     */
    static RWSplit* create(SERVICE* pService);

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
     * @return New router session or nullptr on error
     */
    std::shared_ptr<mxs::RouterSession>
    newSession(MXS_SESSION* pSession, const Endpoints& endpoints) override;

    /**
     * @brief JSON diagnostics routine
     *
     * @return The JSON representation of this router instance
     */
    json_t* diagnostics() const override;

    /**
     * @brief Get router capabilities
     */
    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
    }

private:
    bool check_causal_reads(SERVER* server) const;

    SERVICE*                             m_service;         /**< Service where the router belongs*/
    RWSConfig                            m_config;
    Stats                                m_stats;
    mxs::WorkerLocal<TargetSessionStats> m_server_stats;
    std::map<uint32_t, gtid>             m_last_gtid;
    mutable mxb::shared_mutex            m_last_gtid_lock;
};
