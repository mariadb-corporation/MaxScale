/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "readwritesplit.hh"
#include "trx.hh"

#include <chrono>
#include <string>
#include <deque>

#include <maxscale/buffer.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/queryclassifier.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>

#define TARGET_IS_MASTER(t)       mariadb::QueryClassifier::target_is_master(t)
#define TARGET_IS_SLAVE(t)        mariadb::QueryClassifier::target_is_slave(t)
#define TARGET_IS_NAMED_SERVER(t) mariadb::QueryClassifier::target_is_named_server(t)
#define TARGET_IS_ALL(t)          mariadb::QueryClassifier::target_is_all(t)
#define TARGET_IS_RLAG_MAX(t)     mariadb::QueryClassifier::target_is_rlag_max(t)
#define TARGET_IS_LAST_USED(t)    mariadb::QueryClassifier::target_is_last_used(t)

struct ExecInfo
{
    // The latest server this was executed on, used to figure out where COM_STMT_FETCH needs to be sent.
    mxs::RWBackend* target = nullptr;
};

/** Map of COM_STMT_EXECUTE targets by internal ID */
typedef std::unordered_map<uint32_t, ExecInfo> ExecMap;

/**
 * The client session of a RWSplit instance
 */
class RWSplitSession final : public mxs::RouterSession
                           , private mariadb::QueryClassifier::Handler
{
    RWSplitSession(const RWSplitSession&) = delete;
    RWSplitSession& operator=(const RWSplitSession&) = delete;

public:
    enum
    {
        TARGET_UNDEFINED    = mariadb::QueryClassifier::TARGET_UNDEFINED,
        TARGET_MASTER       = mariadb::QueryClassifier::TARGET_MASTER,
        TARGET_SLAVE        = mariadb::QueryClassifier::TARGET_SLAVE,
        TARGET_NAMED_SERVER = mariadb::QueryClassifier::TARGET_NAMED_SERVER,
        TARGET_ALL          = mariadb::QueryClassifier::TARGET_ALL,
        TARGET_RLAG_MAX     = mariadb::QueryClassifier::TARGET_RLAG_MAX,
        TARGET_LAST_USED    = mariadb::QueryClassifier::TARGET_LAST_USED,
    };

    enum wait_gtid_state
    {
        NONE,
        WAITING_FOR_HEADER,
        RETRYING_ON_MASTER,
        UPDATING_PACKETS
    };

    ~RWSplitSession();

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     *
     * @return New router session
     */
    static RWSplitSession* create(RWSplit* router, MXS_SESSION* session, const Endpoints& endpoints);

    bool routeQuery(GWBUF* pPacket) override;

    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, GWBUF* pMessage,
                     mxs::Endpoint* pProblem, const mxs::Reply& pReply) override;

    mariadb::QueryClassifier& qc()
    {
        return m_qc;
    }

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::SRWBackends backends);

    struct RoutingPlan
    {
        enum class Type
        {
            NORMAL,
            OTRX_START,
            OTRX_END
        };

        route_target_t  route_target = TARGET_UNDEFINED;
        mxs::RWBackend* target = nullptr;
        Type            type = Type::NORMAL;
    };

    bool open_connections();

    bool route_session_write(GWBUF* querybuf, uint8_t command, uint32_t type);
    void continue_large_session_write(GWBUF* querybuf, uint32_t type);
    bool write_session_command(mxs::RWBackend* backend, mxs::Buffer buffer, uint8_t cmd);
    bool route_stmt(mxs::Buffer&& querybuf, const RoutingPlan& res);
    bool route_single_stmt(mxs::Buffer&& buffer, const RoutingPlan& res);
    bool route_stored_query();
    void close_stale_connections();
    void execute_queued_commands(mxs::RWBackend* backend);

    int64_t         get_current_rank();
    mxs::RWBackend* get_hinted_backend(const char* name);
    mxs::RWBackend* get_slave_backend(int max_rlag);
    mxs::RWBackend* get_master_backend();
    mxs::RWBackend* get_last_used_backend();
    mxs::RWBackend* get_target_backend(backend_type_t btype, const char* name, int max_rlag);
    mxs::RWBackend* get_root_master();

    // The main target selection function
    mxs::RWBackend* get_target(const mxs::Buffer& buffer, route_target_t route_target);

    RoutingPlan resolve_route(const mxs::Buffer& buffer, const mariadb::QueryClassifier::RouteInfo&);

    bool            handle_target_is_all(mxs::Buffer&& buffer, const RoutingPlan& res);
    mxs::RWBackend* handle_hinted_target(const GWBUF* querybuf, route_target_t route_target);
    mxs::RWBackend* handle_slave_is_target(uint8_t cmd, uint32_t stmt_id);
    mxs::RWBackend* handle_master_is_target();
    bool            handle_got_target(mxs::Buffer&& buffer, mxs::RWBackend* target, bool store);
    bool            handle_routing_failure(mxs::Buffer&& buffer, const RoutingPlan& res);
    bool            prepare_target(mxs::RWBackend* target, route_target_t route_target);
    bool            prepare_connection(mxs::RWBackend* target);
    bool            create_one_connection_for_sescmd();
    void            retry_query(GWBUF* querybuf, int delay = 1);

    // Transaction state helpers
    bool trx_is_starting() const;
    bool trx_is_read_only() const;
    bool trx_is_open() const;
    bool trx_is_ending() const;

    bool can_continue_using_master(const mxs::RWBackend* master);
    bool is_valid_for_master(const mxs::RWBackend* master);
    bool should_replace_master(mxs::RWBackend* target);
    void replace_master(mxs::RWBackend* target);
    void discard_master_connection(const std::string& error);
    bool should_migrate_trx(mxs::RWBackend* target);
    bool start_trx_migration(mxs::RWBackend* target, GWBUF* querybuf);
    void log_master_routing_failure(bool found,
                                    mxs::RWBackend* old_master,
                                    mxs::RWBackend* curr_master);

    void send_readonly_error();
    bool query_not_supported(GWBUF* querybuf);

    GWBUF* handle_causal_read_reply(GWBUF* writebuf, const mxs::Reply& reply, mxs::RWBackend* backend);
    bool   finish_causal_read();
    GWBUF* add_prefix_wait_gtid(GWBUF* origin);
    void   correct_packet_sequence(GWBUF* buffer);
    GWBUF* discard_master_wait_gtid_result(GWBUF* buffer);
    void   send_sync_query(mxs::RWBackend* target);

    int get_max_replication_lag();

    bool reuse_prepared_stmt(const mxs::Buffer& buffer);

    bool retry_master_query(mxs::RWBackend* backend);
    bool handle_error_new_connection(mxs::RWBackend* backend, GWBUF* errmsg,
                                     mxs::RWBackend::close_type failure_type);
    void manage_transactions(mxs::RWBackend* backend, GWBUF* writebuf, const mxs::Reply& reply);
    void finish_transaction(mxs::RWBackend* backend);

    void trx_replay_next_stmt();

    // Do we have at least one open slave connection
    bool have_connected_slaves() const;

    /**
     * Start the replaying of the latest transaction
     *
     * @return True if the session can continue. False if the session must be closed.
     */
    bool start_trx_replay();

    /**
     * See if the transaction could be done on a slave
     *
     * @param route_target Target where the query is routed
     *
     * @return True if the query can be attempted on a slave
     */
    bool should_try_trx_on_slave(route_target_t route_target) const;

    /**
     * Track optimistic transaction status
     *
     * Tracks the progress of the optimistic transaction and starts the rollback
     * procedure if the transaction turns out to be one that modifies data.
     *
     * @param buffer Current query
     * @param res    Routing result
     */
    void track_optimistic_trx(mxs::Buffer* buffer, const RoutingPlan& res);

private:
    // QueryClassifier::Handler
    bool lock_to_master() override;
    bool is_locked_to_master() const override;
    bool supports_hint(Hint::Type hint_type) const override;
    bool handle_ignorable_error(mxs::RWBackend* backend, const mxs::Error& error);

    const mariadb::QueryClassifier::RouteInfo& route_info() const
    {
        return m_qc.current_route_info();
    }

    MYSQL_session* protocol_data() const
    {
        return static_cast<MYSQL_session*>(m_pSession->protocol_data());
    }

    inline bool can_retry_query() const
    {
        /** Individual queries can only be retried if we are not inside
         * a transaction. If a query in a transaction needs to be retried,
         * the whole transaction must be replayed before the retrying is done.
         *
         * @see handle_trx_replay
         */
        return m_config.delayed_retry
               && m_retry_duration < m_config.delayed_retry_timeout.count()
               && !trx_is_open();
    }

    // Whether a transaction replay can remain active
    inline bool can_continue_trx_replay() const
    {
        return m_state == TRX_REPLAY && m_retry_duration < m_config.delayed_retry_timeout.count();
    }

    inline bool can_recover_servers() const
    {
        const auto& config = *m_pSession->service->config();
        bool rval = false;

        if (protocol_data()->history.empty())
        {
            // Servers can always be recovered if no session commands have been executed
            rval = true;
        }
        else if (!config.disable_sescmd_history)
        {
            // Recovery is also possible if history pruning is enabled or the history limit hasn't exceeded
            // the limit
            if (config.prune_sescmd_history || !protocol_data()->history_pruned)
            {
                rval = true;
            }
        }

        return rval;
    }

    inline bool can_recover_master() const
    {
        return m_config.master_reconnection && can_recover_servers();
    }

    inline bool have_open_connections() const
    {
        return std::any_of(m_raw_backends.begin(), m_raw_backends.end(), [](mxs::RWBackend* b) {
                               return b->in_use();
                           });
    }

    inline bool is_last_backend(mxs::RWBackend* backend)
    {
        return std::none_of(m_raw_backends.begin(), m_raw_backends.end(), [&](mxs::RWBackend* b) {
                                return b->in_use() && b != backend;
                            });
    }

    std::string get_verbose_status()
    {
        std::string status;

        for (const auto& a : m_backends)
        {
            status += "\n";
            status += a->get_verbose_status();
        }

        return status;
    }

    inline bool can_route_query(const mxs::Buffer& buffer, const RoutingPlan& res) const
    {
        bool can_route = false;

        if (m_query_queue.empty() || gwbuf_is_replayed(buffer.get()))
        {
            if (m_expected_responses == 0
                || route_info().load_data_state() != mariadb::QueryClassifier::LOAD_DATA_INACTIVE
                || route_info().large_query())
            {
                // Not currently doing anything or we're processing a multi-packet query
                can_route = true;
            }
            else if (route_info().stmt_id() != MARIADB_PS_DIRECT_EXEC_ID
                     && res.route_target == TARGET_MASTER
                     && m_prev_plan.route_target == TARGET_MASTER
                     && res.type == m_prev_plan.type
                     && res.target == m_prev_plan.target
                     && res.target == m_current_master
                    // If transaction replay is configured, we cannot stream the queries as we need to know
                    // what they returned in case the transaction is replayed.
                     && (!m_config.transaction_replay || !trx_is_open()))
            {
                mxb_assert(res.type == RoutingPlan::Type::NORMAL);
                mxb_assert(m_current_master->is_waiting_result());
                can_route = true;
            }
        }

        return can_route;
    }

    inline mariadb::QueryClassifier::current_target_t get_current_target() const
    {
        mariadb::QueryClassifier::current_target_t current_target;

        if (m_target_node == NULL)
        {
            current_target = mariadb::QueryClassifier::CURRENT_TARGET_UNDEFINED;
        }
        else if (m_target_node == m_current_master)
        {
            current_target = mariadb::QueryClassifier::CURRENT_TARGET_MASTER;
        }
        else
        {
            current_target = mariadb::QueryClassifier::CURRENT_TARGET_SLAVE;
        }

        return current_target;
    }

    void update_statistics(const RoutingPlan& res)
    {
        if (res.route_target == TARGET_MASTER)
        {
            mxb::atomic::add(&m_router->stats().n_master, 1, mxb::atomic::RELAXED);
        }
        else if (res.route_target == TARGET_SLAVE)
        {
            mxb::atomic::add(&m_router->stats().n_slave, 1, mxb::atomic::RELAXED);
        }

        const uint32_t read_only_types = QUERY_TYPE_READ | QUERY_TYPE_LOCAL_READ
            | QUERY_TYPE_USERVAR_READ | QUERY_TYPE_SYSVAR_READ | QUERY_TYPE_GSYSVAR_READ;

        if ((route_info().type_mask() & ~read_only_types) && !trx_is_read_only()
            && res.target->is_master())
        {
            m_server_stats[res.target->target()].inc_write();
        }
        else
        {
            m_server_stats[res.target->target()].inc_read();
        }

        if (trx_is_ending())
        {
            mxb::atomic::add(route_info().is_trx_still_read_only() ?
                             &m_router->stats().n_ro_trx :
                             &m_router->stats().n_rw_trx,
                             1,
                             mxb::atomic::RELAXED);
        }
    }

    void replace_binary_ps_id(GWBUF* buffer, uint32_t id)
    {
        uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
        gw_mysql_set_byte4(ptr, id);
    }

    uint32_t extract_binary_ps_id(GWBUF* buffer)
    {
        uint8_t* ptr = GWBUF_DATA(buffer) + MYSQL_PS_ID_OFFSET;
        return gw_mysql_get_byte4(ptr);
    }

    bool in_optimistic_trx() const
    {
        return m_state == OTRX_STARTING || m_state == OTRX_ACTIVE || m_state == OTRX_ROLLBACK;
    }

    enum State
    {
        ROUTING,        // Normal routing
        TRX_REPLAY,     // Replaying a transaction
        OTRX_STARTING,  // Transaction starting on slave
        OTRX_ACTIVE,    // Transaction open on a slave server
        OTRX_ROLLBACK   // Transaction being rolled back on the slave server
    };

    State m_state = ROUTING;

    mxs::SRWBackends  m_backends;               /**< Mem. management, not for use outside RWSplitSession */
    mxs::PRWBackends  m_raw_backends;           /**< Backend pointers for use in interfaces . */
    mxs::RWBackend*   m_current_master;         /**< Current master server */
    mxs::RWBackend*   m_target_node;            /**< The currently locked target node */
    RoutingPlan       m_prev_plan;              /**< The previous routing plan */
    RWSConfig::Values m_config;                 /**< Configuration for this session */

    int  m_expected_responses;          /**< Number of expected responses to the current query */
    bool m_locked_to_master {false};    /**< Whether session is permanently locked to the master */

    std::deque<mxs::Buffer> m_query_queue;  /**< Queued commands waiting to be executed */
    RWSplit*                m_router;       /**< The router instance */
    mxs::RWBackend*         m_sescmd_replier {nullptr};

    ExecMap m_exec_map;     // Information map of COM_STMT_EXECUTE execution

    RWSplit::gtid   m_gtid_pos {0, 0, 0};   /**< Gtid position for causal read */
    wait_gtid_state m_wait_gtid;            /**< State of MASTER_GTID_WAIT reply */
    uint32_t        m_next_seq;             /**< Next packet's sequence number */

    mariadb::QueryClassifier m_qc;      /**< The query classifier. */

    int64_t     m_retry_duration;       /**< Total time spent retrying queries */
    mxs::Buffer m_current_query;        /**< Current query being executed */
    Trx         m_trx;                  /**< Current transaction */
    bool        m_can_replay_trx;       /**< Whether the transaction can be replayed */
    Trx         m_replayed_trx;         /**< The transaction we are replaying */
    mxs::Buffer m_interrupted_query;    /**< Query that was interrupted mid-transaction. */
    Trx         m_orig_trx;             /**< The backup of the transaction we're replaying */
    mxs::Buffer m_orig_stmt;            /**< The backup of the statement that was interrupted */
    int64_t     m_num_trx_replays = 0;  /**< How many times trx replay has been attempted */

    TargetSessionStats& m_server_stats;     /**< The server stats local to this thread, cached in the
                                             * session object. This avoids the lookup involved in getting
                                             * the worker-local value from the worker's container. */

    // Map of COM_STMT_PREPARE responses mapped to their SQL
    std::unordered_map<std::string, mxs::Buffer> m_ps_cache;
};

/**
 * @brief Get the internal ID for the given binary prepared statement
 *
 * @param rses   Router client session
 * @param buffer Buffer containing a binary protocol statement other than COM_STMT_PREPARE
 *
 * @return The internal ID of the prepared statement that the buffer contents refer to
 */
uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer);

static inline const char* route_target_to_string(route_target_t target)
{
    if (TARGET_IS_MASTER(target))
    {
        return "TARGET_MASTER";
    }
    else if (TARGET_IS_SLAVE(target))
    {
        return "TARGET_SLAVE";
    }
    else if (TARGET_IS_NAMED_SERVER(target))
    {
        return "TARGET_NAMED_SERVER";
    }
    else if (TARGET_IS_ALL(target))
    {
        return "TARGET_ALL";
    }
    else if (TARGET_IS_RLAG_MAX(target))
    {
        return "TARGET_RLAG_MAX";
    }
    else if (TARGET_IS_LAST_USED(target))
    {
        return "TARGET_LAST_USED";
    }
    else
    {
        mxb_assert(!true);
        return "Unknown target value";
    }
}
