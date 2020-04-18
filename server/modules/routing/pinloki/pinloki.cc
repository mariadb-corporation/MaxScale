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

#include "pinloki.hh"
#include "pinlokisession.hh"

namespace pinloki
{

Pinloki::Pinloki(SERVICE* pService)
    : Router<Pinloki, PinlokiSession>(pService)
    // , m_config(config) // TODO: Pass configuration parameters to Config
    , m_inventory(m_config)
{
}
// static
Pinloki* Pinloki::create(SERVICE* pService, mxs::ConfigParameters* pParams)
{
    return new Pinloki(pService);
}

PinlokiSession* Pinloki::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
    return new PinlokiSession(pSession);
}

json_t* Pinloki::diagnostics() const
{
    return nullptr;
}

uint64_t Pinloki::getCapabilities()
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

bool Pinloki::configure(mxs::ConfigParameters* pParams)
{
    return true;
}
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        "Pinloki",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &pinloki::Pinloki::s_object,
        NULL,
        NULL,
        NULL,
        NULL
    };

    return &info;
}
