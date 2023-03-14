/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xrouter.hh"
#include "xroutersession.hh"

namespace
{
mxs::config::Specification s_spec(MXB_MODULE_NAME, mxs::config::Specification::ROUTER);
}

XRouter::XRouter(const std::string& name)
    : m_config(name, &s_spec)
{
}

XRouter* XRouter::create(SERVICE* pService)
{
    return new XRouter(pService->name());
}

mxs::RouterSession* XRouter::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    SBackends backends;

    for (mxs::Endpoint* e : endpoints)
    {
        if (e->target()->is_connectable())
        {
            if (auto b = std::make_unique<mxs::Backend>(e); b->connect())
            {
                backends.push_back(std::move(b));
            }
        }
    }

    return backends.empty() ? nullptr : new XRouterSession(pSession, this, std::move(backends));
}

json_t* XRouter::diagnostics() const
{
    return nullptr;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        "xrouter",
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::ALPHA,
        MXS_ROUTER_VERSION,
        "Project X Router",
        "V1.0.0",
        XRouter::CAPS,
        &mxs::RouterApi<XRouter>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        &s_spec
    };

    return &info;
}
