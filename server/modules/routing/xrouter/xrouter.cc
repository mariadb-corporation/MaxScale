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

mxs::config::ParamString s_main_sql(
    &s_spec, "main_sql", "SQL executed on the main node",
    "SET foo.bar = 'main'", mxs::config::Param::AT_RUNTIME);

mxs::config::ParamString s_secondary_sql(
    &s_spec, "secondary_sql", "SQL executed on the secondary nodes",
    "SET foo.bar = 'secondary'", mxs::config::Param::AT_RUNTIME);

mxs::config::ParamString s_lock_sql(
    &s_spec, "lock_sql", "SQL executed to lock a node",
    "SELECT pg_advisory_lock(1679475768)", mxs::config::Param::AT_RUNTIME);

mxs::config::ParamString s_unlock_sql(
    &s_spec, "unlock_sql", "SQL executed to unlock a node",
    "SELECT pg_advisory_unlock(1679475768) ", mxs::config::Param::AT_RUNTIME);

mxs::config::ParamSeconds s_retry_timeout(
    &s_spec, "retry_timeout",
    "Time limit for retrying of failing multi-node commands on secondary nodes",
    60s, mxs::config::Param::AT_RUNTIME);
}

XRouter::Config::Config(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&Config::m_v, &Values::main_sql, &s_main_sql);
    add_native(&Config::m_v, &Values::secondary_sql, &s_secondary_sql);
    add_native(&Config::m_v, &Values::lock_sql, &s_lock_sql);
    add_native(&Config::m_v, &Values::unlock_sql, &s_unlock_sql);
    add_native(&Config::m_v, &Values::retry_timeout, &s_retry_timeout);
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

    mxs::RouterSession* rv = nullptr;

    if (!backends.empty())
    {
        rv = new XRouterSession(pSession, *this, std::move(backends), m_config.m_shared.get_ref());
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
