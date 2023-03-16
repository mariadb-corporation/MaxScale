/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "xrouter.hh"
#include "xroutersession.hh"

#include <maxscale/modutil.hh>

// static
std::string_view XRouterSession::state_to_str(State state)
{
    switch (state)
    {
    case State::INIT:
        return "INIT";

    case State::IDLE:
        return "IDLE";

    case State::SOLO:
        return "SOLO";

    case State::WAIT_SOLO:
        return "WAIT_SOLO";

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

XRouterSession::XRouterSession(MXS_SESSION* session, XRouter& router, SBackends backends)
    : RouterSession(session)
    , m_router(router)
    , m_backends(std::move(backends))
    , m_main(m_backends[rand() % m_backends.size()].get())
{
    for (auto& b : m_backends)
    {
        if (b->in_use())
        {
            const auto& sql = b.get() == m_main ?
                m_router.config().main_sql : m_router.config().secondary_sql;

            b->write(m_pSession->protocol()->make_query(sql), mxs::Backend::IGNORE_RESPONSE);
        }
    }
}

bool XRouterSession::routeQuery(GWBUF&& packet)
{
    bool ok = true;

    switch (m_state)
    {
    case State::IDLE:
        if (is_multi_node(packet))
        {
            m_state = State::MAIN;
            ok = route_main(std::move(packet));
        }
        else
        {
            m_state = State::SOLO;
            ok = route_solo(std::move(packet));
        }
        break;

    case State::SOLO:
        ok = route_solo(std::move(packet));
        break;

    case State::MAIN:
        ok = route_main(std::move(packet));
        break;

    case State::INIT:
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

    if (protocol_data()->will_respond(packet))
    {
        type = mxs::Backend::EXPECT_RESPONSE;
        m_state = State::WAIT_SOLO;
    }

    return route_to_one(m_main, std::move(packet), type);
}

bool XRouterSession::route_main(GWBUF&& packet)
{
    auto type = mxs::Backend::NO_RESPONSE;

    if (protocol_data()->will_respond(packet))
    {
        type = mxs::Backend::IGNORE_RESPONSE;
        m_state = State::WAIT_MAIN;
    }

    m_packets.push_back(packet.shallow_clone());
    return route_to_one(m_main, std::move(packet), type);
}

bool XRouterSession::route_secondary()
{
    bool ok = true;
    MXB_SINFO("Routing to secondary backends");

    for (auto& b : m_backends)
    {
        if (b->in_use() && b.get() != m_main)
        {
            for (const auto& packet : m_packets)
            {
                auto type = protocol_data()->will_respond(packet) ?
                    mxs::Backend::IGNORE_RESPONSE : mxs::Backend::NO_RESPONSE;

                if (!route_to_one(b.get(), packet.shallow_clone(), type))
                {
                    ok = false;
                }
            }
        }
    }

    return ok;
}

bool XRouterSession::route_to_one(mxs::Backend* backend, GWBUF&& packet, mxs::Backend::response_type type)
{
    MXB_SINFO("Route to '" << backend->name() << "': " << describe(packet));
    return backend->write(std::move(packet), type);
}

bool XRouterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::Backend* backend = static_cast<mxs::Backend*>(down.back()->get_userdata());
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
    case State::INIT:
        if (all_backends_idle())
        {
            // All initialization queries complete, proceed with normal routing
            m_state = State::IDLE;
        }
        break;

    case State::SOLO:
        // This might be an error condition in MaxScale but technically it is possible for the server to
        // send a partial response before we expect it.
        mxb_assert_message(!complete, "Result should not be complete");
        [[fallthrough]];

    case State::WAIT_SOLO:
        if (complete)
        {
            // We just routed the final response to the query, route queued queries
            mxb_assert(route);
            mxb_assert(all_backends_idle());
            m_state = State::IDLE;
        }
        break;

    case State::MAIN:
        // This might also be an error condition in MaxScale but we should still handle it.
        mxb_assert_message(!complete, "Result should not be complete");
        [[fallthrough]];

    case State::WAIT_MAIN:
        mxb_assert(!route);
        m_response.append(packet);
        packet.clear();

        if (complete)
        {
            mxb_assert(all_backends_idle());

            if (reply.error())
            {
                // The command failed, don't propagate the change
                MXB_SINFO("Multi-node command failed:" << reply.describe());
                route = true;
                packet = finish_multinode();
            }
            else
            {
                m_state = State::WAIT_SECONDARY;
                rv = route_secondary();
            }
        }
        break;

    case State::WAIT_SECONDARY:
        mxb_assert_message(!route, "No response expected from '%s'", backend->name());
        mxb_assert_message(backend != m_main, "Main backend should not respond");
        mxb_assert_message(m_main->is_idle(), "Main backend should be idle");

        if (complete)
        {
            if (reply.error())
            {
                // TODO: Fence out the node somehow
                MXB_SINFO("Command failed on '" << backend->name() << "': " << reply.describe());
            }

            if (all_backends_idle())
            {
                // All backends have responded with something, clear out the packets and route the response.
                MXB_SINFO("Multi-node command complete");
                route = true;
                packet = finish_multinode();
            }
        }
        break;

    default:
        MXB_SWARNING("Unexpected response" << reply.to_string() << " " << reply.describe());
        m_pSession->kill();
        mxb_assert(!true);
        rv = false;
        break;
    }

    if (route)
    {
        mxb_assert(packet);
        rv = mxs::RouterSession::clientReply(std::move(packet), down, reply);
    }

    if (rv && complete && m_state == State::IDLE)
    {
        rv = route_queued();
    }

    return rv;
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
    return m_pSession->protocol()->describe(buffer);
}

GWBUF XRouterSession::finish_multinode()
{
    GWBUF packet = std::move(m_response);
    m_response.clear();
    m_packets.clear();
    m_state = State::IDLE;
    return packet;
}

bool XRouterSession::is_multi_node(GWBUF& buffer) const
{
    using namespace mxs::sql;
    bool is_multi = false;

    if (!mxs::Parser::type_mask_contains(parser().get_type_mask(buffer), TYPE_CREATE_TMP_TABLE))
    {
        switch (parser().get_operation(buffer))
        {
        // TODO: Update with the parser changes when merging
        case OP_CREATE:
        case OP_CREATE_TABLE:
        case OP_DROP:
        case OP_DROP_TABLE:
        case OP_ALTER:
        case OP_ALTER_TABLE:
        case OP_GRANT:
        case OP_REVOKE:
            is_multi = true;
            break;

        default:
            break;
        }
    }

    return is_multi;
}
