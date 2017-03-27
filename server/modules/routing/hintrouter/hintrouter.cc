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
#include "hintrouter.hh"
#include <maxscale/log_manager.h>
#include "dcb.hh"


HintRouter::HintRouter(SERVICE* pService)
    : maxscale::Router<HintRouter, HintRouterSession>(pService)
{
    HR_ENTRY();
    MXS_NOTICE("Hint router [%s] created.", pService->name);
}


//static
HintRouter* HintRouter::create(SERVICE* pService, char** pzOptions)
{
    HR_ENTRY();
    return new HintRouter(pService);
}


HintRouterSession* HintRouter::newSession(MXS_SESSION *pSession)
{
    HR_ENTRY();
    HintRouterSession* pRouterSession = NULL;

    HintRouterSession::Backends backends;

    for (SERVER_REF* pSref = m_pService->dbref; pSref; pSref = pSref->next)
    {
        if (SERVER_REF_IS_ACTIVE(pSref))
        {
            SERVER* pServer = pSref->server;

            HR_DEBUG("Connecting to %s.", pServer->name);

            DCB* pDcb = dcb_connect(pServer, pSession, pServer->protocol);

            if (pDcb)
            {
                HR_DEBUG("Connected to %p %s.", pDcb, pServer->name);
                // TODO: What's done here, should be done by dcb_connect().
                atomic_add(&pSref->connections, 1);
                pDcb->service = pSession->service;

                backends.push_back(Dcb(pDcb));
            }
            else
            {
                HR_DEBUG("Failed to connect to %s.", pServer->name);
            }
        }
    }

    if (backends.size() != 0)
    {
        pRouterSession = new HintRouterSession(pSession, this, backends);
    }

    return pRouterSession;
}

void HintRouter::diagnostics(DCB* pOut)
{
    HR_ENTRY();
}

uint64_t HintRouter::getCapabilities()
{
    HR_ENTRY();
    return RCAP_TYPE_NONE;
}


extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE module =
    {
        MXS_MODULE_API_ROUTER,   /* Module type */
        MXS_MODULE_BETA_RELEASE, /* Release status */
        MXS_ROUTER_VERSION,      /* Implemented module API version */
        "A hint router", /* Description */
        "V1.0.0", /* Module version */
        RCAP_TYPE_STMT_OUTPUT,
        &HintRouter::s_object,
        NULL, /* Process init, can be null */
        NULL, /* Process finish, can be null */
        NULL, /* Thread init */
        NULL, /* Thread finish */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &module;
}
