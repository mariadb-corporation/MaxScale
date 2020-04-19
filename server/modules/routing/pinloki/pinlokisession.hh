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
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxscale/router.hh>

#include "rpl_event.hh"

namespace pinloki
{

class PinlokiSession : public mxs::RouterSession
{
public:
    PinlokiSession(const PinlokiSession&) = delete;
    PinlokiSession& operator=(const PinlokiSession&) = delete;

    PinlokiSession(MXS_SESSION* pSession);
    void    close();
    int32_t routeQuery(GWBUF* pPacket);
    void    clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    bool    handleError(mxs::ErrorType type, GWBUF* pMessage,
                        mxs::Endpoint* pProblem, const mxs::Reply& pReply);

private:
    uint8_t m_seq = 1;      // Packet sequence number, incremented for each sent packet

    bool send_event(const maxsql::RplEvent& event);
};
}
