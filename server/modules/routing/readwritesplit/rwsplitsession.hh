/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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
#include <maxscale/queryclassifier.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>

#define TARGET_IS_MASTER(t)       maxscale::QueryClassifier::target_is_master(t)
#define TARGET_IS_SLAVE(t)        maxscale::QueryClassifier::target_is_slave(t)
#define TARGET_IS_NAMED_SERVER(t) maxscale::QueryClassifier::target_is_named_server(t)
#define TARGET_IS_ALL(t)          maxscale::QueryClassifier::target_is_all(t)
#define TARGET_IS_RLAG_MAX(t)     maxscale::QueryClassifier::target_is_rlag_max(t)
#define TARGET_IS_LAST_USED(t)    maxscale::QueryClassifier::target_is_last_used(t)

// External ID to internal ID
typedef std::map<uint32_t, uint32_t> ClientHandleMap;

// Records of the backend who responded to the session commands and whether it succeeded
typedef std::map<uint64_t, std::pair<mxs::RWBackend*, mxs::Error>> ResponseMap;

/** List of slave responses that arrived before the master and whether they succeeded */
typedef std::unordered_map<mxs::RWBackend*, mxs::Error> SlaveResponseList;

struct ExecInfo
{
    // The latest server this was executed on, used to figure out where COM_STMT_FETCH needs to be sent.
    mxs::RWBackend* target = nullptr;
    // Parameter type metadata
    std::vector<uint8_t> metadata;
    // The set of servers to which the metadata has been sent
    std::unordered_set<mxs::RWBackend*> metadata_sent;
};

/** Map of COM_STMT_EXECUTE targets by internal ID */
typedef std::unordered_map<uint32_t, ExecInfo> ExecMap;

/**
 * The client session of a RWSplit instance
 */
class RWSplitSession final : public mxs::RouterSession
                           , private mxs::QueryClassifier::Handler
{
    RWSplitSession(const RWSplitSession&) = delete;
    RWSplitSession& operator=(const RWSplitSession&) = delete;

public:
    enum
    {
        TARGET_UNDEFINED    = maxscale::QueryClassifier::TARGET_UNDEFINED,
        TARGET_MASTER       = maxscale::QueryClassifier::TARGET_MASTER,
        TARGET_SLAVE        = maxscale::QueryClassifier::TARGET_SLAVE,
        TARGET_NAMED_SERVER = maxscale::QueryClassifier::TARGET_NAMED_SERVER,
        TARGET_ALL          = maxscale::QueryClassifier::TARGET_ALL,
        TARGET_RLAG_MAX     = maxscale::QueryClassifier::TARGET_RLAG_MAX,
        TARGET_LAST_USED    = maxscale::QueryClassifier::TARGET_LAST_USED,
    };

    enum otrx_state
    {
        OTRX_INACTIVE,  // No open transactions
        OTRX_STARTING,  // Transaction starting on slave
        OTRX_ACTIVE,    // Transaction open on a slave server
        OTRX_ROLLBACK   // Transaction being rolled back on the slave server
    };

    enum wait_gtid_state
    {
        NONE,
        WAITING_FOR_HEADER,
        RETRYING_ON_MASTER,
        UPDATING_PACKETS
    };

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     *
     * @return New router session
     */
    static RWSplitSession* create(RWSplit* router, MXS_SESSION* session, const Endpoints& endpoints);

    /**
     * Called when a client session has been closed.
     */
    void close();

    /**
     * Called when a packet being is routed to the backend. The router should
     * forward the packet to the appropriate server(s).
     *
     * @param pPacket A client packet.
     */
    int32_t routeQuery(GWBUF* pPacket);

    /**
     * Called when a packet is routed to the client. The router should
     * forward the packet to the client using `RouterSession::clientReply`.
     *
     * @param pPacket A client packet.
     * @param down    The route the reply took
     * @param reply   The reply information
     */
    void clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    bool handleError(mxs::ErrorType type, GWBUF* pMessage, mxs::Endpoint* pProblem, const mxs::Reply& pReply);

    mxs::QueryClassifier& qc()
    {
        return m_qc;
    }

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session, mxs::SRWBackends backends);

    bool open_connections();
    void process_sescmd_response(mxs::RWBackend* backend, GWBUF** ppPacket, const mxs::Reply& reply);
    void compress_history(mxs::SSessionCommand& sescmd);

    mxs::SSessionCommand create_sescmd(GWBUF* buffer);

    // Discards master response history to position @c pos
    void discard_responses(uint64_t pos);

    // Discards history that happened before a connection reset
    void discard_old_history(uint64_t lowest_pos);

    bool route_session_write(GWBUF* querybuf, uint8_t command, uint32_t type);
    void continue_large_session_write(GWBUF* querybuf, uint32_t type);
    bool route_stmt(mxs::Buffer&& querybuf);
    bool route_single_stmt(mxs::Buffer&& buffer);
    bool route_stored_query();
    void close_stale_connections();
    void execute_queued_commands(mxs::RWBackend* backend, bool processed_sescmd);

    int64_t         get_current_rank();
    mxs::RWBackend* get_hinted_backend(const char* name);
    mxs::RWBackend* get_slave_backend(int max_rlag);
    mxs::RWBackend* get_master_backend();
    mxs::RWBackend* get_last_used_backend();
    mxs::RWBackend* get_target_backend(backend_type_t btype, const char* name, int max_rlag);
    mxs::RWBackend* get_root_master();

    // The main target selection function
    mxs::RWBackend* get_target(GWBUF* querybuf, route_target_t route_target);

    bool            handle_target_is_all(mxs::Buffer&& buffer);
    mxs::RWBackend* handle_hinted_target(GWBUF* querybuf, route_target_t route_target);
    mxs::RWBackend* handle_slave_is_target(uint8_t cmd, uint32_t stmt_id);
    mxs::RWBackend* handle_master_is_target();
    bool            handle_got_target(mxs::Buffer&& buffer, mxs::RWBackend* target, bool store);
    bool            handle_routing_failure(mxs::Buffer&& buffer, route_target_t route_target);
    bool            prepare_target(mxs::RWBackend* target, route_target_t route_target);
    bool            prepare_connection(mxs::RWBackend* target);
    bool            create_one_connection_for_sescmd();
    void            retry_query(GWBUF* querybuf, int delay = 1);
    void            process_stmt_execute(mxs::Buffer* buffer, uint32_t id, mxs::RWBackend* target);

    // Transaction state helpers
    bool trx_is_starting() const;
    bool trx_is_read_only() const;
    bool trx_is_open() const;
    bool trx_is_ending() const;

    bool can_continue_using_master(const mxs::RWBackend* master);
    bool is_valid_for_master(const mxs::RWBackend* master);
    bool should_replace_master(mxs::RWBackend* target);
    void replace_master(mxs::RWBackend* target);
    bool should_migrate_trx(mxs::RWBackend* target);
    bool start_trx_migration(mxs::RWBackend* target, GWBUF* querybuf);
    void log_master_routing_failure(bool found,
                                    mxs::RWBackend* old_master,
                                    mxs::RWBackend* curr_master);

    void send_readonly_error();
    bool query_not_supported(GWBUF* querybuf);

    GWBUF* handle_causal_read_reply(GWBUF* writebuf, const mxs::Reply& reply, mxs::RWBackend* backend);
    bool   finish_causal_read();
    GWBUF* add_prefix_wait_gtid(uint64_t version, GWBUF* origin);
    void   correct_packet_sequence(GWBUF* buffer);
    GWBUF* discard_master_wait_gtid_result(GWBUF* buffer);

    int get_max_replication_lag();

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
     * @param buffer     Current query
     *
     * @return Whether the current statement should be stored for the duration of the query
     */
    bool track_optimistic_trx(mxs::Buffer* buffer);

private:
    // QueryClassifier::Handler
    bool lock_to_master();
    bool is_locked_to_master() const;
    bool supports_hint(HINT_TYPE hint_type) const;
    bool handle_ignorable_error(mxs::RWBackend* backend, const mxs::Error& error);

    // Struct for holding the results of session commands that are executed inside transactions
    struct SescmdResp
    {
        SescmdResp(const mxs::Buffer& stmt, const mxs::Buffer& res, const mxs::Reply& rep)
            : statement(stmt)
            , result(res)
            , reply(rep)
        {
        }

        mxs::Buffer statement;
        mxs::Buffer result;
        mxs::Reply  reply;
    };

    inline bool can_retry_query() const
    {
        /** Individual queries can only be retried if we are not inside
         * a transaction. If a query in a transaction needs to be retried,
         * the whole transaction must be replayed before the retrying is done.
         *
         * @see handle_trx_replay
         */
        return m_config.delayed_retry
               && m_retry_duration < m_config.delayed_retry_timeout
               && !trx_is_open();
    }

    // Whether a transaction replay can remain active
    inline bool can_continue_trx_replay() const
    {
        return m_is_replay_active && m_retry_duration < m_config.delayed_retry_timeout;
    }

    inline bool can_recover_servers() const
    {
        return !m_config.disable_sescmd_history || m_recv_sescmd == 0;
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

    inline bool should_route_sescmd_to_master() const
    {
        return trx_is_open() && m_config.transaction_replay
               && TARGET_IS_ALL(m_qc.current_route_info().target());
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

    inline bool is_large_query(GWBUF* buf)
    {
        uint32_t buflen = gwbuf_length(buf);

        // The buffer should contain at most (2^24 - 1) + 4 bytes ...
        mxb_assert(buflen <= MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN);
        // ... and the payload should be buflen - 4 bytes
        mxb_assert(MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buf)) == buflen - MYSQL_HEADER_LEN);

        return buflen == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;
    }

    inline bool can_route_queries() const
    {
        return m_expected_responses == 0
               || m_qc.load_data_state() == mxs::QueryClassifier::LOAD_DATA_ACTIVE
               || m_qc.large_query();
    }

    inline mxs::QueryClassifier::current_target_t get_current_target() const
    {
        mxs::QueryClassifier::current_target_t current_target;

        if (m_target_node == NULL)
        {
            current_target = mxs::QueryClassifier::CURRENT_TARGET_UNDEFINED;
        }
        else if (m_target_node == m_current_master)
        {
            current_target = mxs::QueryClassifier::CURRENT_TARGET_MASTER;
        }
        else
        {
            current_target = mxs::QueryClassifier::CURRENT_TARGET_SLAVE;
        }

        return current_target;
    }

    void update_trx_statistics()
    {
        if (trx_is_ending())
        {
            mxb::atomic::add(m_qc.is_trx_still_read_only() ?
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

    mxs::SRWBackends m_backends;                /**< Mem. management, not for use outside RWSplitSession */
    mxs::PRWBackends m_raw_backends;            /**< Backend pointers for use in interfaces . */
    mxs::RWBackend*  m_current_master;          /**< Current master server */
    mxs::RWBackend*  m_target_node;             /**< The currently locked target node */
    mxs::RWBackend*  m_prev_target;             /**< The previous target where a query was sent */
    RWSConfig        m_config;                  /**< Configuration for this session */
    MXS_SESSION*     m_session;                 /**< The client session */
    uint64_t         m_sescmd_count;            /**< Number of executed session commands (starts from 1) */
    int              m_expected_responses;      /**< Number of expected responses to the current query */
    bool             m_locked_to_master {false};/**< Whether session is permanently locked to the master */

    maxbase::TimePoint m_last_keepalive_check;      /**< When the last ping was done */

    std::deque<mxs::Buffer> m_query_queue;      /**< Queued commands waiting to be executed */
    RWSplit*                m_router;           /**< The router instance */
    mxs::SessionCommandList m_sescmd_list;      /**< List of executed session commands */
    ResponseMap             m_sescmd_responses; /**< Response to each session command */
    mxs::RWBackend*         m_sescmd_replier {nullptr};
    SlaveResponseList       m_slave_responses;  /**< Slaves that replied before the master */
    uint64_t                m_sent_sescmd;      /**< ID of the last sent session command*/
    uint64_t                m_recv_sescmd;      /**< ID of the most recently completed session
                                                 * command */
    uint64_t m_sescmd_prune_pos {0};

    ExecMap m_exec_map;     // Information map of COM_STMT_EXECUTE execution

    RWSplit::gtid        m_gtid_pos {0, 0, 0};  /**< Gtid position for causal read */
    wait_gtid_state      m_wait_gtid;           /**< State of MASTER_GTID_WAIT reply */
    uint32_t             m_next_seq;            /**< Next packet's sequence number */
    mxs::QueryClassifier m_qc;                  /**< The query classifier. */
    uint64_t             m_retry_duration;      /**< Total time spent retrying queries */
    mxs::Buffer          m_current_query;       /**< Current query being executed */
    Trx                  m_trx;                 /**< Current transaction */
    bool                 m_is_replay_active;    /**< Whether we are actively replaying a
                                                 * transaction */
    bool        m_can_replay_trx;               /**< Whether the transaction can be replayed */
    Trx         m_replayed_trx;                 /**< The transaction we are replaying */
    mxs::Buffer m_interrupted_query;            /**< Query that was interrupted mid-transaction. */
    Trx         m_orig_trx;                     /**< The backup of the transaction we're replaying */
    mxs::Buffer m_orig_stmt;                    /**< The backup of the statement that was interrupted */
    int64_t     m_num_trx_replays = 0;          /**< How many times trx replay has been attempted */

    std::vector<SescmdResp> m_trx_sescmd;   /**< Session commands executed during the transaction */

    otrx_state m_otrx_state = OTRX_INACTIVE;    /**< Optimistic trx state*/

    TargetSessionStats& m_server_stats;     /**< The server stats local to this thread, cached in the
                                             * session object. This avoids the lookup involved in getting
                                             * the worker-local value from the worker's container. */
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
