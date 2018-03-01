/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/filter.hh>

namespace maxscale
{

//
// FilterSession
//

FilterSession::FilterSession(MXS_SESSION* pSession)
    : m_pSession(pSession)
{
}

FilterSession::~FilterSession()
{
}

void FilterSession::close()
{
}

void FilterSession::setDownstream(const Downstream& down)
{
    m_down = down;
}

void FilterSession::setUpstream(const Upstream& up)
{
    m_up = up;
}

int FilterSession::routeQuery(GWBUF* pPacket)
{
    return m_down.routeQuery(pPacket);
}

int FilterSession::clientReply(GWBUF* pPacket)
{
    return m_up.clientReply(pPacket);
}

void FilterSession::diagnostics(DCB* pDcb)
{
}

}

