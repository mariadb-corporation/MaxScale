/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/client.hh"

namespace maxscale
{

namespace mock
{

//
// Client
//
Client::Client(const char* zUser,
               const char* zHost,
               Handler* pHandler)
    : m_user(zUser)
    , m_host(zHost)
    , m_pHandler(pHandler)
    , m_n_responses(0)
{
}

Client::~Client()
{
}

const char* Client::user() const
{
    return m_user.c_str();
}

const char* Client::host() const
{
    return m_host.c_str();
}

size_t Client::n_responses() const
{
    return m_n_responses;
}

Client::Handler* Client::set_handler(Handler* pHandler)
{
    Handler* pH = m_pHandler;
    m_pHandler = pHandler;
    return pH;
}

void Client::reset()
{
    m_n_responses = 0;

    if (m_pHandler)
    {
        m_pHandler->reset();
    }
}

void Client::set_as_upstream_on(FilterModule::Session& filter_session)
{
    MXS_UPSTREAM upstream;
    upstream.instance = &m_instance;
    upstream.session = this;
    upstream.clientReply = &Client::clientReply;

    filter_session.setUpstream(&upstream);
}

int32_t Client::clientReply(GWBUF* pResponse)
{
    int32_t rv = 1;

    ++m_n_responses;

    if (m_pHandler)
    {
        rv = m_pHandler->backend_reply(pResponse);
    }
    else
    {
        gwbuf_free(pResponse);
    }

    return rv;
}

int32_t Client::write(GWBUF* pResponse)
{
    int32_t rv = 1;

    ++m_n_responses;

    if (m_pHandler)
    {
        rv = m_pHandler->maxscale_reply(pResponse);
    }
    else
    {
        gwbuf_free(pResponse);
    }

    return rv;
}

// static
int32_t Client::clientReply(MXS_FILTER* pInstance,
                            MXS_FILTER_SESSION* pSession,
                            GWBUF* pResponse)
{
    Client* pClient = reinterpret_cast<Client*>(pSession);
    mxb_assert(pInstance == &pClient->m_instance);

    return pClient->clientReply(pResponse);
}

//
// Client::Handler
//

Client::Handler::~Handler()
{
}

void Client::Handler::reset()
{
}
}
}
