/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include "smartrouter.hh"
#include "smartsession.hh"

#include <maxscale/modutil.hh>

/**
 * The module entry point.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Initialise smartrouter module.");

    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_GA,
        MXS_ROUTER_VERSION,
        "Provides routing for the Smart Query feature",
        "V1.0.0",
        RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_CONTIGUOUS_INPUT | RCAP_TYPE_CONTIGUOUS_OUTPUT,
        &SmartRouter::s_object,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

bool SmartRouter::configure(MXS_CONFIG_PARAMETER* pParams)
{
    // TODO ensure Servers are internal ones. Later TODO call routers directly.
    return true;
}

SERVICE* SmartRouter::service() const
{
    return m_pService;
}

SmartRouter::SmartRouter(SERVICE* service)
    : mxs::Router<SmartRouter, SmartRouterSession>(service)
{
}

SmartRouterSession* SmartRouter::newSession(MXS_SESSION* pSession)
{
    SmartRouterSession* pRouter = nullptr;
    MXS_EXCEPTION_GUARD(pRouter = SmartRouterSession::create(this, pSession));
    return pRouter;
}

// static
SmartRouter* SmartRouter::create(SERVICE* pService, MXS_CONFIG_PARAMETER* pParams)
{
    SmartRouter* pRouter = new(std::nothrow) SmartRouter(pService);

    if (pRouter && !pRouter->configure(pParams))
    {
        delete pRouter;
        pRouter = nullptr;
    }

    return pRouter;
}

void SmartRouter::diagnostics(DCB* pDcb)
{
}

json_t* SmartRouter::diagnostics_json() const
{
    json_t* pJson = json_object();

    return pJson;
}

uint64_t SmartRouter::getCapabilities()
{
    return RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_CONTIGUOUS_INPUT | RCAP_TYPE_CONTIGUOUS_OUTPUT;
}
