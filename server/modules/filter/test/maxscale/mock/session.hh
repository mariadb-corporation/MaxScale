/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/listener.hh>
#include "client.hh"
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include "../../../core/internal/session.hh"

namespace maxscale
{

namespace mock
{
using SListenerData = std::shared_ptr<const mxs::ListenerData>;

/**
 * The class Session provides a mock MXS_SESSION that can be used when
 * testing.
 */
class Session : public ::Session
{
    Session(const Session&);
    Session& operator=(Session&);

public:

    /**
     * Constructor
     *
     * @param pClient  The client of the session. Must remain valid for
     *                 the lifetime of the Session.
     */
    Session(Client* pClient, SERVICE* service, SListenerData listener_data);
    ~Session();

    Client& client() const;

    bool route_query(GWBUF&& buffer)
    {
        return Session::routeQuery(std::move(buffer));
    }

    void set_downstream(FilterModule::Session* pSession);

private:
    Client&                                m_client;
    Dcb                                    m_client_dcb;
    std::unique_ptr<mxs::ClientConnection> m_sClient_connection;
};
}
}
