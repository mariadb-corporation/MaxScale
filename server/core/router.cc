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

#include <maxscale/router.hh>

namespace maxscale
{

//
// RouterSession
//
RouterSession::RouterSession(MXS_SESSION* pSession)
    : m_pSession(pSession)
{
}

RouterSession::~RouterSession()
{
}

void RouterSession::close()
{
}

int32_t RouterSession::routeQuery(GWBUF* pPacket)
{
    return 0;
}

void RouterSession::clientReply(GWBUF* pPacket, DCB* pBackend)
{
}

void RouterSession::handleError(GWBUF*             pMessage,
                                DCB*               pProblem,
                                mxs_error_action_t action,
                                bool*              pSuccess)
{
}

}
