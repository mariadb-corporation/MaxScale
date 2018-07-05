#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "readwritesplit.hh"
#include "rwbackend.hh"
#include "trx.hh"

#include <string>

#include <maxscale/buffer.hh>
#include <maxscale/modutil.h>
#include <maxscale/queryclassifier.hh>

#define TARGET_IS_MASTER(t)         maxscale::QueryClassifier::target_is_master(t)
#define TARGET_IS_SLAVE(t)          maxscale::QueryClassifier::target_is_slave(t)
#define TARGET_IS_NAMED_SERVER(t)   maxscale::QueryClassifier::target_is_named_server(t)
#define TARGET_IS_ALL(t)            maxscale::QueryClassifier::target_is_all(t)
#define TARGET_IS_RLAG_MAX(t)       maxscale::QueryClassifier::target_is_rlag_max(t)

typedef std::map<uint32_t, uint32_t> ClientHandleMap;  /** External ID to internal ID */

typedef std::tr1::unordered_set<std::string> TableSet;
typedef std::map<uint64_t, uint8_t>          ResponseMap;

/** List of slave responses that arrived before the master */
typedef std::list< std::pair<mxs::SRWBackend, uint8_t> > SlaveResponseList;

/** Map of COM_STMT_EXECUTE targets by internal ID */
typedef std::tr1::unordered_map<uint32_t, mxs::SRWBackend> ExecMap;

/**
 * The client session of a RWSplit instance
 */
class RWSplitSession: public mxs::RouterSession,
    private mxs::QueryClassifier::Handler
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
    };

    enum otrx_state
    {
        OTRX_INACTIVE, // No open transactions
        OTRX_STARTING, // Transaction starting on slave
        OTRX_ACTIVE,   // Transaction open on a slave server
        OTRX_ROLLBACK  // Transaction being rolled back on the slave server
    };

    enum wait_gtid_state
    {
        NONE,
        WAITING_FOR_HEADER,
        UPDATING_PACKETS
    };

    virtual ~RWSplitSession()
    {
    }

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     *
     * @return New router session
     */
    static RWSplitSession* create(RWSplit* router, MXS_SESSION* session);

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
     * forward the packet to the client using `MXS_SESSION_ROUTE_REPLY`.
     *
     * @param pPacket  A client packet.
     * @param pBackend The backend the packet is coming from.
     */
    void clientReply(GWBUF* pPacket, DCB* pBackend);

    /**
     *
     * @param pMessage  The error message.
     * @param pProblem  The DCB on which the error occurred.
     * @param action    The context.
     * @param pSuccess  On output, if false, the session will be terminated.
     */
    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);

    mxs::QueryClassifier& qc()
    {
        return m_qc;
    }

    // TODO: Make member variables private
    mxs::SRWBackendList     m_backends; /**< List of backend servers */
    mxs::SRWBackend         m_current_master; /**< Current master server */
    mxs::SRWBackend         m_target_node; /**< The currently locked target node */
    mxs::SRWBackend         m_prev_target; /**< The previous target where a query was sent */
    SConfig                 m_config; /**< Configuration for this session */
    int                     m_nbackends; /**< Number of backend servers (obsolete) */
    DCB*                    m_client; /**< The client DCB */
    uint64_t                m_sescmd_count; /**< Number of executed session commands */
    int                     m_expected_responses; /**< Number of expected responses to the current query */
    GWBUF*                  m_query_queue; /**< Queued commands waiting to be executed */
    RWSplit*                m_router; /**< The router instance */
    mxs::SessionCommandList m_sescmd_list; /**< List of executed session commands */
    ResponseMap             m_sescmd_responses; /**< Response to each session command */
    SlaveResponseList       m_slave_responses; /**< Slaves that replied before the master */
    uint64_t                m_sent_sescmd; /**< ID of the last sent session command*/
    uint64_t                m_recv_sescmd; /**< ID of the most recently completed session command */
    ClientHandleMap         m_ps_handles;  /**< Client PS handle to internal ID mapping */
    ExecMap                 m_exec_map; /**< Map of COM_STMT_EXECUTE statement IDs to Backends */
    std::string             m_gtid_pos; /**< Gtid position for causal read */
    wait_gtid_state         m_wait_gtid; /**< State of MASTER_GTID_WAIT reply */
    uint32_t                m_next_seq; /**< Next packet's sequence number */
    mxs::QueryClassifier    m_qc; /**< The query classifier. */
    uint64_t                m_retry_duration; /**< Total time spent retrying queries */
    mxs::Buffer             m_current_query; /**< Current query being executed */
    Trx                     m_trx; /**< Current transaction */
    bool                    m_is_replay_active; /**< Whether we are actively replaying a transaction */
    bool                    m_can_replay_trx; /**< Whether the transaction can be replayed */
    Trx                     m_replayed_trx; /**< The transaction we are replaying */
    mxs::Buffer             m_interrupted_query; /**< Query that was interrupted mid-transaction. */
    otrx_state              m_otrx_state = OTRX_INACTIVE; /**< Optimistic trx state*/

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session,
                   const mxs::SRWBackendList& backends, const mxs::SRWBackend& master);

    void process_sescmd_response(mxs::SRWBackend& backend, GWBUF** ppPacket);
    void compress_history(mxs::SSessionCommand& sescmd);

    bool route_session_write(GWBUF *querybuf, uint8_t command, uint32_t type);
    void continue_large_session_write(GWBUF *querybuf, uint32_t type);
    bool route_single_stmt(GWBUF *querybuf);
    bool route_stored_query();

    mxs::SRWBackend get_hinted_backend(char *name);
    mxs::SRWBackend get_slave_backend(int max_rlag);
    mxs::SRWBackend get_master_backend();
    mxs::SRWBackend get_target_backend(backend_type_t btype, char *name, int max_rlag);

    bool handle_target_is_all(route_target_t route_target, GWBUF *querybuf,
                              int packet_type, uint32_t qtype);
    mxs::SRWBackend handle_hinted_target(GWBUF *querybuf, route_target_t route_target);
    mxs::SRWBackend handle_slave_is_target(uint8_t cmd, uint32_t stmt_id);
    bool handle_master_is_target(mxs::SRWBackend* dest);
    bool handle_got_target(GWBUF* querybuf, mxs::SRWBackend& target, bool store);
    void handle_connection_keepalive(mxs::SRWBackend& target);
    bool prepare_target(mxs::SRWBackend& target, route_target_t route_target);
    void retry_query(GWBUF* querybuf, int delay = 1);

    bool should_replace_master(mxs::SRWBackend& target);
    void replace_master(mxs::SRWBackend& target);
    void log_master_routing_failure(bool found, mxs::SRWBackend& old_master,
                                    mxs::SRWBackend& curr_master);

    GWBUF* handle_causal_read_reply(GWBUF *writebuf, mxs::SRWBackend& backend);
    GWBUF* add_prefix_wait_gtid(SERVER *server, GWBUF *origin);
    void correct_packet_sequence(GWBUF *buffer);
    GWBUF* discard_master_wait_gtid_result(GWBUF *buffer);

    int get_max_replication_lag();
    mxs::SRWBackend& get_backend_from_dcb(DCB *dcb);

    void handle_error_reply_client(DCB *backend_dcb, GWBUF *errmsg);
    bool handle_error_new_connection(DCB *backend_dcb, GWBUF *errmsg);

    void handle_trx_replay();

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
    bool track_optimistic_trx(GWBUF** buffer);

private:
    // QueryClassifier::Handler
    bool lock_to_master();
    bool is_locked_to_master() const;
    bool supports_hint(HINT_TYPE hint_type) const;

    inline bool can_retry_query() const
    {
        /** Individual queries can only be retried if we are not inside
         * a transaction. If a query in a transaction needs to be retried,
         * the whole transaction must be replayed before the retrying is done.
         *
         * @see handle_trx_replay
         */
        return m_config->delayed_retry &&
               m_retry_duration < m_config->delayed_retry_timeout &&
               !session_trx_is_active(m_client->session);
    }

    inline bool can_recover_servers() const
    {
        return !m_config->disable_sescmd_history || m_recv_sescmd == 0;
    }

    inline bool is_large_query(GWBUF* buf)
    {
        uint32_t buflen = gwbuf_length(buf);

        // The buffer should contain at most (2^24 - 1) + 4 bytes ...
        ss_dassert(buflen <= MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN);
        // ... and the payload should be buflen - 4 bytes
        ss_dassert(MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buf)) == buflen - MYSQL_HEADER_LEN);

        return buflen == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;
    }
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
