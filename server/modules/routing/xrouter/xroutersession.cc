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

XRouterSession::XRouterSession(MXS_SESSION* session, XRouter* router, SBackends backends)
    : RouterSession(session)
    , m_backends(std::move(backends))
    , m_main(m_backends[rand() % m_backends.size()].get())
{
}

bool XRouterSession::routeQuery(GWBUF&& packet)
{
    if (m_state != State::IDLE)
    {
        MXB_SINFO("Queuing '" << (char)packet[0] << "'");
        m_queue.push_back(std::move(packet));
        return true;
    }

    return route_query(std::move(packet));
}

bool XRouterSession::route_query(GWBUF&& packet)
{
    // TODO: use the query classifier to detect statements that need to be routed to all nodes
    if ("should route to all"s == "yes, we should"s)
    {
        return route_to_all(std::move(packet));
    }

    return route_to_one(std::move(packet));
}

bool XRouterSession::route_to_one(GWBUF&& packet)
{
    MXB_SINFO("Route '" << (char)packet[0] << "' to '" << m_main->name() << "'");
    const auto type = response_type(m_main, packet);
    return m_main->write(std::move(packet), type);
}

bool XRouterSession::route_to_all(GWBUF&& packet)
{
    bool ok = true;
    m_state = State::BUSY;
    MXB_SINFO("Routing '" << (char)packet[0] << "' to all backends");

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

    if (reply.is_complete())
    {
        backend->ack_write();
        MXB_SINFO("Reply complete from " << backend->name());
    }
    else
    {
        MXB_SINFO("Partial reply from " << backend->name());
    }

    if (route)
    {
        mxb_assert(backend == m_main);
        rv = mxs::RouterSession::clientReply(std::move(packet), down, reply);

        if (rv && reply.is_complete() && !backend->is_waiting_result())
        {
            // No more results are expected, route any queued queries
            rv = route_queued();
        }
    }

    return rv;
}

bool XRouterSession::route_queued()
{
    bool ok = true;
    m_state = State::ROUTE_QUEUED;

    while (!m_queue.empty() && ok && m_state == State::ROUTE_QUEUED)
    {
        ok = route_query(std::move(m_queue.front()));
        m_queue.pop_front();
    }

    if (m_state == State::ROUTE_QUEUED)
    {
        m_state = State::IDLE;
    }

    return ok;
}
