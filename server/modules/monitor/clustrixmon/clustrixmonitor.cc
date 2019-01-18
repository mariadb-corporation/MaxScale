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

ClustrixMonitor::ClustrixMonitor(MXS_MONITOR* pMonitor)
    : maxscale::MonitorInstance(pMonitor)
{
}

ClustrixMonitor::~ClustrixMonitor()
{
}

//static
ClustrixMonitor* ClustrixMonitor::create(MXS_MONITOR* pMonitor)
{
    return new ClustrixMonitor(pMonitor);
}

bool ClustrixMonitor::configure(const MXS_CONFIG_PARAMETER* pParams)
{
    m_health_urls.clear();
    m_nodes.clear();

    m_config.set_cluster_monitor_interval(config_get_integer(pParams, CLUSTER_MONITOR_INTERVAL_NAME));
    m_config.set_health_check_threshold(config_get_integer(pParams, HEALTH_CHECK_THRESHOLD_NAME));

    refresh_cluster_nodes();

    return true;
}

void ClustrixMonitor::pre_loop()
{
    make_health_check();
}

void ClustrixMonitor::post_loop()
{
    if (m_pMonitored_server && m_pMonitored_server->con)
    {
        mysql_close(m_pMonitored_server->con);
        m_pMonitored_server->con = nullptr;
    }
}

void ClustrixMonitor::tick()
{
    if (now() - m_last_cluster_check > m_config.cluster_monitor_interval())
    {
        refresh_cluster_nodes();
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
        if (m_monitor->monitored_servers)
        {
            update_server_statuses();
	    make_health_check();
	}
	break;
    }
}

namespace
{

bool is_part_of_the_quorum(const MXS_MONITORED_SERVER& ms)
{
    bool rv = false;

    const char ZQUERY_TEMPLATE[] =
        "SELECT ms.status FROM system.membership AS ms INNER JOIN system.nodeinfo AS ni "
        "ON ni.nodeid = ms.nid WHERE ni.iface_ip = '%s'";

    const char* zAddress = ms.server->address;
    char zQuery[sizeof(ZQUERY_TEMPLATE) + strlen(zAddress)];

    sprintf(zQuery, ZQUERY_TEMPLATE, zAddress);

    if (mysql_query(ms.con, zQuery) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(ms.con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(ms.con) == 1);

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(pResult)) != nullptr)
            {
                if (row[0])
                {
                    Clustrix::Status status = Clustrix::status_from_string(row[0]);

                    switch (status)
                    {
                    case Clustrix::Status::QUORUM:
                        rv = true;
                        break;

                    case Clustrix::Status::STATIC:
                        MXS_NOTICE("Node %s is not part of the quorum, switching to "
                                   "other node for monitoring.", zAddress);
                        break;

                    case Clustrix::Status::UNKNOWN:
                        MXS_WARNING("Do not know how to interpret '%s'. Assuming node %s "
                                    "is not part of the quorum.", row[0], zAddress);
                    }
                }
                else
                {
                    MXS_WARNING("No status returned for '%s' on %s.", zQuery, zAddress);
                }
            }

            mysql_free_result(pResult);
        }
        else
        {
            MXS_WARNING("No result returned for '%s' on %s.", zQuery, zAddress);
        }
    }
    else
    {
        MXS_ERROR("Could not execute '%s' on %s: %s", zQuery, zAddress, mysql_error(ms.con));
    }

    return rv;
}

}

void ClustrixMonitor::update_cluster_nodes()
{
    auto b = begin(*(m_monitor->monitored_servers));
    auto e = end(*(m_monitor->monitored_servers));

    auto it = find_if(b, e,
                      [this](MXS_MONITORED_SERVER& ms) -> bool {
                          mxs_connect_result_t rv = mon_ping_or_connect_to_db(m_monitor, &ms);

                          bool usable = false;

                          if (mon_connection_is_ok(rv) && is_part_of_the_quorum(ms))
                          {
                              usable = true;
                          }

                          return usable;
                      });

    if (it != e)
    {
        MXS_MONITORED_SERVER& ms = *it;

        if (!m_pMonitored_server)
        {
            MXS_NOTICE("Monitoring Clustrix cluster state using node %s.", ms.server->address);
        }
        else if (m_pMonitored_server != &ms)
        {
            MXS_NOTICE("Monitoring Clustrix cluster state using %s (used to be %s).",
                       ms.server->address, m_pMonitored_server->server->address);
        }

        m_pMonitored_server = &ms;

        update_cluster_nodes(*m_pMonitored_server);

    }
    else
    {
        MXS_ERROR("Could not connect to any server.");
    }
}

void ClustrixMonitor::update_cluster_nodes(MXS_MONITORED_SERVER& ms)
{
    mxb_assert(ms.con);

    map<int, ClustrixMembership> memberships;

    check_cluster_membership(ms, &memberships);

    const char ZQUERY[] = "SELECT nodeid, iface_ip, mysql_port, healthmon_port FROM system.nodeinfo";

    if (mysql_query(ms.con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(ms.con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(ms.con) == 4);

            set<int> nids;
            for_each(m_nodes.begin(), m_nodes.end(),
                     [&nids](const pair<int, ClustrixNode>& element) {
                         nids.insert(element.first);
                     });

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(pResult)) != nullptr)
            {
                if (row[0] && row[1])
                {
                    int id = atoi(row[0]);
                    string ip = row[1];
                    int mysql_port = row[2] ? atoi(row[2]) : DEFAULT_MYSQL_PORT;
                    int health_port = row[3] ? atoi(row[3]) : DEFAULT_HEALTH_PORT;

                    string name = "@Clustrix-Server-" + std::to_string(id);

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

                            ClustrixNode node(membership, ip, mysql_port, health_port, health_check_threshold, pServer);

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

	    for_each(nids.begin(), nids.end(),
		     [this](int nid) {
		       auto it = m_nodes.find(nid);
		       mxb_assert(it != m_nodes.end());

		       ClustrixNode& node = it->second;
                       node.set_running(false, ClustrixNode::APPROACH_OVERRIDE);
		     });

            vector<string> health_urls;
            for_each(m_nodes.begin(), m_nodes.end(),
                     [&health_urls](const pair<int, ClustrixNode>& element) {
                         const ClustrixNode& node = element.second;
                         string url = "http://" + node.ip() + ":" + std::to_string(node.health_port());

                         health_urls.push_back(url);
                     });

            m_health_urls.swap(health_urls);

            m_last_cluster_check = now();
        }
        else
        {
            MXS_WARNING("No result returned for '%s' on %s.", ZQUERY, ms.server->address);
        }
    }
    else
    {
        MXS_ERROR("Could not execute '%s' on %s: %s",
		  ZQUERY, ms.server->address, mysql_error(ms.con));
    }
}

void ClustrixMonitor::refresh_cluster_nodes()
{
    if (m_pMonitored_server)
    {
        mxs_connect_result_t rv = mon_ping_or_connect_to_db(m_monitor, m_pMonitored_server);

        if (mon_connection_is_ok(rv))
        {
            update_cluster_nodes(*m_pMonitored_server);
        }
        else
        {
            mysql_close(m_pMonitored_server->con);
            m_pMonitored_server->con = nullptr;

            update_cluster_nodes();
        }
    }
    else if (m_monitor->monitored_servers)
    {
        update_cluster_nodes();
    }
}

bool ClustrixMonitor::check_cluster_membership(MXS_MONITORED_SERVER& ms,
                                               std::map<int, ClustrixMembership>* pMemberships)
{
    mxb_assert(ms.con);
    mxb_assert(pMemberships);

    bool rv = false;

    const char ZQUERY[] = "SELECT nid, status, instance, substate FROM system.membership";

    if (mysql_query(ms.con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(ms.con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(ms.con) == 4);

            set<int> nids;
            for_each(m_nodes.begin(), m_nodes.end(),
                     [&nids](const pair<int, ClustrixNode>& element) {
                         nids.insert(element.first);
                     });

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
	    for_each(nids.begin(), nids.end(),
		     [this](int nid) {
		       auto it = m_nodes.find(nid);
		       mxb_assert(it != m_nodes.end());

		       ClustrixNode& node = it->second;
		       node.deactivate_server();
		       m_nodes.erase(it);
		     });

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
		  ZQUERY, ms.server->address, mysql_error(ms.con));
    }

    return rv;
}

void ClustrixMonitor::update_server_statuses()
{
    mxb_assert(m_monitor->monitored_servers);

    auto b = std::begin(*m_monitor->monitored_servers);
    auto e = std::end(*m_monitor->monitored_servers);

    for_each(b, e,
             [this](MXS_MONITORED_SERVER& ms) {
                 monitor_stash_current_status(&ms);

                 auto it = find_if(m_nodes.begin(), m_nodes.end(),
                                   [&ms](const std::pair<int,ClustrixNode>& element) -> bool {
                                       const ClustrixNode& info = element.second;
                                       return ms.server->address == info.ip();
                                   });

                 if (it != m_nodes.end())
                 {
                     const ClustrixNode& info = it->second;

                     if (info.is_running())
                     {
                         monitor_set_pending_status(&ms, SERVER_RUNNING);
                     }
                     else
                     {
                         monitor_clear_pending_status(&ms, SERVER_RUNNING);
                     }
                 }
                 else
                 {
                     monitor_clear_pending_status(&ms, SERVER_RUNNING);
                 }

             });
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

    long max_delay_ms = m_monitor->interval / 10;

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
