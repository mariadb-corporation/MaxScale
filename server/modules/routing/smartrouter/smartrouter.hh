/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#define MXB_MODULE_NAME "smartrouter"

/**
 * @file Smart Router. Routes queries to the best router for the type of query.
 */

#include "perf_info.hh"
#include "perf_updater.hh"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/router.hh>
#include <maxscale/service.hh>
#include <maxscale/protocol/mariadb/module_names.hh>

#include <future>

namespace config = maxscale::config;

class SmartRouterSession;

/** class Smartrouter. Manages the performance info reads and updates.
 */
class SmartRouter : public mxs::Router
{
public:
    ~SmartRouter();

    static constexpr uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT | RCAP_TYPE_STMT_OUTPUT
        | RCAP_TYPE_QUERY_CLASSIFICATION | RCAP_TYPE_OLD_PROTOCOL;

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
    };

    static SmartRouter* create(SERVICE* pService);

    std::shared_ptr<mxs::RouterSession>
    newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints) override;

    json_t*  diagnostics() const override;
    uint64_t getCapabilities() const override;

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    std::set<std::string> protocols() const override
    {
        return {MXS_MARIADB_PROTOCOL_NAME};
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
};
