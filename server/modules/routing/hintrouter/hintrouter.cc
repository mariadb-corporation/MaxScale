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


HintRouter::HintRouter(SERVICE* pService)
    : maxscale::Router<HintRouter, HintRouterSession>(pService)
{
    MXS_NOTICE("Hint router [%s] created.", pService->name);
}


//static
HintRouter* HintRouter::create(SERVICE* pService, char** pzOptions)
{
    return new HintRouter(pService);
}


HintRouterSession* HintRouter::newSession(MXS_SESSION *pSession)
{
    return new HintRouterSession(pSession, this);
}

void HintRouter::diagnostics(DCB* pOut)
{
}

uint64_t HintRouter::getCapabilities()
{
    return 0;
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
