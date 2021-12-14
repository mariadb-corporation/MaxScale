/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/session.hh"

namespace maxscale
{

namespace mock
{

bool Session::Endpoint::routeQuery(GWBUF* buffer)
{
    return m_session.routeQuery(buffer);
}

bool Session::Endpoint::clientReply(GWBUF* buffer, ReplyRoute& down, const mxs::Reply& reply)
{
    return 0;
}

bool Session::Endpoint::handleError(mxs::ErrorType type,
                                    GWBUF* error,
                                    mxs::Endpoint* down,
                                    const mxs::Reply& reply)
{
    return true;
}

Session::Session(Client* pClient, SListenerData listener_data)
    : ::Session(std::move(listener_data), pClient->host())
    , m_client(*pClient)
    , m_client_dcb(this, pClient->host(), pClient)
{
    set_user(pClient->user());

    m_state = MXS_SESSION::State::CREATED;
    client_dcb = &m_client_dcb;
    set_protocol_data(std::make_unique<MYSQL_session>());
}

Session::~Session()
{
    m_down->close();
    // This prevents the protocol module from freeing the data
    refcount = 0;
    client_dcb = nullptr;
}

Client& Session::client() const
{
    return m_client;
}

void Session::set_downstream(FilterModule::Session* pSession)
{
    m_down = std::unique_ptr<mxs::Endpoint>(new Endpoint(pSession));
}
}
}
