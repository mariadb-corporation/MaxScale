/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "xrouter.hh"
#include "xroutersession.hh"
#include <maxscale/service.hh>

#include <algorithm>

// static
std::string_view XRouterSession::state_to_str(State state)
{
    switch (state)
    {
    case State::IDLE:
        return "IDLE";

    case State::SOLO:
        return "SOLO";

    case State::WAIT_SOLO:
        return "WAIT_SOLO";

    case State::LOAD_DATA:
        return "LOAD_DATA";

    case State::LOCK_MAIN:
        return "LOCK_MAIN";

    case State::UNLOCK_MAIN:
        return "UNLOCK_MAIN";

    case State::MAIN:
        return "MAIN";

    case State::WAIT_MAIN:
        return "WAIT_MAIN";

    case State::WAIT_SECONDARY:
        return "WAIT_SECONDARY";
    }

    mxb_assert(!true);
    return "UNKNOWN";
}

std::string_view XRouterSession::state_str() const
{
    return state_to_str(m_state);
}

XRouterSession::XRouterSession(MXS_SESSION* session, XRouter& router, SBackends backends,
                               XRouter::Config::ValueRef config)
    : RouterSession(session)
    , m_router(router)
    , m_backends(std::move(backends))
    , m_main(m_backends[0].get())
    , m_solo(m_backends[rand() % m_backends.size()].get())
    , m_config(std::move(config))
{
}

bool XRouterSession::routeQuery(GWBUF&& packet)
{
    if (!m_main->in_use() || !m_solo->in_use())
    {
        MXB_SINFO("Main node or the single-target node is no longer in use, closing session.");
        return false;
    }

    bool ok = true;

    switch (m_state)
    {
    case State::IDLE:
        m_trx_tracker.track_transaction_state(packet, parser());
        preprocess(packet);

        if (!check_node_status())
        {
            ok = false;
        }
        else if (is_multi_node(packet))
        {
            // Send the lock query to the main node before doing the DDL. This way the operations are
            // serialized with respect to the main node.
            MXB_SINFO("Multi-node command, sending `main_sql` and `lock_sql` to '"
                      << m_main->name() << "': " << describe(packet));
            m_state = State::LOCK_MAIN;
            ok = send_query(m_main, main_sql()) && send_query(m_main, lock_sql(m_config->lock_id));
            m_queue.push_back(std::move(packet));
        }
        else
        {
            if (is_tmp_table_ddl(packet) && m_solo != m_main)
            {
                MXB_SINFO("Temporary table DDL on non-main node, "
                          << "send `main_sql` to '" << m_solo->name() << "': " << describe(packet));
                ok = send_query(m_solo, main_sql());
            }
            else
            {
                MXB_SINFO("Single-node command on '" << m_main->name() << "': " << describe(packet));
            }

            if (ok)
            {
                // Normal single-node query (DML) that does not need to be sent to the secondary nodes.
                m_state = State::SOLO;
                ok = route_solo(std::move(packet));
            }
        }
        break;

    case State::SOLO:
        // More packets that belong to the single-node command. Keep routing them until we get one that will
        // generate a response.
        ok = route_solo(std::move(packet));
        break;

    case State::LOAD_DATA:
        // Client is uploading data, keep routing it to the solo node until the server responds.
        ok = route_to_one(m_solo, std::move(packet), mxs::Backend::NO_RESPONSE);
        break;

    case State::MAIN:
        // More packets that belong to the multi-node command. Keep routing them until we get one that will
        // generate a response.
        ok = route_main(std::move(packet));
        break;

    case State::LOCK_MAIN:
    case State::UNLOCK_MAIN:
    case State::WAIT_SOLO:
    case State::WAIT_MAIN:
    case State::WAIT_SECONDARY:
        MXB_SINFO("Queuing: " << describe(packet));
        m_queue.push_back(std::move(packet));
        break;
    }

    return ok;
}

bool XRouterSession::route_solo(GWBUF&& packet)
{
    auto type = mxs::Backend::NO_RESPONSE;

    if (protocol_data().will_respond(packet))
    {
        type = mxs::Backend::EXPECT_RESPONSE;
        m_state = State::WAIT_SOLO;
    }

    return route_to_one(m_solo, std::move(packet), type);
}

bool XRouterSession::route_main(GWBUF&& packet)
{
    auto type = mxs::Backend::NO_RESPONSE;

    if (protocol_data().will_respond(packet))
    {
        type = mxs::Backend::IGNORE_RESPONSE;
        m_state = State::WAIT_MAIN;
    }

    m_packets.push_back(packet.shallow_clone());
    return route_to_one(m_main, std::move(packet), type);
}

bool XRouterSession::route_stored_command(mxs::Backend* backend)
{
    MXB_SINFO("Sending `secondary_sql` to '" << backend->name() << "'");
    bool ok = send_query(backend, secondary_sql());

    if (ok)
    {
        for (const auto& packet : m_packets)
        {
            auto type = protocol_data().will_respond(packet) ?
                mxs::Backend::IGNORE_RESPONSE : mxs::Backend::NO_RESPONSE;

            if (!route_to_one(backend, packet.shallow_clone(), type))
            {
                ok = false;
                break;
            }
        }
    }

    return ok;
}

bool XRouterSession::route_secondary()
{
    bool ok = true;
    MXB_SINFO("Routing to secondary backends");

    for (auto& b : m_backends)
    {
        if (b->in_use() && b.get() != m_main && !route_stored_command(b.get()))
        {
            ok = false;
        }
    }

    return ok;
}

bool XRouterSession::route_to_one(mxs::Backend* backend, GWBUF&& packet, mxs::Backend::response_type type)
{
    MXB_SINFO("Route to '" << backend->name() << "': " << describe(packet));
    mxb_assert(backend->in_use());
    return backend->write(std::move(packet), type);
}

bool XRouterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::Backend* backend = static_cast<mxs::Backend*>(down.endpoint()->get_userdata());
    bool rv = true;
    bool route = backend->is_expected_response();
    bool complete = reply.is_complete();

    if (complete)
    {
        backend->ack_write();
        MXB_SINFO("Reply complete from " << backend->name() << ". " << reply.describe());
    }
    else
    {
        MXB_SINFO("Partial reply from " << backend->name());
    }

    switch (m_state)
    {
    case State::SOLO:
        // This might be an error condition in MaxScale but technically it is possible for the server to
        // send a partial response before we expect it.
        mxb_assert_message(!complete, "Result should not be complete");
        [[fallthrough]];

    case State::LOAD_DATA:
    case State::WAIT_SOLO:
        mxb_assert(route);
        rv = reply_state_wait_solo(backend, std::move(packet), down, reply);
        break;

    case State::LOCK_MAIN:
        mxb_assert(!route);
        rv = reply_state_lock_main(backend, std::move(packet), down, reply);
        break;

    case State::UNLOCK_MAIN:
        mxb_assert(!route);
        rv = reply_state_unlock_main(backend, std::move(packet), down, reply);
        break;

    case State::MAIN:
        // This might also be an error condition in MaxScale but we should still handle it.
        mxb_assert_message(!complete, "Result should not be complete");
        [[fallthrough]];

    case State::WAIT_MAIN:
        mxb_assert(!route);
        rv = reply_state_wait_main(backend, std::move(packet), down, reply);
        break;

    case State::WAIT_SECONDARY:
        mxb_assert(!route);
        rv = reply_state_wait_secondary(backend, std::move(packet), down, reply);
        break;

    default:
        MXB_SWARNING("Unexpected response: " << reply.describe());
        m_pSession->kill();
        mxb_assert(!true);
        rv = false;
        break;
    }

    return rv;
}

bool XRouterSession::reply_state_wait_solo(mxs::Backend* backend, GWBUF&& packet,
                                           const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (reply.is_complete())
    {
        // We just routed the final response to the query, route queued queries
        mxb_assert(all_backends_idle());
        m_state = State::IDLE;
    }
    else if (reply.state() == mxs::ReplyState::LOAD_DATA)
    {
        MXB_SINFO("Data load starting, waiting for more data from the client.");

        // It's possible that the current state is already LOAD_DATA. In this case the client executed a
        // query starts multiple data loads. For example, in MariaDB multiple LOAD DATA LOCAL INFILE
        // commands separated by a semicolons would result in this.
        m_state = State::LOAD_DATA;
    }

    bool rv = mxs::RouterSession::clientReply(std::move(packet), down, reply);

    if (rv && (m_state == State::IDLE || m_state == State::LOAD_DATA))
    {
        rv = route_queued();
    }

    return rv;
}

bool XRouterSession::reply_state_lock_main(mxs::Backend* backend, GWBUF&& packet,
                                           const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool complete = reply.is_complete();
    bool rv = true;

    if (complete)
    {
        if (backend->is_idle())
        {
            MXB_SINFO("Main node locked, routing query to main node.");
            m_state = State::MAIN;
            rv = route_queued();
        }
        else
        {
            MXB_SINFO("`main_sql` response received, waiting for `lock_sql` response.");
        }
    }

    return rv;
}

bool XRouterSession::reply_state_unlock_main(mxs::Backend* backend, GWBUF&& packet,
                                             const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool rv = true;

    if (reply.is_complete())
    {
        MXB_SINFO("Main node unlocked, returning to normal routing.");
        m_state = State::IDLE;
        rv = route_queued();
    }

    return rv;
}

bool XRouterSession::reply_state_wait_main(mxs::Backend* backend, GWBUF&& packet,
                                           const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool rv = true;
    m_response.append(packet);
    packet.clear();

    if (reply.is_complete())
    {
        mxb_assert(all_backends_idle());

        if (reply.error())
        {
            // The command failed, don't propagate the change
            MXB_SINFO("Multi-node command failed: " << reply.describe());
            rv = mxs::RouterSession::clientReply(finish_multinode(), down, reply);
        }
        else
        {
            // The command was successful. Route the stored command to the secondary backends and wait for
            // their responses. The lock is held on the main node for the duration of this to serialize the
            // execution of multi-node commands across all MaxScale instances.
            m_state = State::WAIT_SECONDARY;
            rv = route_secondary();
        }
    }

    return rv;
}

bool XRouterSession::reply_state_wait_secondary(mxs::Backend* backend, GWBUF&& packet,
                                                const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    bool rv = true;
    mxb_assert_message(backend != m_main, "Main backend should not respond");
    mxb_assert_message(m_main->is_idle(), "Main backend should be idle");

    if (reply.is_complete())
    {
        bool route = true;

        if (reply.error())
        {
            MXB_SINFO("Command failed on '" << backend->name() << "': " << reply.describe());

            if (can_retry_secondary_query(reply.error().sql_state()) && retry_secondary_query(backend))
            {
                // The query is being retried, return the result to the client after it completes.
                route = false;
            }
        }

        if (route && all_backends_idle())
        {
            // All backends have responded with something, clear out the packets and route the response.
            MXB_SINFO("Multi-node command complete");
            rv = mxs::RouterSession::clientReply(finish_multinode(), down, reply);
        }
    }

    return rv;
}

bool XRouterSession::handleError(mxs::ErrorType type, const std::string& message,
                                 mxs::Endpoint* pProblem, const mxs::Reply& reply)
{
    mxs::Backend* backend = static_cast<mxs::Backend*>(pProblem->get_userdata());
    bool can_continue = false;

    MXB_SINFO("Node '" << backend->name() << "' failed: " << message);

    if (backend != m_main && m_state == State::WAIT_SECONDARY)
    {
        can_continue = retry_secondary_query(backend);
    }
    else if (backend == m_solo && backend->is_waiting_result())
    {
        mxb_assert(m_state == State::SOLO || m_state == State::WAIT_SOLO);
        MXB_SINFO("Solo query was interrupted, closing session.");
    }

    // Close the backend. If a reconnection takes place, it is done after handleError has returned.
    backend->close();

    return can_continue || mxs::RouterSession::handleError(type, message, pProblem, reply);
}

bool XRouterSession::route_queued()
{
    bool ok = true;
    bool again = true;

    while (!m_queue.empty() && ok && again)
    {
        ok = routeQuery(std::move(m_queue.front()));
        m_queue.pop_front();

        switch (m_state)
        {
        case State::UNLOCK_MAIN:
        case State::LOCK_MAIN:
        case State::WAIT_SOLO:
        case State::WAIT_MAIN:
        case State::WAIT_SECONDARY:
            again = false;
            break;

        default:
            break;
        }
    }

    if (!ok)
    {
        MXB_SINFO("Failed to route queued queries");
        m_pSession->kill();
    }

    return ok;
}

bool XRouterSession::all_backends_idle() const
{
    return std::all_of(m_backends.begin(), m_backends.end(), [](const auto& b){
        return b->is_idle();
    });
}

std::string XRouterSession::describe(const GWBUF& buffer)
{
    return protocol().describe(buffer);
}

bool XRouterSession::send_query(mxs::Backend* backend, std::string_view sql)
{
    return route_to_one(backend, protocol().make_query(sql), mxs::Backend::IGNORE_RESPONSE);
}

void XRouterSession::fence_bad_node(mxs::Backend* backend)
{
    if (!backend->target()->is_in_maint())
    {
        auto servers = m_router.service().reachable_servers();

        if (auto it = std::find(servers.begin(), servers.end(), backend->target()); it != servers.end())
        {
            SERVER* srv = *it;
            MXB_SWARNING("Server '" << srv->name() << "' has failed. "
                                    << "The node has been excluded from routing and "
                                    << "is now in maintenance mode.");
            srv->set_maintenance();
        }
    }

    backend->close(mxs::Backend::CLOSE_FATAL);
}

bool XRouterSession::check_node_status()
{
    for (auto& b : m_backends)
    {
        if (b->in_use() && !b->can_connect())
        {
            b->close();
        }
    }

    return m_main->in_use() && m_solo->in_use();
}

GWBUF XRouterSession::finish_multinode()
{
    GWBUF packet = std::move(m_response);
    m_response.clear();
    m_packets.clear();
    m_retry_start = mxb::TimePoint::min();
    m_state = State::UNLOCK_MAIN;
    MXB_SINFO("Unlocking main backend.");

    if (!send_query(m_main, unlock_sql(m_config->lock_id)))
    {
        MXB_SINFO("Failed to unlock main backend, next query will close the session.");
        m_main->close(mxs::Backend::CLOSE_FATAL);
    }

    return packet;
}

bool XRouterSession::is_multi_node(GWBUF& buffer) const
{
    using namespace mxs::sql;
    bool is_multi = false;

    if (!mxs::Parser::type_mask_contains(parser().get_type_mask(buffer), TYPE_CREATE_TMP_TABLE))
    {
        auto op = parser().get_operation(buffer);

        switch (op)
        {
        case OP_ALTER_TABLE:
        case OP_ALTER:
        case OP_CREATE_ROLE:
        case OP_CREATE_TABLE:
        case OP_CREATE_USER:
        case OP_CREATE:
        case OP_DROP_TABLE:
        case OP_DROP:
        case OP_GRANT:
        case OP_REVOKE:
            MXB_INFO("Multi-node command: %s", mxs::sql::to_string(op));
            is_multi = true;
            break;

        case OP_CALL:
        case OP_CHANGE_DB:
        case OP_DELETE:
        case OP_EXECUTE:
        case OP_EXPLAIN:
        case OP_INSERT:
        case OP_KILL:
        case OP_LOAD_LOCAL:
        case OP_LOAD:
        case OP_SELECT:
        case OP_SET_TRANSACTION:
        case OP_SET:
        case OP_SHOW_DATABASES:
        case OP_SHOW:
        case OP_TRUNCATE:
        case OP_UNDEFINED:
        case OP_UPDATE:
            MXB_INFO("Single-node command: %s", mxs::sql::to_string(op));
            break;
        }
    }

    return is_multi;
}

bool XRouterSession::is_tmp_table_ddl(GWBUF& buffer) const
{
    using namespace mxs::sql;
    // TODO: Detect DROP and ALTER of a temporary table
    return mxs::Parser::type_mask_contains(parser().get_type_mask(buffer), TYPE_CREATE_TMP_TABLE);
}

bool XRouterSession::can_retry_secondary_query(std::string_view sqlstate)
{
    return std::any_of(
        m_config->retry_sqlstates.begin(), m_config->retry_sqlstates.end(), [&](const auto& value){
        return sqlstate.substr(0, value.size()) == value;
    });
}

bool XRouterSession::retry_secondary_query(mxs::Backend* backend)
{
    bool ok = true;
    auto func = [this, backend](auto ignored){
        if (!backend->in_use())
        {
            backend->connect();
        }

        return route_stored_command(backend);
    };

    if (m_retry_start == mxb::TimePoint::min())
    {
        MXB_SINFO("Retrying query for the first time.");
        m_retry_start = mxb::Clock::now();

        // Route the query again to the secondary node in the hopes that it will work when executed again. Add
        // a small delay to avoid flooding the server with requests if the command completes very fast.
        //
        // TODO: The GWBUF argument to delay_routing is unnecessary in this case. A similar function for
        //       safely delaying execution of router code would be useful.
        m_pSession->delay_routing(this, GWBUF {}, 1s, std::move(func));
    }
    else if (mxb::Clock::now() - m_retry_start < m_config->retry_timeout)
    {
        MXB_SINFO("Retrying query again.");
        m_pSession->delay_routing(this, GWBUF {}, 1s, std::move(func));
    }
    else
    {
        MXB_SINFO("Query retry time limit reached, fencing out the bad node.");
        fence_bad_node(backend);
        ok = false;
    }

    return ok;
}
