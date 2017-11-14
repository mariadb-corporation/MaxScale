/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/upstream.hh"

namespace maxscale
{

namespace mock
{

//
// Upstream
//
Upstream::Upstream(Handler* pHandler)
    : m_pHandler(pHandler)
    , m_n_responses(0)
{
}

Upstream::~Upstream()
{
}

size_t Upstream::n_responses() const
{
    return m_n_responses;
}

Upstream::Handler* Upstream::set_handler(Handler* pHandler)
{
    Handler* pH = m_pHandler;
    m_pHandler = pHandler;
    return pH;
}

void Upstream::reset()
{
    m_n_responses = 0;

    if (m_pHandler)
    {
        m_pHandler->reset();
    }
}

void Upstream::set_as_upstream_on(FilterModule::Session& filter_session)
{
    MXS_UPSTREAM upstream;
    upstream.instance = &m_instance;
    upstream.session = this;
    upstream.clientReply = &Upstream::clientReply;
    upstream.error = NULL;

    filter_session.setUpstream(&upstream);
}

int32_t Upstream::clientReply(GWBUF* pResponse)
{
    int rv = 1;

    ++m_n_responses;

    if (m_pHandler)
    {
        rv = m_pHandler->clientReply(pResponse);
    }
    else
    {
        gwbuf_free(pResponse);
    }

    return rv;
}

//static
int32_t Upstream::clientReply(MXS_FILTER* pInstance,
                              MXS_FILTER_SESSION* pSession,
                              GWBUF* pResponse)
{
    Upstream* pUpstream = reinterpret_cast<Upstream*>(pSession);
    ss_dassert(pInstance == &pUpstream->m_instance);

    return pUpstream->clientReply(pResponse);
}

//
// Upstream::Handler
//

Upstream::Handler::~Handler()
{
}

void Upstream::Handler::reset()
{
}

}

}
