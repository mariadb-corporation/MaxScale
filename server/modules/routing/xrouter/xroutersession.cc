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
        // TODO: use the query classifier to detect statements that need to be routed to all nodes
        if ("should route to all"s == "yes, we should"s)
        {
            m_state = State::BUSY;
            ok = route_to_all(std::move(packet));
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

    case State::INIT:
    case State::WAIT_SOLO:
    case State::BUSY:
        MXB_SINFO("Queuing: " << describe(packet));
        m_queue.push_back(std::move(packet));
        break;
    }

    return ok;
}

bool XRouterSession::route_solo(GWBUF&& packet)
{
    return route_to_one(std::move(packet), State::WAIT_SOLO);
}

bool XRouterSession::route_to_one(GWBUF&& packet, State next_state)
{
    MXB_SINFO("Route to '" << m_main->name() << "': " << describe(packet));
    auto type = mxs::Backend::NO_RESPONSE;

    if (protocol_data()->will_respond(packet))
    {
        type = mxs::Backend::EXPECT_RESPONSE;
        m_state = next_state;
    }

    return m_main->write(std::move(packet), type);
}

bool XRouterSession::route_to_all(GWBUF&& packet)
{
    bool ok = true;
    m_state = State::BUSY;
    MXB_SINFO("Routing to all backends: " << describe(packet));

    for (auto& b : m_backends)
    {
        if (b->in_use())
        {
            if (b->write(packet.shallow_clone(), response_type(b.get(), packet)))
            {
                MXB_SINFO("Route to '" << b->name() << "'");
            }
            else
            {
                ok = false;
            }
        }
    }

    return ok;
}

mxs::Backend::response_type XRouterSession::response_type(mxs::Backend* backend, const GWBUF& packet)
{
    if (!protocol_data()->will_respond(packet))
    {
        return mxs::Backend::NO_RESPONSE;
    }

    return backend == m_main ? mxs::Backend::EXPECT_RESPONSE : mxs::Backend::IGNORE_RESPONSE;
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

    default:
        MXB_SWARNING("Unexpected response" << reply.to_string() << " " << reply.describe());
        m_pSession->kill();
        mxb_assert(!true);
        rv = false;
        break;
    }

    if (route)
    {
        mxb_assert(backend == m_main);
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
