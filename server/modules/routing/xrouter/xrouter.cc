/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xrouter.hh"
#include "xroutersession.hh"
#include <maxscale/service.hh>

namespace
{
mxs::config::Specification s_spec(MXB_MODULE_NAME, mxs::config::Specification::ROUTER);

mxs::config::ParamString s_lock_id(
    &s_spec, "lock_id",
    "The lock identifier used with the locking SQL",
    "1679475768", mxs::config::Param::AT_RUNTIME);

mxs::config::ParamSeconds s_retry_timeout(
    &s_spec, "retry_timeout",
    "Time limit for retrying of failing multi-node commands on secondary nodes",
    60s, mxs::config::Param::AT_RUNTIME);

mxs::config::ParamStringList s_retry_sqlstates(
    &s_spec, "retry_sqlstates",
    "The SQLSTATE prefixes that trigger a replay on a secondary node",
    ",",
    {"HV", "HW"}, mxs::config::Param::AT_RUNTIME);
}

XRouter::Config::Config(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::m_v, &Values::lock_id, &s_lock_id);
    add_native(&Config::m_v, &Values::retry_timeout, &s_retry_timeout);
    add_native(&Config::m_v, &Values::retry_sqlstates, &s_retry_sqlstates);
}

XRouter::XRouter(SERVICE& service)
    : m_config(service.name())
    , m_service(service)
{
}

XRouter* XRouter::create(SERVICE* pService)
{
    return new XRouter(*pService);
}

std::shared_ptr<mxs::RouterSession> XRouter::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
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

    std::shared_ptr<mxs::RouterSession> rv;

    if (!backends.empty())
    {
        if (pSession->protocol()->name() == MXS_POSTGRESQL_PROTOCOL_NAME)
        {
            rv = std::make_shared<XgresSession>(pSession, *this, std::move(backends), m_config.m_shared.get_ref());
        }
        else if (pSession->protocol()->name() == MXS_MARIADB_PROTOCOL_NAME)
        {
            rv = std::make_shared<XmSession>(pSession, *this, std::move(backends), m_config.m_shared.get_ref());
        }
        else
        {
            mxb_assert_message(!true, "This should not be called with an invalid protocol");
        }
    }

    return rv;
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
