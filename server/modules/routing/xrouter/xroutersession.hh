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
    XRouterSession(MXS_SESSION* session, XRouter* router, SBackends backends);
    bool routeQuery(GWBUF&& packet) override;
    bool clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    enum class State
    {
        IDLE,
        BUSY,
        ROUTE_QUEUED,
    };

    bool route_query(GWBUF&& packet);
    bool route_to_one(GWBUF&& packet);
    bool route_to_all(GWBUF&& packet);
    bool route_queued();
    bool all_backends_idle();

    mxs::Backend::response_type response_type(mxs::Backend* backend, const GWBUF& buffer);

    State            m_state{State::IDLE};
    SBackends        m_backends;
    mxs::Backend*    m_main;
    std::list<GWBUF> m_queue;
};
