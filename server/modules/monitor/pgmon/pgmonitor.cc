/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "pgmon"

#include "pgmonitor.hh"
#include "pgserver.hh"

namespace
{
namespace cfg = mxs::config;
cfg::Specification s_spec(MXB_MODULE_NAME, cfg::Specification::MONITOR);
}

PgMonitor* PgMonitor::create(const std::string& name, const std::string& module)
{
    return new PgMonitor(name, module);
}

json_t* PgMonitor::diagnostics() const
{
    json_t* rval = Monitor::diagnostics();
    return rval;
}

json_t* PgMonitor::diagnostics(mxs::MonitorServer* server) const
{
    json_t* rval = Monitor::diagnostics(server);
    return rval;
}

mxs::config::Configuration& PgMonitor::configuration()
{
    return m_config;
}

void PgMonitor::update_server_status(mxs::MonitorServer* monitored_server)
{
    auto mon_server = static_cast<PgServer*>(monitored_server);
}

void PgMonitor::pre_tick()
{
}

void PgMonitor::post_tick()
{
}

bool PgMonitor::can_be_disabled(const mxs::MonitorServer& server, maxscale::Monitor::DisableType type,
                                std::string* errmsg_out) const
{
    // If the server is the master, it cannot be drained. It can be set to maintenance, though.
    bool rval = true;
    if (type == DisableType::DRAIN && status_is_master(server.server->status()))
    {
        rval = false,
        *errmsg_out = "The server is master, so it cannot be set to draining mode.";
    }
    return rval;
}

PgMonitor::PgMonitor(const std::string& name, const std::string& module)
    : SimpleMonitor(name, module)
    , m_config(name, this)
{
}

bool PgMonitor::post_configure()
{
    return true;
}

std::string PgMonitor::permission_test_query() const
{
    return "";
}

void PgMonitor::configured_servers_updated(const std::vector<SERVER*>& servers)
{
    for (auto srv : m_servers)
    {
        delete srv;
    }

    auto& shared_settings = settings().shared;
    m_servers.resize(servers.size());
    for (size_t i = 0; i < servers.size(); i++)
    {
        m_servers[i] = new PgServer(servers[i], shared_settings);
    }

    // The configured servers and the active servers are the same.
    set_active_servers(std::vector<mxs::MonitorServer*>(m_servers.begin(), m_servers.end()));
}

void PgMonitor::pre_loop()
{
    m_master = nullptr;
    SimpleMonitor::pre_loop();
}

PgMonitor::Config::Config(const std::string& name, PgMonitor* monitor)
    : mxs::config::Configuration(name, &s_spec)
    , m_monitor(*monitor)
{
}

bool PgMonitor::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    return m_monitor.post_configure();
}


extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::MONITOR,
        mxs::ModuleStatus::GA,
        MXS_MONITOR_VERSION,
        "PostGreSQL monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<PgMonitor>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        &s_spec
    };

    return &info;
}
