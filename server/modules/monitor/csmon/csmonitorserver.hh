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
#include <maxscale/jansson.hh>
#include "columnstore.hh"

class CsMonitorServer : public maxscale::MonitorServer
{
public:
    CsMonitorServer(const CsMonitorServer&) = delete;
    CsMonitorServer& operator=(const CsMonitorServer&) = delete;

    CsMonitorServer(SERVER* pServer,
                    const SharedSettings& shared,
                    int64_t admin_port,
                    mxb::http::Config* pConfig);
    virtual ~CsMonitorServer();

    class Status
    {
    public:
        explicit operator bool () const
        {
            return this->valid;
        }

        bool                    valid        { false };
        cs::ClusterMode         cluster_mode { cs::READ_ONLY };
        cs::DbrmMode            dbrm_mode    { cs::SLAVE };
        std::unique_ptr<json_t> sJson;
    };

    const char* name() const
    {
        return this->server->name();
    }

    json_t* config() const
    {
        return m_sConfig.get();
    }

    const Status& status() const
    {
        return m_status;
    }

    bool ping(json_t** ppError = nullptr);

    bool refresh_config(json_t** ppError = nullptr);
    bool refresh_status(json_t** ppError = nullptr);

    bool set_config(const std::string& body, json_t** ppError = nullptr);
    bool set_status(const std::string& body, json_t** ppError = nullptr);

    bool update(cs::ClusterMode mode, json_t** ppError = nullptr);

    static bool refresh_status(const std::vector<CsMonitorServer*>& servers,
                               const mxb::http::Config& config,
                               json_t** ppError = nullptr);
    static bool shutdown(const std::vector<CsMonitorServer*>& servers,
                         const std::chrono::seconds& timeout,
                         const mxb::http::Config& config,
                         json_t** ppOutput);
    static bool start(const std::vector<CsMonitorServer*>& servers,
                      const mxb::http::Config& config,
                      json_t** ppOutput);
    static bool update(const std::vector<CsMonitorServer*>& servers,
                       cs::ClusterMode mode,
                       const mxb::http::Config& config,
                       json_t** ppError = nullptr);

private:
    bool set_status(const mxb::http::Result& result, json_t** ppError);

    std::string create_url(cs::rest::Action action, const std::string& tail = std::string());
    static std::vector<std::string> create_urls(const std::vector<CsMonitorServer*>& servers,
                                                cs::rest::Action action,
                                                const std::string& tail = std::string());

private:
    int64_t                  m_admin_port;
    const mxb::http::Config& m_http_config;
    std::unique_ptr<json_t>  m_sConfig;
    std::unique_ptr<xmlDoc>  m_sDoc;
    Status                   m_status;
};
