/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "xrouter.hh"
#include <maxscale/backend.hh>
#include <maxscale/queryclassifier.hh>
#include <list>

using SBackends = std::vector<std::unique_ptr<mxs::Backend>>;

class XRouterSession : public mxs::RouterSession
{
public:
    XRouterSession(MXS_SESSION* session, XRouter& router, SBackends backends,
                   XRouter::Config::ValueRef config);
    bool routeQuery(GWBUF&& packet) override;
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    bool handleError(mxs::ErrorType type, const std::string& message,
                     mxs::Endpoint* pProblem, const mxs::Reply& reply) override;

protected:
    virtual void        preprocess(GWBUF&) = 0;
    virtual std::string main_sql() const = 0;
    virtual std::string secondary_sql() const = 0;
    virtual std::string lock_sql(std::string_view lock_id) const = 0;
    virtual std::string unlock_sql(std::string_view lock_id) const = 0;

private:
    enum class State
    {
        IDLE,           // The session is idle
        SOLO,           // Routing single-node command
        WAIT_SOLO,      // Waiting for single-node command to complete
        LOAD_DATA,      // Data streaming from client in progress
        LOCK_MAIN,      // Locking main node
        UNLOCK_MAIN,    // Unlocking main node
        MAIN,           // Routing multi-node command to main node
        WAIT_MAIN,      // Waiting for main node to complete the command
        WAIT_SECONDARY, // Waiting for secondary nodes to complete the command
    };

    static std::string_view state_to_str(State state);
    std::string_view        state_str() const;
    std::string             describe(const GWBUF& buffer);
    bool                    send_query(mxs::Backend* backend, std::string_view sql);

    // Functions called from clientReply in different states
    bool reply_state_wait_solo(mxs::Backend* backend, GWBUF&& packet,
                               const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool reply_state_lock_main(mxs::Backend* backend, GWBUF&& packet,
                               const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool reply_state_unlock_main(mxs::Backend* backend, GWBUF&& packet,
                                 const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool reply_state_wait_main(mxs::Backend* backend, GWBUF&& packet,
                               const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool reply_state_wait_secondary(mxs::Backend* backend, GWBUF&& packet,
                                    const mxs::ReplyRoute& down, const mxs::Reply& reply);

    bool route_to_one(mxs::Backend* backend, GWBUF&& packet, mxs::Backend::response_type type);
    bool route_stored_command(mxs::Backend* backend);
    bool route_solo(GWBUF&& packet);
    bool route_main(GWBUF&& packet);
    bool route_secondary();
    bool route_queued();
    bool all_backends_idle() const;

    GWBUF finish_multinode();
    bool  check_node_status();

    // TODO: const-correct after parser is fixed
    bool is_multi_node(GWBUF& buffer) const;
    bool is_tmp_table_ddl(GWBUF& buffer) const;

    // Functions related to error handling and query retrying
    bool can_retry_secondary_query(std::string_view sqlstate);
    bool retry_secondary_query(mxs::Backend* backend);
    void fence_bad_node(mxs::Backend* backend);

    XRouter&  m_router;
    State     m_state{State::IDLE};
    SBackends m_backends;

    // The "main" node. This is the first node in the backend list and it's used by all MaxScale instances
    // for DDLs and other commands that need to be sent to multiple nodes (referred to as multi-node commands
    // in the source code). It's also the node which is locked before the DDLs get executed. As it's always
    // the same node that gets locked, the DDLs end up being executed serially across all MaxScale instances
    // that use the same configuration.
    //
    // The remaining nodes in the backend list are treated as "secondary" nodes. They execute the multi-node
    // commands without locks after the main node has successfully executed it but before the main node is
    // unlocked.
    //
    // This approach protects DDL execution most of the time but it is not free of race conditions: it is
    // possible that the main node executes a DDL successfully but the connection to it is lost immediately
    // afterwards. As the advisory locks are lost when the connection closes, it is possible that secondary
    // nodes end up executing the DDLs out-of-order compared to the main node. However, if the client receives
    // the response from MaxScale, it is guaranteed that all nodes that participated in the DDL have either
    // returned a response or died mid-operation.
    mxs::Backend* m_main;

    // The "solo" node. This one is used for all non-DDL queries that do not need any special handling like
    // SELECTs and INSERTs. This node is randomly chosen from the backend list which means it can be either
    // the main node or a secondary node. A node separate from the main one is used to load balance requests
    // across all available nodes.
    mxs::Backend* m_solo;

    // The list of queued queries that were received when the session was busy doing something else. These get
    // routed after whatever the session was doing is complete.
    std::list<GWBUF> m_queue;

    // The packets that make up the multi-node command
    std::list<GWBUF> m_packets;

    // The point in time when a retry of a multi-node command started on a secondary node. If the multi-node
    // command does not succeed before the configured limit is reached, the node is marked as failed.
    mxb::TimePoint m_retry_start {mxb::TimePoint::min()};

    // The response to the multi-node command that will be returned to the client
    GWBUF m_response;

    mariadb::TrxTracker m_trx_tracker;

    // The router configuration that was active when this session was started.
    XRouter::Config::ValueRef m_config;
};

class XgresSession final : public XRouterSession
{
    using XRouterSession::XRouterSession;

    void        preprocess(GWBUF&) override;
    std::string main_sql() const override;
    std::string secondary_sql() const override;
    std::string lock_sql(std::string_view lock_id) const override;
    std::string unlock_sql(std::string_view lock_id) const override;
};

class XmSession final : public XRouterSession
{
    using XRouterSession::XRouterSession;

    void        preprocess(GWBUF&) override;
    std::string main_sql() const override;
    std::string secondary_sql() const override;
    std::string lock_sql(std::string_view lock_id) const override;
    std::string unlock_sql(std::string_view lock_id) const override;
};
