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

namespace http = mxb::http;
using namespace std;

ClustrixMonitor::ClustrixMonitor(MXS_MONITOR* pMonitor)
    : maxscale::MonitorInstance(pMonitor)
    , m_delayed_http_check_id(0)
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

    m_config.set_cluster_monitor_interval(config_get_integer(pParams, CLUSTER_MONITOR_INTERVAL_NAME));

    MXS_MONITORED_SERVER* pMonitored_server = m_monitor->monitored_servers;

    while (pMonitored_server)
    {
        SERVER* pServer = pMonitored_server->server;

        string url(pServer->address);
        url += ":";
        url += "3581"; // TODO: Possibly make configurable.

        m_health_urls.push_back(url);

        pMonitored_server = pMonitored_server->next;
    }

    return true;
}

void ClustrixMonitor::pre_loop()
{
    m_http = mxb::http::get_async(m_health_urls);

    if (m_http.status() == http::Async::ERROR)
    {
        MXS_WARNING("Could not initiate health check to nodes.");
    }
}

void ClustrixMonitor::tick()
{
    switch (m_http.status())
    {
    case http::Async::PENDING:
        MXS_WARNING("Health check round had not completed when next tick arrived.");
        break;

    case http::Async::ERROR:
        MXS_WARNING("Health check round ended with general error.");
    case http::Async::READY:
        {
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

                MXS_MONITORED_SERVER* pMonitored_server = m_monitor->monitored_servers;

                for (size_t i = 0; i < m_health_urls.size(); ++i)
                {
                    mxb_assert(pMonitored_server);

                    const auto& url = m_health_urls[i];
                    const auto& result = results[i];

                    MXS_INFO("%s: %s", url.c_str(), (result.code == 200) ? "OK" : result.body.c_str());

                    uint64_t bits = 0;

                    if (result.code == 200)
                    {
                        bits |= SERVER_RUNNING;
                    }

                    monitor_set_pending_status(pMonitored_server, bits);
                    pMonitored_server = pMonitored_server->next;
                }
            }
            break;

        case http::Async::ERROR:
            MXS_ERROR("Health check waiting ended with general error.");
        }
    }

    return false;
}
