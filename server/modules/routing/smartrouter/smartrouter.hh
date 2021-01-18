/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-18
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

#include "performance.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/router.hh>

class SmartRouterSession;

/** class Smartrouter. Contains and manages the performance info.
 */
class SmartRouter : public mxs::Router<SmartRouter, SmartRouterSession>
{
public:
    class Config : public config::Configuration
    {
    public:
        Config(const std::string& name);

        Config(const Config&) = delete;
        Config& operator=(const Config&) = delete;

        static void populate(MXS_MODULE& module);

        bool configure(const MXS_CONFIG_PARAMETER& params);

        SERVER* master() const
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
        config::Server m_master;
        config::Bool   m_persist_performance_data;
    };

    static SmartRouter* create(SERVICE* pService, MXS_CONFIG_PARAMETER* pParams);

    SmartRouterSession* newSession(MXS_SESSION* pSession);

    void     diagnostics(DCB* pDcb);
    json_t*  diagnostics_json() const;
    uint64_t getCapabilities();
    bool     configure(MXS_CONFIG_PARAMETER* pParams);

    SERVICE* service() const;

    const Config& config() const
    {
        return m_config;
    }

    /** Thread safe find a PerformanceInfo. Some entry expiration handling is done here.
     */
    PerformanceInfo perf_find(const std::string& canonical);

    /** Thread safe update/insert a PerformanceInfo. Some entry expiration handling is done here.
     */
    void perf_update(const std::string& canonical, const PerformanceInfo& perf);

private:
    SmartRouter(SERVICE* service);

    Config m_config;

    using Perfs = std::unordered_map<std::string, PerformanceInfo>;

    std::mutex m_perf_mutex;
    Perfs      m_perfs;
};
