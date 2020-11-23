/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-16
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

int32_t RouterSession::routeQuery(GWBUF* pStatement)
{
    m_pBackend->handle_statement(this, pStatement);
    return 1;
}

int32_t RouterSession::clientReply(GWBUF* pResponse, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return m_pUpstream_filter_session->clientReply(pResponse, reply);
}
}
}
