/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinlokisession.hh"

#include <maxscale/modutil.hh>

namespace pinloki
{
PinlokiSession::PinlokiSession(MXS_SESSION* pSession)
    : mxs::RouterSession(pSession)
{
}

void PinlokiSession::close()
{
}

int32_t PinlokiSession::routeQuery(GWBUF* pPacket)
{
    const mxs::ReplyRoute down;
    const mxs::Reply reply;
    mxs::RouterSession::clientReply(modutil_create_ok(), down, reply);
    return 0;
}

void PinlokiSession::clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxs::RouterSession::clientReply(pPacket, down, reply);
}

bool PinlokiSession::handleError(mxs::ErrorType type, GWBUF* pMessage,
                                 mxs::Endpoint* pProblem, const mxs::Reply& pReply)
{
    return false;
}
}
