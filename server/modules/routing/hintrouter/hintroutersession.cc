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

#define MXS_MODULE_NAME "hintrouter"
#include "hintroutersession.hh"
#include <maxscale/log_manager.h>


HintRouterSession::HintRouterSession(MXS_SESSION* pSession, HintRouter* pRouter)
    : maxscale::RouterSession(pSession)
    , m_pRouter(pRouter)
{
}


HintRouterSession::~HintRouterSession()
{
}


void HintRouterSession::close()
{
}


int32_t HintRouterSession::routeQuery(GWBUF* pPacket)
{
    MXS_ERROR("routeQuery not implemented yet.");
    return 0;
}


void HintRouterSession::clientReply(GWBUF* pPacket, DCB* pBackend)
{
    MXS_ERROR("clientReply not implemented yet.");
}


void HintRouterSession::handleError(GWBUF*             pMessage,
                                    DCB*               pProblem,
                                    mxs_error_action_t action,
                                    bool*              pSuccess)
{
    ss_dassert(pProblem->dcb_role == DCB_ROLE_BACKEND_HANDLER);
    MXS_ERROR("handleError not implemented yet.");
}
