/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "xpandmon.hh"
#include <map>
#include <set>
#include <sqlite3.h>
#include <maxscale/config2.hh>
#include <maxscale/monitor.hh>
#include <maxbase/http.hh>
#include "xpandmembership.hh"
#include "xpandnode.hh"

class ClustrixMonitor : public maxscale::MonitorWorker
                      , private ClustrixNode::Persister
{
    ClustrixMonitor(const ClustrixMonitor&) = delete;
    ClustrixMonitor& operator=(const ClustrixMonitor&) = delete;
public:
    class Config
    {
    public:
        Config(const std::string& name);

        static void populate(MXS_MODULE& module);

        bool configure(const MXS_CONFIG_PARAMETER& params);

        long cluster_monitor_interval() const
        {
            return m_cluster_monitor_interval.count();
        }

        long health_check_threshold() const
        {
            return m_health_check_threshold.get();
        }

        bool dynamic_node_detection() const
        {
            return static_cast<bool>(m_dynamic_node_detection);
        }

        int health_check_port() const
        {
            return m_health_check_port.get();
        }

    private:
        config::Configuration                       m_configuration;
        config::Duration<std::chrono::milliseconds> m_cluster_monitor_interval;
        config::Count                               m_health_check_threshold;
        config::Bool                                m_dynamic_node_detection;
        config::Integer                             m_health_check_port;
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

    void check_bootstrap_servers();
    bool remove_persisted_information();
    void persist_bootstrap_servers();

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

    void populate_from_bootstrap_servers();

    void update_server_statuses();

    void make_health_check();
    void initiate_delayed_http_check();
    bool check_http(Call::action_t action);
    void update_http_urls();

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
    std::map<int, ClustrixNode> m_nodes_by_id;
    std::vector<std::string>    m_health_urls;
    mxb::http::Async            m_http;
    uint32_t                    m_delayed_http_check_id {0};
    long                        m_last_cluster_check {0};
    SERVER*                     m_pHub_server {nullptr};
    MYSQL*                      m_pHub_con {nullptr};
    sqlite3*                    m_pDb {nullptr};
};
