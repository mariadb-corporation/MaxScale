/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/session.hh"
#include <maxscale/protocol/mariadb/mariadbparser.hh>

namespace maxscale
{

namespace mock
{

class MockClientConnection : public mxs::ClientConnectionBase
{
public:

    MockClientConnection(DCB* dcb)
    {
        set_dcb(dcb);
    }

    bool init_connection() override
    {
        return true;
    }

    void finish_connection() override
    {
    }

    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
    {
        return true;
    }

    void ready_for_reading(DCB* dcb) override
    {
    }

    void error(DCB* dcb, const char* errmsg) override
    {
    }

    bool safe_to_restart() const override
    {
        return true;
    }

    size_t sizeof_buffers() const override
    {
        return 0;
    }

    Parser* parser() override
    {
        return &MariaDBParser::get();
    };
};

bool Session::Endpoint::routeQuery(GWBUF&& buffer)
{
    return m_session.routeQuery(std::move(buffer));
}

bool Session::Endpoint::clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return 0;
}

bool Session::Endpoint::handleError(mxs::ErrorType type,
                                    const std::string& error,
                                    mxs::Endpoint* down,
                                    const mxs::Reply& reply)
{
    return true;
}

Session::Session(Client* pClient, SERVICE* service, SListenerData listener_data)
    : ::Session(std::move(listener_data), {}, service, pClient->host())
    , m_client(*pClient)
    , m_client_dcb(this, pClient->host(), pClient)
    , m_sClient_connection(std::make_unique<MockClientConnection>(&m_client_dcb))
{
    set_user(pClient->user());

    m_state = MXS_SESSION::State::CREATED;
    client_dcb = &m_client_dcb;
    set_client_connection(m_sClient_connection.get());
    set_protocol_data(std::make_unique<MYSQL_session>(0, false, false));
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
