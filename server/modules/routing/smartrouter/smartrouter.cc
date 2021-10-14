/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "smartrouter.hh"
#include "smartsession.hh"

#include <maxscale/cn_strings.hh>
#include <maxscale/modutil.hh>
#include <maxscale/routingworker.hh>

namespace
{

namespace smartrouter
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::ROUTER);

config::ParamTarget
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
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::GA,
        MXS_ROUTER_VERSION,
        "Provides routing for the Smart Query feature",
        "V1.0.0",
        RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_STMT_INPUT | RCAP_TYPE_STMT_OUTPUT
        | RCAP_TYPE_OLD_PROTOCOL,
        &mxs::RouterApi<SmartRouter>::s_api,
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

SmartRouter::Config::Config(const std::string& name, SmartRouter* router)
    : config::Configuration(name, &smartrouter::specification)
    , m_master(this, &smartrouter::master)
    , m_persist_performance_data(this, &smartrouter::persist_performance_data)
    , m_router(router)
{
}

void SmartRouter::Config::populate(MXS_MODULE& module)
{
    module.specification = &smartrouter::specification;
}

SERVICE* SmartRouter::service() const
{
    return m_service;
}

SmartRouter::SmartRouter(SERVICE* service)
    : m_service(service)
    , m_config(service->name(), this)
{
    using namespace maxscale;
    using namespace maxbase;

    auto shared_ptrs = m_updater.get_shared_data_pointers();

    for (size_t id = 0; id != shared_ptrs.size(); ++id)
    {
        RoutingWorker* pRworker = RoutingWorker::get(id);
        auto pShared = shared_ptrs[id];
        pRworker->execute([pRworker, pShared]() {
                              pRworker->register_epoll_tick_func(std::bind(&SharedPerformanceInfo::
                                                                           reader_ready,
                                                                           pShared));
                          },
                          Worker::EXECUTE_AUTO);
    }

    m_updater.start();
}

SmartRouter::~SmartRouter()
{
    m_updater.stop();
}

mxs::RouterSession* SmartRouter::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    return SmartRouterSession::create(this, pSession, endpoints);
}

// static
SmartRouter* SmartRouter::create(SERVICE* pService)
{
    return new SmartRouter(pService);
}

json_t* SmartRouter::diagnostics() const
{
    json_t* pJson = json_object();

    return pJson;
}

uint64_t SmartRouter::getCapabilities() const
{
    return RCAP_TYPE_TRANSACTION_TRACKING | RCAP_TYPE_STMT_INPUT | RCAP_TYPE_STMT_OUTPUT
           | RCAP_TYPE_QUERY_CLASSIFICATION;
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
    using namespace maxbase;

    auto pShared_data = m_updater.get_shared_data_by_index(mxs_rworker_get_current_id());
    auto sShared_ptr = make_shared_data_ptr(pShared_data);

    auto pContainer = sShared_ptr.get();
    auto perf_it = pContainer->find(canonical);

    PerformanceInfo ret;

    if (perf_it != end(*pContainer))
    {
        if (not perf_it->second.is_updating()
            && perf_it->second.age() > eviction_schedules[perf_it->second.eviction_schedule()])
        {
            PerformanceInfo updt_entry = perf_it->second;

            // Only trigger this worker to re-measure. Since the update is SharedData, multiple
            // workers may still re-measure if they get the same canonical at about the same time.
            // The return value is the default constructed ret.
            updt_entry.set_updating(true);

            MXS_SINFO("Trigger re-measure, schedule "
                      << eviction_schedules[updt_entry.eviction_schedule()]
                      << ", perf: " << updt_entry.target()->name()
                      << ", " << updt_entry.duration() << ", "
                      << show_some(canonical));

            pShared_data->send_update({canonical, updt_entry});
        }
        else
        {
            ret = perf_it->second;
        }
    }

    return ret;
}

void SmartRouter::perf_update(const std::string& canonical, PerformanceInfo perf)
{
    using namespace maxbase;

    auto pShared_data = m_updater.get_shared_data_by_index(mxs_rworker_get_current_id());
    auto sShared_ptr = make_shared_data_ptr(pShared_data);

    auto pContainer = sShared_ptr.get();
    auto perf_it = pContainer->find(canonical);

    if (perf_it != end(*pContainer))
    {
        MXS_SINFO("Update perf: from "
                  << perf_it->second.target()->name() << ", " << perf_it->second.duration()
                  << " to " << perf.target()->name() << ", " << perf.duration()
                  << ", " << show_some(canonical));

        size_t schedule = perf_it->second.eviction_schedule();
        perf.set_eviction_schedule(std::min(++schedule, eviction_schedules.size() - 1));
        perf.set_updating(false);
        pShared_data->send_update({canonical, perf});
    }
    else
    {
        pShared_data->send_update({canonical, perf});
        MXS_SDEBUG("Sent new perf: " << perf.target()->name() << ", " << perf.duration()
                                     << ", " << show_some(canonical));
    }
}
