/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
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
                             true);     // Default value
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
            if (strcmp(pServer->address, "127.0.0.1") == 0 || strcmp(pServer->address, "localhost") == 0)
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
                s += ", ";
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

// Eviction schedule
// Two reasons to evict, and re-measure canonicals.
//   1. When connections are initially created there is more overhead in maxscale and at the server,
//      which can (and does) lead to the wrong performance conclusions.
//   2. Depending on the contents and number of rows in tables, different database engines
//      have different performance advantages (InnoDb is always very fast for small tables).
//
// TODO make configurable, maybe.
static std::array<maxbase::Duration, 4> eviction_schedules =
{
    std::chrono::minutes(2),
    std::chrono::minutes(5),
    std::chrono::minutes(10),
    std::chrono::minutes(20)
};

// TODO need to add the default db to the key (or hash)

PerformanceInfo SmartRouter::perf_find(const std::string& canonical)
{
    std::unique_lock<std::mutex> guard(m_perf_mutex);

    auto perf_it = m_perfs.find(canonical);
    if (perf_it != end(m_perfs) && !perf_it->second.is_updating())
    {
        if (perf_it->second.age() > eviction_schedules[perf_it->second.eviction_schedule()])
        {
            MXS_SINFO("Trigger re-measure, schedule "
                      << eviction_schedules[perf_it->second.eviction_schedule()]
                      << ", perf: " << perf_it->second.host()
                      << ", " << perf_it->second.duration() << ", "
                      << show_some(canonical));

            // Not actually evicting, but trigger a re-measure only for this caller (this worker).
            perf_it->second.set_updating(true);
            perf_it = end(m_perfs);
        }
    }

    return perf_it != end(m_perfs) ? perf_it->second : PerformanceInfo();
}

void SmartRouter::perf_update(const std::string& canonical, const PerformanceInfo& perf)
{
    std::unique_lock<std::mutex> guard(m_perf_mutex);

    auto perf_it = m_perfs.find(canonical);
    if (perf_it != end(m_perfs))
    {
        MXS_SINFO("Update perf: from "
                  << perf_it->second.host() << ", " << perf_it->second.duration()
                  << " to " << perf.host() << ", " << perf.duration()
                  << ", " << show_some(canonical));

        size_t schedule = perf_it->second.eviction_schedule();
        perf_it->second = perf;
        perf_it->second.set_eviction_schedule(std::min(++schedule, eviction_schedules.size() - 1));
        perf_it->second.set_updating(false);
    }
    else
    {
        m_perfs.insert({canonical, perf});
        MXS_SDEBUG("Stored new perf: " << perf.host() << ", " << perf.duration()
                                       << ", " << show_some(canonical));
    }
}
