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

namespace
{

namespace smartrouter
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::ROUTER);

config::ParamServer
master(&specification,
       "master",
       "The server/cluster to be treated as master, that is, the one where updates are sent.");

config::ParamBool
persist_performance_data(&specification,
                         "persist_performance_data",
                         "Persist performance data so that the smartrouter can use information "
                         "collected during earlier runs.",
                         true); // Default value

}

}

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

    SmartRouter::Config::populate(info);

    return &info;
}

SmartRouter::Config::Config(const std::string& name)
    : config::Configuration(name, &smartrouter::specification)
    , m_master(this, &smartrouter::master)
    , m_persist_performance_data(this, &smartrouter::persist_performance_data)
{
}

void SmartRouter::Config::populate(MXS_MODULE& module)
{
    smartrouter::specification.populate(module);
}

bool SmartRouter::Config::configure(const MXS_CONFIG_PARAMETER& params)
{
    return smartrouter::specification.configure(*this, params);
}

bool SmartRouter::Config::post_configure(const MXS_CONFIG_PARAMETER& params)
{
    bool rv = true;

    auto servers = params.get_server_list(CN_SERVERS);

    bool master_found = false;

    for (SERVER* pServer : servers)
    {
        if (pServer == m_master.get())
        {
            master_found = true;
        }

        if (pServer->address[0] != '/')
        {
            if (strcmp(pServer->address, "127.0.0.1") == 0 || strcmp(pServer->address, "localhost"))
            {
                MXS_WARNING("The server %s, used by the smartrouter %s, is currently accessed "
                            "using a TCP/IP socket (%s:%d). For better performance, a Unix "
                            "domain socket should be used. See the 'socket' argument.",
                            pServer->name(), name().c_str(), pServer->address, pServer->port);
            }
        }
    }

    if (rv && !master_found)
    {
        rv = false;

        std::string s;

        for (auto server : servers)
        {
            if (!s.empty())
            {
                s+= ", ";
            }

            s += server->name();
        }

        MXS_ERROR("The master server %s of the smartrouter %s, is not one of the "
                  "servers (%s) of the service.",
                  m_master.get()->name(), name().c_str(), s.c_str());
    }

    return rv;
}

bool SmartRouter::configure(MXS_CONFIG_PARAMETER* pParams)
{
    if (!smartrouter::specification.validate(*pParams))
    {
        return false;
    }

    // Since post_configure() has been overriden, this may fail.
    return m_config.configure(*pParams);
}

SERVICE* SmartRouter::service() const
{
    return m_pService;
}

SmartRouter::SmartRouter(SERVICE* service)
    : mxs::Router<SmartRouter, SmartRouterSession>(service)
    , m_config(service->name())
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
