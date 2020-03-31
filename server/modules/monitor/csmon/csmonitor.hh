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
    bool command_cluster_start(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_start_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_shutdown(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_shutdown_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_ping(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_ping_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_status(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_status_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_config_get(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_config_get_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_config_set(json_t** ppOutput, const char* zJson, SERVER* pServer);
    bool command_cluster_config_set_async(json_t** ppOutput, const char* zJson, SERVER* pServer);
    bool command_cluster_mode_set(json_t** ppOutput, const char* zEnum);
    bool command_cluster_mode_set_async(json_t** ppOutput, const char* zEnum);

    bool command_cluster_add_node(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_add_node_async(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_remove_node(json_t** ppOutput, SERVER* pServer);
    bool command_cluster_remove_node_async(json_t** ppOutput, SERVER* pServer);

    bool command_result(json_t** ppOutput);
    bool command_cancel(json_t** ppOutput);

private:
    enum Mode
    {
        READ_ONLY,
        READ_WRITE
    };

    static bool from_string(const char* zEnum, Mode* pMode);
    static const char* to_string(Mode mode);

    bool ready_to_run(json_t** ppOutput) const;
    static bool is_valid_json(json_t** ppOutput, const char* zJson, size_t len);

    bool command(json_t** ppOutput, mxb::Semaphore& sem, const char* zCmd, std::function<void()> cmd);

    void cluster_get(json_t** ppOutput, mxb::Semaphore* pSem, const char* zCmd, SERVER* pServer);
    void cluster_put(json_t** ppOutput, mxb::Semaphore* pSem, const char* zCmd,
                     SERVER* pServer, std::string&& body = std::string());

    void cluster_start(json_t** ppOutput, mxb::Semaphore* pSem = nullptr, SERVER* pServer = nullptr);
    void cluster_shutdown(json_t** ppOutput, mxb::Semaphore* pSem = nullptr, SERVER* pServer = nullptr);
    void cluster_ping(json_t** ppOutput, mxb::Semaphore* pSem = nullptr, SERVER* pServer = nullptr);
    void cluster_status(json_t** ppOutput, mxb::Semaphore* pSem = nullptr, SERVER* pServer = nullptr);
    void cluster_config_get(json_t** ppOutput, mxb::Semaphore* pSem = nullptr, SERVER* pServer = nullptr);
    void cluster_config_set(json_t** ppOutput, mxb::Semaphore* pSem,
                            std::string&& body, SERVER* pServer = nullptr);
    void cluster_mode_set(json_t** ppOuput, mxb::Semaphore* pSem, Mode mode);
    void cluster_add_node(json_t** ppOutput, mxb::Semaphore* pSem, SERVER* pServer);
    void cluster_remove_node(json_t** ppOutput, mxb::Semaphore* pSem, SERVER* pServer);

    bool has_sufficient_permissions();
    void update_server_status(mxs::MonitorServer* monitored_server);

private:
    CsMonitor(const std::string& name, const std::string& module);
    bool configure(const mxs::ConfigParameters* pParams) override;

    CsConfig                 m_config;
    std::unique_ptr<Command> m_sCommand;
};
