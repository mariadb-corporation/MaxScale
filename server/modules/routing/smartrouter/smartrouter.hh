/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
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

#include <future>

class SmartRouterSession;

/** class Smartrouter. Manages the performance info reads and updates.
 */
class SmartRouter : public mxs::Router<SmartRouter, SmartRouterSession>
{
public:
    ~SmartRouter();

    class Config : public config::Configuration
    {
    public:
        Config(const std::string& name);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        static void populate(MXS_MODULE& module);

        bool configure(const MXS_CONFIG_PARAMETER& params);

        mxs::Target* master() const
        {
            return m_master.get();
        }

        bool persist_performance_data() const
        {
            return static_cast<bool>(m_persist_performance_data);
        }

    private:
        bool post_configure(const MXS_CONFIG_PARAMETER& params) override;

    private:
        config::Target m_master;
        config::Bool   m_persist_performance_data;
    };

    static SmartRouter* create(SERVICE* pService, MXS_CONFIG_PARAMETER* pParams);

    SmartRouterSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);

    json_t*  diagnostics() const;
    uint64_t getCapabilities();
    bool     configure(MXS_CONFIG_PARAMETER* pParams);

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

    Config m_config;

    PerformanceInfoUpdater m_updater;
    std::future<void>      m_updater_future;
};
