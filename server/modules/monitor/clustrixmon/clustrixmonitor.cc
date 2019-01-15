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
    m_node_infos.clear();

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

void ClustrixMonitor::fetch_cluster_nodes()
{
    auto b = begin(*(m_monitor->monitored_servers));
    auto e = end(*(m_monitor->monitored_servers));

    auto it = find_if(b, e,
                      [this](MXS_MONITORED_SERVER& ms) -> bool {
                          mxs_connect_result_t rv = mon_ping_or_connect_to_db(m_monitor, &ms);

                          return mon_connection_is_ok(rv) ? true : false;
                      });

    if (it != e)
    {
        MXS_MONITORED_SERVER& ms = *it;
        fetch_cluster_nodes_from(ms);

        m_pMonitored_server = &ms;
    }
    else
    {
        MXS_ERROR("Could not connect to any server.");
    }
}

void ClustrixMonitor::fetch_cluster_nodes_from(MXS_MONITORED_SERVER& ms)
{
    mxb_assert(ms.con);

    const char ZQUERY[] = "SELECT nodeid, iface_ip, mysql_port, healthmon_port FROM system.nodeinfo";

    if (mysql_query(ms.con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(ms.con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(ms.con) == 4);

            vector<ClustrixNodeInfo> node_infos;
            vector<string> health_urls;

            MYSQL_ROW row;

            while ((row = mysql_fetch_row(pResult)) != nullptr)
            {
                if (row[0] && row[1])
                {
                    int id = atoi(row[0]);
                    string ip = row[1];
                    int mysql_port = row[2] ? atoi(row[2]) : DEFAULT_MYSQL_PORT;
                    int health_port = row[3] ? atoi(row[3]) : DEFAULT_HEALTH_PORT;
                    int health_check_threshold = m_config.health_check_threshold();

                    node_infos.emplace_back(id, ip, mysql_port, health_port, health_check_threshold);

                    string health_url = "http://" + ip + ":" + std::to_string(health_port);
                    health_urls.push_back(health_url);
                }
                else
                {
                    MXS_WARNING("Either nodeid and/or iface_ip is missing, "
                                "ignoring node.");
                }
            }

            mysql_free_result(pResult);

            m_node_infos.swap(node_infos);
            m_health_urls.swap(health_urls);

            m_last_cluster_check = now();
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
}

void ClustrixMonitor::refresh_cluster_nodes()
{
    if (m_pMonitored_server)
    {
        mxs_connect_result_t rv = mon_ping_or_connect_to_db(m_monitor, m_pMonitored_server);

        if (mon_connection_is_ok(rv))
        {
            fetch_cluster_nodes_from(*m_pMonitored_server);
        }
        else
        {
            mysql_close(m_pMonitored_server->con);
            m_pMonitored_server->con = nullptr;

            fetch_cluster_nodes();
        }
    }
    else if (m_monitor->monitored_servers)
    {
        fetch_cluster_nodes();
    }
}

void ClustrixMonitor::update_server_statuses()
{
    mxb_assert(m_monitor->monitored_servers);

    auto b = std::begin(*m_monitor->monitored_servers);
    auto e = std::end(*m_monitor->monitored_servers);

    for_each(b, e,
             [this](MXS_MONITORED_SERVER& ms) {
                 monitor_stash_current_status(&ms);

                 auto it = find_if(m_node_infos.begin(), m_node_infos.end(),
                                   [&ms](const ClustrixNodeInfo& info) -> bool {
                                       return ms.server->address == info.ip();
                                   });

                 if (it != m_node_infos.end())
                 {
                     if (it->is_running())
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

                for (size_t i = 0; i < results.size(); ++i)
                {
                    const auto& result = results[i];

                    bool running = false;

                    if (result.code == 200)
                    {
		        running = true;
                    }

		    auto& node_info = m_node_infos[i];

		    node_info.set_running(running);
                }
            }
            break;

        case http::Async::ERROR:
            MXS_ERROR("Health check waiting ended with general error.");
        }
    }

    return false;
}
