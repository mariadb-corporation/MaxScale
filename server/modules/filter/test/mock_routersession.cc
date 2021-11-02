/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/mock/routersession.hh"
#include "maxscale/mock/backend.hh"

namespace maxscale
{

namespace mock
{

RouterSession::RouterSession(Backend* pBackend, maxscale::mock::Session* session)
    : m_pBackend(pBackend)
    , m_pSession(session)
{
    memset(&m_instance, 0, sizeof(m_instance));
}

RouterSession::~RouterSession()
{
}

void RouterSession::set_upstream(FilterModule::Session* pFilter_session)
{
    m_pUpstream_filter_session = pFilter_session;
}

bool RouterSession::respond()
{
    return m_pBackend->respond(this, mxs::Reply());
}

bool RouterSession::idle() const
{
    return m_pBackend->idle(this);
}

bool RouterSession::discard_one_response()
{
    return m_pBackend->discard_one_response(this);
}

void RouterSession::discard_all_responses()
{
    return m_pBackend->discard_all_responses(this);
}

int32_t RouterSession::routeQuery(MXS_ROUTER* pInstance, GWBUF* pStatement)
{
    mxb_assert(pInstance == &m_instance);

    m_pBackend->handle_statement(this, pStatement);
    return 1;
}

int32_t RouterSession::clientReply(GWBUF* pResponse, const mxs::Reply& reply)
{
    return m_pUpstream_filter_session->clientReply(pResponse, reply);
}

// static
int32_t RouterSession::routeQuery(MXS_FILTER* pInstance,
                                  MXS_FILTER_SESSION* pRouter_session,
                                  GWBUF* pStatement)
{
    RouterSession* pThis = reinterpret_cast<RouterSession*>(pRouter_session);

    return pThis->routeQuery(reinterpret_cast<MXS_ROUTER*>(pInstance), pStatement);
}
}
}
