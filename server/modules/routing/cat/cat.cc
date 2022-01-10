/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "cat.hh"
#include "catsession.hh"

using namespace maxscale;

Cat::Cat(SERVICE* pService)
    : Router<Cat, CatSession>(pService)
{
}

Cat::~Cat()
{
}

Cat* Cat::create(SERVICE* pService, mxs::ConfigParameters* params)
{
    return new Cat(pService);
}

CatSession* Cat::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
    auto backends = RWBackend::from_endpoints(endpoints);
    bool connected = false;

    for (const auto& a : backends)
    {
        if (a->can_connect() && a->connect())
        {
            connected = true;
        }
    }

    return connected ? new CatSession(pSession, this, std::move(backends)) : NULL;
}

json_t* Cat::diagnostics() const
{
    return NULL;
}

const uint64_t caps = RCAP_TYPE_PACKET_OUTPUT | RCAP_TYPE_CONTIGUOUS_OUTPUT | RCAP_TYPE_STMT_INPUT;

uint64_t Cat::getCapabilities()
{
    return caps;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        "Resultset concatenation router",
        "V1.0.0",
        caps,
        &Cat::s_object,
        NULL,   /* Process init. */
        NULL,   /* Process finish. */
        NULL,   /* Thread init. */
        NULL,   /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
