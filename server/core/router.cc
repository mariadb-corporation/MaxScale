/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/router.hh>
#include <maxscale/session.hh>

namespace maxscale
{

//
// RouterSession
//
RouterSession::RouterSession(MXS_SESSION* pSession)
    : m_pSession(pSession)
    , m_pParser(m_pSession->client_connection()->parser())
{
}

bool RouterSession::clientReply(GWBUF&& packet, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    return m_pUp->clientReply(std::move(packet), down, reply);
}

bool RouterSession::handleError(mxs::ErrorType type, const std::string& message, mxs::Endpoint* pProblem,
                                const mxs::Reply& reply)
{
    return m_pUpstream->handleError(type, message, m_endpoint, reply);
}

void RouterSession::set_response(GWBUF&& response) const
{
    session_set_response(m_pSession, m_pUp, std::move(response));
}

const mxs::ProtocolData& RouterSession::protocol_data() const
{
    mxb_assert(m_pSession->protocol_data());
    return *m_pSession->protocol_data();
}

const mxs::ProtocolModule& RouterSession::protocol() const
{
    mxb_assert(m_pSession->protocol());
    return *m_pSession->protocol();
}
}
