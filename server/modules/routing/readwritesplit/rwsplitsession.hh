/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "readwritesplit.hh"
#include "trx.hh"

#include <unordered_map>
#include <optional>

#include <maxbase/format.hh>

#define TARGET_IS_MASTER(t)       mariadb::QueryClassifier::target_is_master(t)
#define TARGET_IS_SLAVE(t)        mariadb::QueryClassifier::target_is_slave(t)
#define TARGET_IS_NAMED_SERVER(t) mariadb::QueryClassifier::target_is_named_server(t)
#define TARGET_IS_ALL(t)          mariadb::QueryClassifier::target_is_all(t)
#define TARGET_IS_RLAG_MAX(t)     mariadb::QueryClassifier::target_is_rlag_max(t)
#define TARGET_IS_LAST_USED(t)    mariadb::QueryClassifier::target_is_last_used(t)

struct ExecInfo
{
    ExecInfo(uint32_t stmt_id, mxs::RWBackend* t = nullptr)
        : id(stmt_id)
        , target(t)
    {
    }

    bool operator==(const ExecInfo& other) const
    {
        return id == other.id;
    }

    uint32_t id;
    // The latest server this was executed on, used to figure out where COM_STMT_FETCH needs to be sent.
    mxs::RWBackend* target = nullptr;
};

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
        UPDATING_PACKETS,
        READING_GTID,
        GTID_READ_DONE
    };

    ~RWSplitSession();

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     * @param backends The backend servers
     *
     * @return New router session
     */
    RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::RWBackends backends);

    bool routeQuery(GWBUF&& packet) override;

    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

    bool handleError(mxs::ErrorType type, const std::string& message,
                     mxs::Endpoint* pProblem, const mxs::Reply& reply) override final;

    void endpointConnReleased(mxs::Endpoint* down) override;

    mariadb::QueryClassifier& qc()
    {
        return m_qc;
    }

private:
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

    void open_connections();

    bool route_query(GWBUF&& buffer);
    void route_session_write(GWBUF&& querybuf);
    void continue_large_session_write(GWBUF&& querybuf);
    bool write_session_command(mxs::RWBackend* backend, GWBUF&& buffer, uint8_t cmd);
    void route_stmt(GWBUF&& querybuf, const RoutingPlan& plan);
    void route_single_stmt(GWBUF&& buffer, const RoutingPlan& plan);
    void client_reply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool route_stored_query();
    void close_stale_connections();

    int64_t         get_current_rank();
    mxs::RWBackend* get_hinted_backend(const char* name);
    mxs::RWBackend* get_slave_backend(int max_rlag);
    mxs::RWBackend* get_master_backend();
    mxs::RWBackend* get_last_used_backend();
    mxs::RWBackend* get_ps_continuation_backend();
    mxs::RWBackend* get_root_master();
    bool            is_gtid_synced(mxs::RWBackend* backend);
    bool            need_slaves();

    // The main target selection function
    mxs::RWBackend* get_target(const GWBUF& buffer, route_target_t route_target);

    RoutingPlan resolve_route(const GWBUF& buffer, const mariadb::QueryClassifier::RouteInfo&);

    void            handle_target_is_all(GWBUF&& buffer);
    mxs::RWBackend* handle_hinted_target(const GWBUF& querybuf, route_target_t route_target);
    void            handle_got_target(GWBUF&& buffer, mxs::RWBackend* target, route_target_t route_target);
    void            observe_trx(mxs::RWBackend* target);
    void            observe_ps_command(GWBUF& buffer, mxs::RWBackend* target, uint8_t cmd);
    bool            prepare_connection(mxs::RWBackend* target);
    void            create_one_connection_for_sescmd();
    void            retry_query(GWBUF&& querybuf, int delay = 1);

    // Returns a human-readable error if the query could not be retried
    std::optional<std::string> handle_routing_failure(GWBUF&& buffer, const RoutingPlan& plan);

    std::string get_master_routing_failure(bool found,
                                           mxs::RWBackend* old_master,
                                           mxs::RWBackend* curr_master);

    // Transaction state helpers
    bool trx_is_starting() const
    {
        return route_info().trx().is_trx_starting();
    }

    bool trx_is_read_only() const
    {
        return route_info().trx().is_trx_read_only();
    }

    bool trx_is_open() const
    {
        return route_info().trx().is_trx_active();
    }

    bool trx_is_ending() const
    {
        return route_info().trx().is_trx_ending();
    }

    bool is_valid_for_master(const mxs::RWBackend* master);
    bool should_replace_master(mxs::RWBackend* target);
    void replace_master(mxs::RWBackend* target);
    void discard_connection(mxs::RWBackend* target, const std::string& error);
    bool trx_target_still_valid() const;
    bool should_migrate_trx() const;
    void start_trx_migration(GWBUF&& querybuf);

    void send_readonly_error();
    bool query_not_supported(const GWBUF& querybuf);

    bool  handle_causal_read_reply(GWBUF& writebuf, const mxs::Reply& reply, mxs::RWBackend* backend);
    bool  should_do_causal_read() const;
    bool  continue_causal_read();
    GWBUF add_prefix_wait_gtid(const GWBUF& origin);
    void  correct_packet_sequence(GWBUF& buffer);
    void  discard_master_wait_gtid_result(GWBUF& buffer);
    bool  send_sync_query(mxs::RWBackend* target);

    bool                          need_gtid_probe(const RoutingPlan& plan) const;
    std::pair<GWBUF, RoutingPlan> start_gtid_probe();
    GWBUF                         reset_gtid_probe();
    void                          parse_gtid_result(GWBUF& buffer, const mxs::Reply& reply);

    int get_max_replication_lag();

    bool reuse_prepared_stmt(const GWBUF& buffer);


    void handle_error(mxs::ErrorType type, const std::string& message,
                      mxs::Endpoint* pProblem, const mxs::Reply& reply);
    void handle_master_error(const mxs::Reply& reply, const std::string& message, bool expected_response);
    void handle_slave_error(const char* name, bool expected_response);
    void manage_transactions(mxs::RWBackend* backend, const GWBUF& writebuf, const mxs::Reply& reply);
    void finish_transaction(mxs::RWBackend* backend);
    void ignore_response(mxs::RWBackend* backend, const mxs::Reply& reply);

    RWSException master_exception(const std::string& message, const mxs::Reply& rely) const;

    bool discard_partial_result(GWBUF& buffer, const mxs::Reply& reply);
    void checksum_mismatch();
    void trx_replay_next_stmt();

    void track_tx_isolation(const mxs::Reply& reply);

    // Do we have at least one open slave connection
    bool have_connected_slaves() const;

    /**
     * Start the replaying of the latest transaction
     */
    void start_trx_replay();

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
     * @param plan   The routing plan
     */
    void track_optimistic_trx(GWBUF& buffer, const RoutingPlan& plan);

private:
    // QueryClassifier::Handler
    bool lock_to_master() override;
    bool is_locked_to_master() const override;
    bool supports_hint(Hint::Type hint_type) const override;
    void handle_ignorable_error(mxs::RWBackend* backend, const mxs::Reply::Error& error);

    std::string get_delayed_retry_failure_reason() const;

    const mariadb::QueryClassifier::RouteInfo& route_info() const
    {
        return m_qc.current_route_info();
    }

    inline bool can_retry_query() const
    {
        /** Individual queries can only be retried if we are not inside
         * a transaction. If a query in a transaction needs to be retried,
         * the whole transaction must be replayed before the retrying is done.
         *
         * @see handle_trx_replay
         */
        return m_config->delayed_retry
               && m_retry_duration < m_config->delayed_retry_timeout.count()
               && !trx_is_open();
    }

    // Whether a transaction replay can remain active
    inline bool can_continue_trx_replay() const
    {
        return replaying_trx() && m_retry_duration < m_config->delayed_retry_timeout.count();
    }

    /**
     * Checks whether a new transaction replay can be started
     *
     * The replay is limited by transaction_replay_max_attempts and transaction_replay_timeout
     *
     * @throws RWSException
     */
    void check_trx_replay() const;

    inline bool can_recover_servers() const
    {
        return protocol_data().can_recover_state();
    }

    inline bool can_recover_master() const
    {
        return m_config->master_reconnection && can_recover_servers();
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

    inline bool need_master_for_sescmd()
    {
        return trx_is_open() && !trx_is_read_only() && !in_optimistic_trx()
               && (!m_current_master || !m_current_master->in_use());
    }

    inline bool include_in_checksum(const mxs::Reply& reply) const
    {
        switch (m_config->trx_checksum)
        {
        case TrxChecksum::FULL:
            return true;

        case TrxChecksum::RESULT_ONLY:
            return !reply.is_ok();

        case TrxChecksum::NO_INSERT_ID:
            // TODO: QUERY_TYPE_MASTER_READ implies LAST_INSERT_ID() but explicitly looking for it might
            // be better. However, this only requires the storage of the type bitmask instead of the whole
            // buffer which would be required for the function information.
            return !reply.is_ok()
                   && !mxs::Parser::type_mask_contains(m_qc.current_route_info().type_mask(),
                                                       mxs::sql::TYPE_MASTER_READ);
        }

        mxb_assert(!true);
        return true;
    }

    std::string get_verbose_status()
    {
        return mxb::transform_join(m_backends, [](const auto& a){
            return mxb::cat("{",
                            " Name: ", a.name(),
                            " Open: ", a.in_use() ? "Yes" : "No",
                            " Status: ", a.target()->status_string(),
                            " }");
        });
    }

    inline bool can_route_query(const GWBUF& buffer, const RoutingPlan& plan, bool trx_was_ending) const
    {
        bool can_route = false;

        if (m_expected_responses == 0
            || route_info().load_data_active()
            || route_info().multi_part_packet())
        {
            // Not currently doing anything or we're processing a multi-packet query
            can_route = true;
        }
        else if (route_info().stmt_id() != MARIADB_PS_DIRECT_EXEC_ID
                 && plan.route_target == TARGET_MASTER
                 && m_prev_plan.route_target == TARGET_MASTER
                 && plan.type == m_prev_plan.type
                 && plan.target == m_prev_plan.target
                 && plan.target == m_current_master
                // If transaction replay is configured, we cannot stream the queries as we need to know
                // what they returned in case the transaction is replayed.
                // TODO: This can be done as long as we track what requests are in-flight.
                 && (!m_config->transaction_replay || !trx_is_open())
                // Causal reads can't support multiple ongoing queries
                 && m_wait_gtid == NONE
                // Can't pipeline more queries until the current transaction ends
                 && !trx_was_ending)
        {
            mxb_assert(plan.type == RoutingPlan::Type::NORMAL);
            mxb_assert(m_current_master->is_waiting_result());
            can_route = true;
        }

        return can_route;
    }

    void update_statistics(const RoutingPlan& plan)
    {
        if (plan.route_target == TARGET_MASTER)
        {
            mxb::atomic::add(&m_router->stats().n_master, 1, mxb::atomic::RELAXED);
        }
        else if (plan.route_target == TARGET_SLAVE)
        {
            mxb::atomic::add(&m_router->stats().n_slave, 1, mxb::atomic::RELAXED);
        }
        else if (plan.route_target == TARGET_ALL)
        {
            mxb::atomic::add(&m_router->stats().n_all, 1, mxb::atomic::RELAXED);
        }

        mxb::atomic::add(&m_router->stats().n_queries, 1, mxb::atomic::RELAXED);

        if (trx_is_ending())
        {
            mxb::atomic::add(route_info().is_trx_still_read_only() ?
                             &m_router->stats().n_ro_trx :
                             &m_router->stats().n_rw_trx,
                             1,
                             mxb::atomic::RELAXED);
        }

        if (plan.target)
        {
            auto& stats = m_router->local_server_stats()[plan.target->target()];
            stats.inc_total();

            if (plan.route_target == TARGET_MASTER)
            {
                stats.inc_write();
            }
            else if (plan.route_target == TARGET_SLAVE)
            {
                stats.inc_read();
            }
        }
    }

    uint32_t extract_binary_ps_id(const GWBUF& buffer)
    {
        const uint8_t* ptr = buffer.data() + MYSQL_PS_ID_OFFSET;
        return mariadb::get_byte4(ptr);
    }

    bool in_optimistic_trx() const
    {
        return m_state == OTRX_STARTING || m_state == OTRX_ACTIVE || m_state == OTRX_ROLLBACK;
    }

    bool replaying_trx() const
    {
        return m_state == TRX_REPLAY || m_state == TRX_REPLAY_INTERRUPTED;
    }

    // How many seconds has the replay took so far. Only accurate during transaction replay.
    int64_t trx_replay_seconds() const
    {
        return std::chrono::duration_cast<std::chrono::seconds>(m_trx_replay_timer.split()).count();
    }

    enum State
    {
        ROUTING,                // Normal routing
        TRX_REPLAY,             // Replaying a transaction
        TRX_REPLAY_INTERRUPTED, // Replaying the interrupted query
        OTRX_STARTING,          // Transaction starting on slave
        OTRX_ACTIVE,            // Transaction open on a slave server
        OTRX_ROLLBACK           // Transaction being rolled back on the slave server
    };

    State m_state = ROUTING;

    mxs::RWBackends  m_backends;                /**< Mem. management, not for use outside RWSplitSession */
    mxs::PRWBackends m_raw_backends;            /**< Backend pointers for use in interfaces . */
    mxs::RWBackend*  m_current_master;          /**< Current master server */
    RoutingPlan      m_prev_plan;               /**< The previous routing plan */

    std::shared_ptr<const RWSConfig::Values> m_config;      /**< Configuration for this session */

    int  m_expected_responses;          /**< Number of expected responses to the current query */
    bool m_locked_to_master {false};    /**< Whether session is permanently locked to the master */
    bool m_check_stale {false};

    std::deque<GWBUF> m_query_queue;    /**< Queued commands waiting to be executed */
    RWSplit*          m_router;         /**< The router instance */
    mxs::RWBackend*   m_sescmd_replier {nullptr};

    std::vector<ExecInfo> m_exec_map;       // Information about COM_STMT_EXECUTE execution

    RWSplit::gtid   m_gtid_pos {0, 0, 0};   /**< Gtid position for causal read */
    wait_gtid_state m_wait_gtid;            /**< State of MASTER_GTID_WAIT reply */
    uint32_t        m_next_seq;             /**< Next packet's sequence number */

    mariadb::QueryClassifier m_qc;      /**< The query classifier. */

    int64_t m_retry_duration;       /**< Total time spent retrying queries */
    Stmt    m_current_query;        /**< Current query being executed */
    Trx     m_trx;                  /**< Current transaction */
    bool    m_can_replay_trx;       /**< Whether the transaction can be replayed */
    Trx     m_replayed_trx;         /**< The transaction we are replaying */
    Stmt    m_interrupted_query;    /**< Query that was interrupted mid-transaction. */
    Trx     m_orig_trx;             /**< The backup of the transaction we're replaying */
    Stmt    m_orig_stmt;            /**< The backup of the statement that was interrupted */
    int64_t m_num_trx_replays = 0;  /**< How many times trx replay has been attempted */

    // The SET TRANSACTION statement if one was sent. Reset after each transaction.
    GWBUF m_set_trx;

    mxb::StopWatch m_trx_replay_timer;      /**< When the last transaction replay started */

    mxb::StopWatch m_session_timer;

    // Number of queries being replayed. If this is larger than zero, the normal routeQuery method is "corked"
    // until the retried queries have been processed. In practice this should always be either 1 or 0.
    int m_pending_retries {0};

    // Map of COM_STMT_PREPARE responses mapped to their SQL
    std::unordered_map<std::string, GWBUF> m_ps_cache;
};

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
