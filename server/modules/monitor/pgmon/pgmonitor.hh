/*
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/monitor.hh>

class PgServer;
class PgMonitor final : public maxscale::SimpleMonitor
{
public:
    ~PgMonitor() = default;
    static PgMonitor* create(const std::string& name, const std::string& module);
    json_t*           diagnostics() const override;
    json_t*           diagnostics(mxs::MonitorServer* server) const override;

    mxs::config::Configuration& configuration() override final;

protected:
    void update_server_status(mxs::MonitorServer* monitored_server) override;
    void pre_tick() override;
    void post_tick() override;
    bool can_be_disabled(const mxs::MonitorServer& server, DisableType type,
                         std::string* errmsg_out) const override;

    class Config : public mxs::config::Configuration
    {
    public:
        Config(const std::string& name, PgMonitor* monitor);
        bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;
    private:
        PgMonitor& m_monitor;
    };

private:
    Config m_config;

    std::vector<PgServer*> m_servers;           /**< Active/configured servers */
    PgServer*              m_master {nullptr};  /**< Master server */

    PgMonitor(const std::string& name, const std::string& module);

    bool        post_configure();
    friend bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params);
    std::string permission_test_query() const override;
    void        configured_servers_updated(const std::vector<SERVER*>& servers) override;
    void        pre_loop() override;
};
