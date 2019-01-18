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
#include <map>
#include <maxscale/monitor.hh>
#include <maxbase/http.hh>
#include "clustrixmembership.hh"
#include "clustrixnode.hh"

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
            , m_health_check_threshold(DEFAULT_HEALTH_CHECK_THRESHOLD_VALUE)
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

        long health_check_threshold() const
        {
            return m_health_check_threshold;
        }

        void set_health_check_threshold(long l)
        {
            m_health_check_threshold = l;
        }

    private:
        long m_cluster_monitor_interval;
        long m_health_check_threshold;
    };

    ~ClustrixMonitor();

    static ClustrixMonitor* create();

    bool configure(const MXS_CONFIG_PARAMETER* pParams) override;

private:
    ClustrixMonitor();

    void pre_loop() override;
    void post_loop() override;

    void tick();

    void update_cluster_nodes();
    void update_cluster_nodes(MXS_MONITORED_SERVER& ms);
    void refresh_cluster_nodes();
    bool check_cluster_membership(MXS_MONITORED_SERVER& ms, std::map<int, ClustrixMembership>* pMemberships);
    void update_server_statuses();

    void make_health_check();
    void initiate_delayed_http_check();
    bool check_http(Call::action_t action);

    static long now()
    {
        return mxb::WorkerLoad::get_time_ms();
    }

private:
    Config                      m_config;
    std::map<int, ClustrixNode> m_nodes;
    std::vector<std::string>    m_health_urls;
    mxb::http::Async            m_http;
    uint32_t                    m_delayed_http_check_id { 0 };
    long                        m_last_cluster_check    { 0 };
    MXS_MONITORED_SERVER*       m_pMonitored_server     { nullptr };
};
