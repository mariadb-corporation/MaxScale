/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "mock.hh"
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
    Session(Client* pClient, SListenerData listener_data);
    ~Session();

    Client& client() const;

    bool route_query(GWBUF* pBuffer)
    {
        return Session::routeQuery(pBuffer);
    }

    void set_downstream(FilterModule::Session* pSession);

private:
    class Endpoint final : public mxs::Endpoint
    {
    public:
        Endpoint(FilterModule::Session* pSession)
            : m_session(*pSession)
        {
        }

        bool routeQuery(GWBUF* buffer) override;
        bool clientReply(GWBUF* buffer, ReplyRoute& down, const mxs::Reply& reply) override;
        bool handleError(mxs::ErrorType type, GWBUF* error,
                         mxs::Endpoint* down, const mxs::Reply& reply) override;

        bool connect() override
        {
            return true;
        }

        void close() override
        {
            m_open = false;
        }

        bool is_open() const override
        {
            return m_open;
        }

        mxs::Target* target() const override
        {
            return nullptr;
        }

    private:
        FilterModule::Session& m_session;
        bool                   m_open = true;
    };

    Client&       m_client;
    Dcb           m_client_dcb;
};
}
}
