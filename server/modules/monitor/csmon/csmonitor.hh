/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "csmon.hh"
#include <maxbase/http.hh>
#include <maxbase/semaphore.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/monitor.hh>
#include "csconfig.hh"

class CsMonitor : public maxscale::MonitorWorkerSimple
{
public:
    class Command;

    CsMonitor(const CsMonitor&) = delete;
    CsMonitor& operator=(const CsMonitor&) = delete;

    ~CsMonitor();
    static CsMonitor* create(const std::string& name, const std::string& module);

public:
    // Only to be called by the module call command mechanism.
    bool command_cluster_start(json_t** ppOutput);
    bool command_cluster_stop(json_t** ppOutput);
    bool command_cluster_shutdown(json_t** ppOutput);
    bool command_cluster_add_node(json_t** ppOutput);
    bool command_cluster_remove_node(json_t** ppOutput);
    bool command_async(const char* zCommand, json_t** ppOutput);
    bool command_result(json_t** ppOutput);
    bool command_cancel(json_t** ppOutput);

private:
    bool command(const char* zCmd, std::function<void()> cmd, mxb::Semaphore& sem, json_t** ppOutput);

    void cluster_put(const char* zCmd, mxb::Semaphore* pSem, json_t** ppOutput);

    void cluster_start(mxb::Semaphore* pSem = nullptr, json_t** ppOutput = nullptr);
    void cluster_stop(mxb::Semaphore* pSem = nullptr, json_t** ppOutput = nullptr);
    void cluster_shutdown(mxb::Semaphore* pSem = nullptr, json_t** ppOutput = nullptr);
    void cluster_add_node(mxb::Semaphore* pSem = nullptr, json_t** ppOutput = nullptr);
    void cluster_remove_node(mxb::Semaphore* pSem = nullptr, json_t** ppOutput = nullptr);

    bool has_sufficient_permissions();
    void update_server_status(mxs::MonitorServer* monitored_server);

private:
    CsMonitor(const std::string& name, const std::string& module);
    bool configure(const mxs::ConfigParameters* pParams) override;

    CsConfig                 m_config;
    std::unique_ptr<Command> m_sCommand;
};
