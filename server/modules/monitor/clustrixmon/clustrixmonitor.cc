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

#include "clustrixmonitor.hh"
#include <algorithm>
#include <set>
#include "../../../core/internal/config_runtime.hh"

namespace http = mxb::http;
using namespace std;

namespace
{

const int DEFAULT_MYSQL_PORT = 3306;
const int DEFAULT_HEALTH_PORT = 3581;

}

ClustrixMonitor::ClustrixMonitor(const string& name, const string& module)
    : MonitorWorker(name, module)
{
}

ClustrixMonitor::~ClustrixMonitor()
{
}

//static
ClustrixMonitor* ClustrixMonitor::create(const string& name, const string& module)
{
    return new ClustrixMonitor(name, module);
}

bool ClustrixMonitor::configure(const MXS_CONFIG_PARAMETER* pParams)
{
    m_health_urls.clear();
    m_nodes.clear();

    m_config.set_cluster_monitor_interval(config_get_integer(pParams, CLUSTER_MONITOR_INTERVAL_NAME));
    m_config.set_health_check_threshold(config_get_integer(pParams, HEALTH_CHECK_THRESHOLD_NAME));

    check_hub_and_refresh_nodes();

    return true;
}

void ClustrixMonitor::pre_loop()
{
    make_health_check();
}

void ClustrixMonitor::post_loop()
{
    if (m_pHub_con)
    {
        mysql_close(m_pHub_con);
    }

    m_pHub_con = nullptr;
    m_pHub_server = nullptr;
}

void ClustrixMonitor::tick()
{
    if (now() - m_last_cluster_check > m_config.cluster_monitor_interval())
    {
        check_hub_and_refresh_nodes();
    }

    switch (m_http.status())
    {
    case http::Async::PENDING:
        MXS_WARNING("Health check round had not completed when next tick arrived.");
        break;

    case http::Async::ERROR:
        MXS_WARNING("Health check round ended with general error.");
	make_health_check();
	break;

    case http::Async::READY:
        update_server_statuses();

        if (!m_health_urls.empty())
        {
	    make_health_check();
	}
	break;
    }
}

void ClustrixMonitor::choose_hub()
{
    mxb_assert(!m_pHub_con);

    SERVER* pHub_server = nullptr;
    MYSQL* pHub_con = nullptr;

    set<string> ips;

    // First we check the dynamic servers, in case there are.
    for (auto it = m_nodes.begin(); !pHub_con && (it != m_nodes.end()); ++it)
    {
        auto& element = *it;
        ClustrixNode& node = element.second;

        if (node.can_be_used_as_hub(*this))
        {
            pHub_con = node.release_connection();
            pHub_server = node.server();
        }

        ips.insert(node.ip());
    }

    if (!pHub_con)
    {
        // If that fails, then we check the bootstrap servers, but only if
        // it was not checked above.

        for (auto it = m_servers.begin(); !pHub_con && (it != m_servers.end()); ++it)
        {
            MXS_MONITORED_SERVER& ms = **it;

            if (ips.find(ms.server->address) == ips.end())
            {
                if (Clustrix::ping_or_connect_to_hub(*this, ms))
                {
                    pHub_con = ms.con;
                    pHub_server = ms.server;
                }
                else if (ms.con)
                {
                    mysql_close(ms.con);
                }

                ms.con = nullptr;
            }
        }
    }

    if (pHub_con)
    {
        MXS_NOTICE("Monitoring Clustrix cluster state using node %s:%d.",
                   pHub_server->address, pHub_server->port);

        m_pHub_con = pHub_con;
        m_pHub_server = pHub_server;

        mxb_assert(m_pHub_con);
        mxb_assert(m_pHub_con);
    }
    else
    {
        MXS_ERROR("Could not connect to any server or no server that could "
                  "be connected to was part of the quorum.");
    }
}

void ClustrixMonitor::refresh_nodes()
{
    mxb_assert(m_pHub_con);

    map<int, ClustrixMembership> memberships;

    if (check_cluster_membership(&memberships))
    {
        const char ZQUERY[] = "SELECT nodeid, iface_ip, mysql_port, healthmon_port FROM system.nodeinfo";

        if (mysql_query(m_pHub_con, ZQUERY) == 0)
        {
            MYSQL_RES* pResult = mysql_store_result(m_pHub_con);

            if (pResult)
            {
                mxb_assert(mysql_field_count(m_pHub_con) == 4);

                set<int> nids;
                for (const auto& element : m_nodes)
                {
                    const ClustrixNode& node = element.second;
                    nids.insert(node.id());
                }

                MYSQL_ROW row;
                while ((row = mysql_fetch_row(pResult)) != nullptr)
                {
                    if (row[0] && row[1])
                    {
                        int id = atoi(row[0]);
                        string ip = row[1];
                        int mysql_port = row[2] ? atoi(row[2]) : DEFAULT_MYSQL_PORT;
                        int health_port = row[3] ? atoi(row[3]) : DEFAULT_HEALTH_PORT;

                        // '@@' ensures no clash with user created servers.
                        // Monitor name ensures no clash with other Clustrix monitor instances.
                        string name = string("@@") + this->name + ":server-" + std::to_string(id);

                        auto nit = m_nodes.find(id);
                        auto mit = memberships.find(id);

                        if (nit != m_nodes.end())
                        {
                            // Existing node.
                            mxb_assert(SERVER::find_by_unique_name(name));

                            ClustrixNode& node = nit->second;

                            if (node.ip() != ip)
                            {
                                node.set_ip(ip);
                            }

                            if (node.mysql_port() != mysql_port)
                            {
                                node.set_mysql_port(mysql_port);
                            }

                            if (node.health_port() != health_port)
                            {
                                node.set_health_port(health_port);
                            }

                            nids.erase(id);
                        }
                        else if (mit != memberships.end())
                        {
                            // New node.
                            mxb_assert(!SERVER::find_by_unique_name(name));

                            if (runtime_create_server(name.c_str(),
                                                      ip.c_str(),
                                                      std::to_string(mysql_port).c_str(),
                                                      "mariadbbackend",
                                                      "mysqlbackendauth",
                                                      false))
                            {
                                SERVER* pServer = SERVER::find_by_unique_name(name);
                                mxb_assert(pServer);

                                const ClustrixMembership& membership = mit->second;
                                int health_check_threshold = m_config.health_check_threshold();

                                ClustrixNode node(membership, ip, mysql_port, health_port,
                                                  health_check_threshold, pServer);

                                m_nodes.insert(make_pair(id, node));
                            }
                            else
                            {
                                MXS_ERROR("Could not create server %s at %s:%d.",
                                          name.c_str(), ip.c_str(), mysql_port);
                            }

                            memberships.erase(mit);
                        }
                        else
                        {
                            // Node found in system.node_info but not in system.membership
                            MXS_ERROR("Node %d at %s:%d,%d found in system.node_info "
                                      "but not in system.membership.",
                                      id, ip.c_str(), mysql_port, health_port);
                        }
                    }
                    else
                    {
                        MXS_WARNING("Either nodeid and/or iface_ip is missing, ignoring node.");
                    }
                }

                mysql_free_result(pResult);

                for (const auto nid : nids)
                {
                    auto it = m_nodes.find(nid);
                    mxb_assert(it != m_nodes.end());

                    ClustrixNode& node = it->second;
                    node.set_running(false, ClustrixNode::APPROACH_OVERRIDE);
                }

                vector<string> health_urls;
                for (const auto& element : m_nodes)
                {
                    const ClustrixNode& node = element.second;
                    string url = "http://" + node.ip() + ":" + std::to_string(node.health_port());

                    health_urls.push_back(url);
                }

                m_health_urls.swap(health_urls);

                m_last_cluster_check = now();
            }
            else
            {
                MXS_WARNING("No result returned for '%s' on %s.", ZQUERY, m_pHub_server->address);
            }
        }
        else
        {
            MXS_ERROR("Could not execute '%s' on %s: %s",
                      ZQUERY, m_pHub_server->address, mysql_error(m_pHub_con));
        }
    }
}

void ClustrixMonitor::check_hub_and_refresh_nodes()
{
    if (m_pHub_con)
    {
        check_hub();
    }

    if (!m_pHub_con)
    {
        choose_hub();
    }

    if (m_pHub_con)
    {
        refresh_nodes();
    }
}

void ClustrixMonitor::check_hub()
{
    mxb_assert(m_pHub_con);
    mxb_assert(m_pHub_server);

    if (!Clustrix::ping_or_connect_to_hub(*this, *m_pHub_server, &m_pHub_con))
    {
        mysql_close(m_pHub_con);
        m_pHub_con = nullptr;
    }
}

bool ClustrixMonitor::check_cluster_membership(std::map<int, ClustrixMembership>* pMemberships)
{
    mxb_assert(pMemberships);

    mxb_assert(m_pHub_con);
    mxb_assert(m_pHub_server);

    bool rv = false;

    const char ZQUERY[] = "SELECT nid, status, instance, substate FROM system.membership";

    if (mysql_query(m_pHub_con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(m_pHub_con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(m_pHub_con) == 4);

            set<int> nids;
            for (const auto& element : m_nodes)
            {
                const ClustrixNode& node = element.second;
                nids.insert(node.id());
            }

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(pResult)) != nullptr)
            {
                if (row[0])
                {
                    int nid = atoi(row[0]);
                    string status = row[1] ? row[1] : "unknown";
                    int instance = row[2] ? atoi(row[2]) : -1;
                    string substate = row[3] ? row[3] : "unknown";

                    auto it = m_nodes.find(nid);

                    if (it != m_nodes.end())
                    {
                        ClustrixNode& node = it->second;

                        node.update(Clustrix::status_from_string(status),
                                    Clustrix::substate_from_string(substate),
                                    instance);

                        nids.erase(node.id());
                    }
                    else
                    {
                        ClustrixMembership membership(nid,
                                                      Clustrix::status_from_string(status),
                                                      Clustrix::substate_from_string(substate),
                                                      instance);

                        pMemberships->insert(make_pair(nid, membership));
                    }
                }
                else
                {
                    MXS_WARNING("No node id returned in row for '%s'.", ZQUERY);
                }
            }

            mysql_free_result(pResult);

            // Deactivate all servers that are no longer members.
            for (const auto nid : nids)
            {
                auto it = m_nodes.find(nid);
                mxb_assert(it != m_nodes.end());

                ClustrixNode& node = it->second;
                node.deactivate_server();
                m_nodes.erase(it);
            }

            rv = true;
        }
        else
        {
            MXS_WARNING("No result returned for '%s'.", ZQUERY);
        }
    }
    else
    {
        MXS_ERROR("Could not execute '%s' on %s: %s",
		  ZQUERY, m_pHub_server->address, mysql_error(m_pHub_con));
    }

    return rv;
}

void ClustrixMonitor::update_server_statuses()
{
    mxb_assert(!m_servers.empty());

    for (auto ms : m_servers)
    {
        monitor_stash_current_status(ms);

        auto it = find_if(m_nodes.begin(), m_nodes.end(),
                          [ms](const std::pair<int,ClustrixNode>& element) -> bool {
                              const ClustrixNode& info = element.second;
                              return ms->server->address == info.ip();
                          });

        if (it != m_nodes.end())
        {
            const ClustrixNode& info = it->second;

            if (info.is_running())
            {
                monitor_set_pending_status(ms, SERVER_RUNNING);
            }
            else
            {
                monitor_clear_pending_status(ms, SERVER_RUNNING);
            }
        }
        else
        {
            monitor_clear_pending_status(ms, SERVER_RUNNING);
        }
    }
}

void ClustrixMonitor::make_health_check()
{
    mxb_assert(m_http.status() != http::Async::PENDING);

    m_http = mxb::http::get_async(m_health_urls);

    switch (m_http.status())
    {
    case http::Async::PENDING:
	initiate_delayed_http_check();
	break;

    case http::Async::ERROR:
	MXS_ERROR("Could not initiate health check.");
	break;

    case http::Async::READY:
	MXS_NOTICE("Health check available immediately.");
	break;
    }
}

void ClustrixMonitor::initiate_delayed_http_check()
{
    mxb_assert(m_delayed_http_check_id == 0);

    long max_delay_ms = m_settings.interval / 10;

    long ms = m_http.wait_no_more_than();

    if (ms > max_delay_ms)
    {
        ms = max_delay_ms;
    }

    m_delayed_http_check_id = delayed_call(ms, &ClustrixMonitor::check_http, this);
}

bool ClustrixMonitor::check_http(Call::action_t action)
{
    m_delayed_http_check_id = 0;

    if (action == Call::EXECUTE)
    {
        switch (m_http.perform())
        {
        case http::Async::PENDING:
            initiate_delayed_http_check();
            break;

        case http::Async::READY:
            {
                const vector<http::Result>& results = m_http.results();

                auto it = m_nodes.begin();

                for_each(results.begin(), results.end(),
                         [&it](const http::Result& result) {
                             bool running = false;

                             if (result.code == 200)
                             {
                                 running = true;
                             }

                             auto& node_info = it->second;

                             node_info.set_running(running);

                             ++it;
                         });
            }
            break;

        case http::Async::ERROR:
            MXS_ERROR("Health check waiting ended with general error.");
        }
    }

    return false;
}
