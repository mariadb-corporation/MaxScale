/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXS_MODULE_NAME "smartrouter"

/**
 * @file Smart Router. Routes queries to the best router for the type of query.
 */

#include "perf_info.hh"
#include "perf_updater.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>

#include <future>

namespace config = maxscale::config;

class SmartRouterSession;

/** class Smartrouter. Manages the performance info reads and updates.
 */
class SmartRouter : public mxs::Router
{
public:
    ~SmartRouter();

    class Config : public config::Configuration
    {
    public:
        Config(const std::string& name, SmartRouter* router);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        static void populate(MXS_MODULE& module);

        mxs::Target* master() const
        {
            return m_master.get();
        }

        bool persist_performance_data() const
        {
            return m_persist_performance_data.get();
        }

    private:
        config::Target m_master;
        config::Bool   m_persist_performance_data;
        SmartRouter*   m_router;
    };

    static SmartRouter* create(SERVICE* pService);

    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;

    json_t*  diagnostics() const override;
    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    SERVICE* service() const;

    const Config& config() const
    {
        return m_config;
    }

    /** Find a PerformanceInfo, if not found returns a default initialized PerformanceInfo.
     */
    PerformanceInfo perf_find(const std::string& canonical);

    /** Update SharedData
     */
    void perf_update(const std::string& canonical, PerformanceInfo perf);

private:
    SmartRouter(SERVICE* service);

    SERVICE* m_service;
    Config   m_config;

    PerformanceInfoUpdater m_updater;
    std::future<void>      m_updater_future;
};
