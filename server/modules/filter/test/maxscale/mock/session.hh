/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
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

/**
 * The class Session provides a mock MXS_SESSION that can be used when
 * testing.
 */
class Session : public ::Session
{
    Session(const Session&);
    Session& operator=(Session&);

public:
    typedef mxs_session_trx_state_t trx_state_t;

    /**
     * Constructor
     *
     * @param pClient  The client of the session. Must remain valid for
     *                 the lifetime of the Session.
     */
    Session(Client* pClient, const SListener& listener);
    ~Session();

    Client& client() const;

    bool is_autocommit() const
    {
        return session_is_autocommit(this);
    }

    void set_autocommit(bool autocommit)
    {
        session_set_autocommit(this, autocommit);
    }

    trx_state_t trx_state() const
    {
        return session_get_trx_state(this);
    }

    void set_trx_state(trx_state_t state)
    {
        session_set_trx_state(this, state);
    }

    bool route_query(GWBUF* pBuffer)
    {
        return mxs_route_query(this, pBuffer);
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

        int32_t routeQuery(GWBUF* buffer) override;
        int32_t clientReply(GWBUF* buffer, ReplyRoute& down, const mxs::Reply& reply) override;
        bool handleError(GWBUF* error, mxs::Endpoint* down, const mxs::Reply& reply) override;

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

        mxs::Target* target() const
        {
            return nullptr;
        }

    private:
        FilterModule::Session& m_session;
        bool                   m_open = true;
    };

    Client&       m_client;
    Dcb           m_client_dcb;
    MYSQL_session m_mysql_session;
};
}
}
