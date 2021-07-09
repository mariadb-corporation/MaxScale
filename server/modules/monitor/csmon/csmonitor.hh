/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <chrono>
#include <map>
#include <sqlite3.h>
#include <maxbase/http.hh>
#include <maxbase/semaphore.hh>
#include <maxbase/stopwatch.hh>
#include <maxscale/modulecmd.hh>
#include "columnstore.hh"
#include "cscontext.hh"
#include "csmonitorserver.hh"

class CsMonitor : public maxscale::MonitorWorkerSimple,
                  private CsDynamicServer::Persister
{
public:
    using Base = mxs::MonitorWorkerSimple;

    CsMonitor(const CsMonitor&) = delete;
    CsMonitor& operator=(const CsMonitor&) = delete;

    ~CsMonitor();
    static CsMonitor* create(const std::string& name, const std::string& module);

    bool is_dynamic() const override;

public:
    using ServerVector = std::vector<CsMonitorServer*>;

    const CsContext& context() const
    {
        return m_context;
    }

    ServerVector get_monitored_serverlist(const std::string& key, bool* error_out)
    {
        const auto& sl = Base::get_monitored_serverlist(key, error_out);

        return reinterpret_cast<const ServerVector&>(sl);
    }

    CsMonitorServer* get_monitored_server(SERVER* search_server);
    CsDynamicServer* get_dynamic_server(const SERVER* pServer) const;

    const ServerVector& servers() const
    {
        return reinterpret_cast<const ServerVector&>(Base::servers());
    }

    // Only to be called by the module call command mechanism.
    bool command_add_node(json_t** ppOutput, const std::string& host, const std::chrono::seconds& timeout);
    bool command_config_get(json_t** ppOutput, CsMonitorServer* pServer);
    bool command_mode_set(json_t** ppOutput, const char* zEnum, const std::chrono::seconds& timeout);
    bool command_remove_node(json_t** ppOutput,
                             const std::string& host, const std::chrono::seconds& timeout);
    bool command_shutdown(json_t** ppOutput, const std::chrono::seconds& timeout);
    bool command_start(json_t** ppOutput, const std::chrono::seconds& timeout);
    bool command_status(json_t** ppOutput, CsMonitorServer* pServer);

#if defined(CSMON_EXPOSE_TRANSACTIONS)
    bool command_begin(json_t** ppOutput, const std::chrono::seconds& timeout, CsMonitorServer* pServer);
    bool command_commit(json_t** ppOutput, const std::chrono::seconds& timeout, CsMonitorServer* pServer);
    bool command_rollback(json_t** ppOutput, CsMonitorServer* pServer);
#endif


private:
    // CsDynamicServer::Persister
    void persist(const CsDynamicServer& node) override final;
    void unpersist(const CsDynamicServer& node) override final;

    void remove_dynamic_host(const std::string& host);

private:
    bool ready_to_run(json_t** ppOutput) const;
    static bool is_valid_json(json_t** ppOutput, const char* zJson, size_t len);

    bool command(json_t** ppOutput, mxb::Semaphore& sem, const char* zCmd, std::function<void()> cmd);

    void cs_add_node(json_t** ppOutput, mxb::Semaphore* pSem,
                     const std::string& host, const std::chrono::seconds& timeout);
    void cs_config_get(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer);
    void cs_mode_set(json_t** ppOuput, mxb::Semaphore* pSem,
                     cs::ClusterMode mode, const std::chrono::seconds& timeout);
    void cs_remove_node(json_t** ppOutput, mxb::Semaphore* pSem,
                        const std::string& host, const std::chrono::seconds& timeout);
    void cs_shutdown(json_t** ppOutput, mxb::Semaphore* pSem, const std::chrono::seconds& timeout);
    void cs_start(json_t** ppOutput, mxb::Semaphore* pSem,  const std::chrono::seconds& timeout);
    void cs_status(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer);
#if defined(CSMON_EXPOSE_TRANSACTIONS)
    void cs_begin(json_t** ppOutput, mxb::Semaphore* pSem,
                  const std::chrono::seconds& timeout, CsMonitorServer* pServer);
    void cs_commit(json_t** ppOutput, mxb::Semaphore* pSem,
                   const std::chrono::seconds& timeout, CsMonitorServer* pServer);
    void cs_rollback(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer);
#endif

    bool has_sufficient_permissions() override;
    void update_server_status(mxs::MonitorServer* pServer) override;
    int fetch_status_mask(const CsMonitorServer& server);
    void update_status_of_dynamic_servers();

    CsMonitorServer* create_server(SERVER* server, const mxs::MonitorServer::SharedSettings& shared) override;

protected:
    void populate_services() override;

    void server_added(SERVER* pServer) override;
    void server_removed(SERVER* pServer) override;

private:
    CsMonitor(const std::string& name, const std::string& module, sqlite3* pDb);

    bool configure(const mxs::ConfigParameters* pParams) override;

    void pre_loop() override;
    void pre_tick() override;

    using HostPortPair  = std::pair<std::string, int>;
    using HostPortPairs = std::vector<HostPortPair>;
    using Hosts         = std::set<std::string>;
    using HostsByHost   = std::map<std::string, Hosts>;
    using StatusByHost  = std::map<std::string, cs::Status>;

    void probe_cluster();
    void probe_cluster(const HostPortPairs&);
    void probe_fuzzy_cluster(const HostsByHost& hosts_by_host);
    void adjust_dynamic_servers(const Hosts& hosts);

    bool check_bootstrap_servers();
    bool remove_persisted_information();
    bool persist_bootstrap_servers();

    void populate_from_bootstrap_servers();

    bool fetch_configs(const std::vector<std::string>& hosts, std::vector<cs::Config>* pConfigs);

    bool should_probe_cluster() const;

    std::string create_dynamic_name(const std::string& host) const;

    CsContext                                               m_context;
    std::map<std::string, std::unique_ptr<CsDynamicServer>> m_nodes_by_id;
    sqlite3*                                                m_pDb {nullptr};
    mxb::TimePoint                                          m_last_probe;
    bool                                                    m_probe_cluster { true };
    std::set<std::string>                                   m_obsolete_bootstraps;
};
