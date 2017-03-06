#pragma once
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

#include "hintrouterdefs.hh"
#include <deque>
#include <maxscale/router.hh>
#include "dcb.hh"

class HintRouter;

class HintRouterSession : public maxscale::RouterSession
{
public:
    typedef std::deque<Dcb> Backends;

    HintRouterSession(MXS_SESSION*    pSession,
                      HintRouter*     pRouter,
                      const Backends& backends);

    ~HintRouterSession();

    void close();

    int32_t routeQuery(GWBUF* pPacket);

    void clientReply(GWBUF* pPacket, DCB* pBackend);

    void handleError(GWBUF*             pMessage,
                     DCB*               pProblem,
                     mxs_error_action_t action,
                     bool*              pSuccess);

private:
    HintRouterSession(const HintRouterSession&);
    HintRouterSession& operator = (const HintRouterSession&);

private:
    HintRouter* m_pRouter;
    Backends    m_backends;
    size_t      m_surplus_replies;

};
