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
#pragma once

#include "xrouter.hh"
#include <maxscale/backend.hh>
#include <list>

using SBackends = std::vector<std::unique_ptr<mxs::Backend>>;

class XRouterSession final : public mxs::RouterSession
{
public:
    XRouterSession(MXS_SESSION* session, XRouter& router, SBackends backends);
    bool routeQuery(GWBUF&& packet) override;
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    enum class State
    {
        INIT,
        IDLE,
        SOLO,
        WAIT_SOLO,
        BUSY,
    };

    bool route_to_one(GWBUF&& packet, State next_state);
    bool route_solo(GWBUF&& packet);
    bool route_to_all(GWBUF&& packet);
    bool route_queued();
    bool all_backends_idle() const;

    std::string                 describe(const GWBUF& buffer);
    mxs::Backend::response_type response_type(mxs::Backend* backend, const GWBUF& buffer);

    XRouter&         m_router;
    State            m_state{State::INIT};
    SBackends        m_backends;
    mxs::Backend*    m_main;
    std::list<GWBUF> m_queue;
};
