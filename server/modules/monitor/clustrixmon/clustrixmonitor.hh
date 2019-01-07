/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include "clustrixmon.hh"
#include <maxscale/monitor.hh>
#include <maxbase/http.hh>

class ClustrixMonitor : public maxscale::MonitorInstance
{
    ClustrixMonitor(const ClustrixMonitor&) = delete;
    ClustrixMonitor& operator=(const ClustrixMonitor&) = delete;
public:
    class Config
    {
    public:
        Config()
            : m_cluster_monitor_interval(DEFAULT_CLUSTER_MONITOR_INTERVAL_VALUE)
        {
        };

        long cluster_monitor_interval() const
        {
            return m_cluster_monitor_interval;
        }

        void set_cluster_monitor_interval(long l)
        {
            m_cluster_monitor_interval = l;
        }

    private:
        long m_cluster_monitor_interval;
    };

    static ClustrixMonitor* create(MXS_MONITOR* pMonitor);

    bool configure(const MXS_CONFIG_PARAMETER* pParams) override;

private:
    ClustrixMonitor(MXS_MONITOR* pMonitor);

    void pre_loop() override;

    void tick();

    void initiate_delayed_http_check();
    bool check_http(Call::action_t action);

private:
    Config                   m_config;
    mxb::http::Async         m_http;
    std::vector<std::string> m_health_urls;
    uint32_t                 m_delayed_http_check_id;
};
