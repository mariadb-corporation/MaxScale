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

#include "readwritesplit.hh"
#include "rwsplit_ps.hh"
#include "rwbackend.hh"
#include "routeinfo.hh"

#include <string>

#include <maxscale/buffer.hh>
#include <maxscale/modutil.h>
#include <maxscale/queryclassifier.hh>

typedef enum
{
    EXPECTING_NOTHING = 0,
    EXPECTING_WAIT_GTID_RESULT,
    EXPECTING_REAL_RESULT
} wait_gtid_state_t;

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
    Config                  m_config; /**< copied config info from router instance */
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
    wait_gtid_state_t       m_wait_gtid_state; /**< Determine boundary of generated query result */
    uint32_t                m_next_seq; /**< Next packet's sequence number */
    mxs::QueryClassifier    m_qc; /**< The query classifier. */
    uint64_t                m_retry_duration; /**< Total time spent retrying queries */
    mxs::Buffer             m_current_query; /**< Current query being executed */

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session,
                   const mxs::SRWBackendList& backends, const mxs::SRWBackend& master);

    void process_sescmd_response(mxs::SRWBackend& backend, GWBUF** ppPacket);
    void purge_history(mxs::SSessionCommand& sescmd);

    bool route_session_write(GWBUF *querybuf, uint8_t command, uint32_t type);
    bool route_single_stmt(GWBUF *querybuf, const RouteInfo& info);
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
    void retry_query(GWBUF* querybuf);

    bool should_replace_master(mxs::SRWBackend& target);
    void replace_master(mxs::SRWBackend& target);
    void log_master_routing_failure(bool found, mxs::SRWBackend& old_master,
                                    mxs::SRWBackend& curr_master);

    bool handle_causal_read_reply(GWBUF *writebuf, mxs::SRWBackend& backend);
    GWBUF* add_prefix_wait_gtid(SERVER *server, GWBUF *origin);
    void correct_packet_sequence(GWBUF *buffer);
    GWBUF* discard_master_wait_gtid_result(GWBUF *buffer);

    int get_max_replication_lag();
    mxs::SRWBackend& get_backend_from_dcb(DCB *dcb);

    void handle_error_reply_client(DCB *backend_dcb, GWBUF *errmsg);
    bool handle_error_new_connection(DCB *backend_dcb, GWBUF *errmsg);

    /**
     * Check if the session is locked to the master
     *
     * @return Whether the session is locked to the master
     */
    inline bool locked_to_master() const
    {
        return m_qc.large_query() || (m_current_master && m_target_node == m_current_master);
    }

private:
    // QueryClassifier::Handler
    bool lock_to_master();
    bool is_locked_to_master() const;
    bool supports_hint(HINT_TYPE hint_type) const;

    inline bool can_retry_query() const
    {
        return m_config.delayed_retry &&
               m_retry_duration < m_config.delayed_retry_timeout &&
               !session_trx_is_active(m_client->session);
    }

    /**
     * Set the current query
     *
     * @param query The current query
     */
    inline void set_query(GWBUF* query)
    {
        ss_dassert(!m_current_query.get());
        m_current_query.reset(gwbuf_clone(query));
    }

    /**
     * Release current query
     *
     * @return The current query
     */
    inline GWBUF* release_query()
    {
        return m_current_query.release();
    }

    /**
     * Reset current query
     */
    inline void reset_query()
    {
        m_current_query.reset();
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
