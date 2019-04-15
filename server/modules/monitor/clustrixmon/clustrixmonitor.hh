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
#include <set>
#include <sqlite3.h>
#include <maxscale/monitor.hh>
#include <maxbase/http.hh>
#include "clustrixmembership.hh"
#include "clustrixnode.hh"

class ClustrixMonitor : public maxscale::MonitorWorker,
                        private ClustrixNode::Persister
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
            , m_dynamic_node_detection(DEFAULT_DYNAMIC_NODE_DETECTION_VALUE)
            , m_health_check_port(DEFAULT_HEALTH_CHECK_PORT_VALUE)
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

        bool dynamic_node_detection() const
        {
            return m_dynamic_node_detection;
        }

        void set_dynamic_node_detection(bool b)
        {
            m_dynamic_node_detection = b;
        }

        int health_check_port() const
        {
            return m_health_check_port;
        }

        void set_health_check_port(int p)
        {
            m_health_check_port = p;
        }

    private:
        long m_cluster_monitor_interval;
        long m_health_check_threshold;
        bool m_dynamic_node_detection;
        int  m_health_check_port;
    };

    ~ClustrixMonitor();

    static ClustrixMonitor* create(const std::string& name, const std::string& module);

    bool configure(const MXS_CONFIG_PARAMETER* pParams) override;

    bool softfail(SERVER* pServer, json_t** ppError);
    bool unsoftfail(SERVER* pServer, json_t** ppError);

protected:
    void populate_services() override;

    void server_added(SERVER* pServer) override;
    void server_removed(SERVER* pServer) override;

private:
    ClustrixMonitor(const std::string& name,
                    const std::string& module,
                    sqlite3* pDb);

    void pre_loop() override;
    void post_loop() override;

    void tick() override;

    void check_cluster(Clustrix::Softfailed softfailed);
    void check_hub(Clustrix::Softfailed softfailed);
    void choose_hub(Clustrix::Softfailed softfailed);

    bool choose_dynamic_hub(Clustrix::Softfailed softfailed, std::set<std::string>& ips_checked);
    bool choose_bootstrap_hub(Clustrix::Softfailed softfailed, std::set<std::string>& ips_checked);
    bool refresh_using_persisted_nodes(std::set<std::string>& ips_checked);

    bool refresh_nodes();
    bool refresh_nodes(MYSQL* pHub_con);
    bool check_cluster_membership(MYSQL* pHub_con,
                                  std::map<int, ClustrixMembership>* pMemberships);

    void update_server_statuses();

    void make_health_check();
    void initiate_delayed_http_check();
    bool check_http(Call::action_t action);

    bool perform_softfail(SERVER* pServer, json_t** ppError);
    bool perform_unsoftfail(SERVER* pServer, json_t** ppError);

    enum class Operation
    {
        SOFTFAIL,
        UNSOFTFAIL,
    };

    bool perform_operation(Operation operation,
                           SERVER* pServer,
                           json_t** ppError);


    bool should_check_cluster() const
    {
        return now() - m_last_cluster_check > m_config.cluster_monitor_interval();
    }

    void trigger_cluster_check()
    {
        m_last_cluster_check = 0;
    }

    void cluster_checked()
    {
        m_last_cluster_check = now();
    }

    static long now()
    {
        return mxb::WorkerLoad::get_time_ms();
    }

    // ClustrixNode::Persister
    void persist(const ClustrixNode& node);
    void unpersist(const ClustrixNode& node);

private:
    Config                      m_config;
    std::map<int, ClustrixNode> m_nodes;
    std::vector<std::string>    m_health_urls;
    mxb::http::Async            m_http;
    uint32_t                    m_delayed_http_check_id { 0 };
    long                        m_last_cluster_check    { 0 };
    SERVER*                     m_pHub_server           { nullptr };
    MYSQL*                      m_pHub_con              { nullptr };
    sqlite3*                    m_pDb                   { nullptr };
};
