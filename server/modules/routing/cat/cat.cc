/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "cat.hh"
#include "catsession.hh"

using namespace maxscale;

Cat::Cat(SERVICE* pService):
    Router<Cat, CatSession>(pService)
{
}

Cat::~Cat()
{
}

Cat* Cat::create(SERVICE* pService, char** pzOptions)
{
    return new Cat(pService);
}

CatSession* Cat::newSession(MXS_SESSION* pSession)
{
    auto backends = RWBackend::from_servers(pSession->service->dbref);
    bool connected = false;

    for (auto a = backends.begin(); a != backends.end(); a++)
    {
        if ((*a)->can_connect() && (*a)->connect(pSession))
        {
            connected = true;
        }
    }

    return connected ? new CatSession(pSession, this, backends) : NULL;
}

void Cat::diagnostics(DCB* dcb)
{
}

json_t* Cat::diagnostics_json() const
{
    return NULL;
}

const uint64_t caps = RCAP_TYPE_STMT_OUTPUT | RCAP_TYPE_STMT_INPUT;

uint64_t Cat::getCapabilities()
{
    return caps;
}

MXS_BEGIN_DECLS

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
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
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

MXS_END_DECLS
