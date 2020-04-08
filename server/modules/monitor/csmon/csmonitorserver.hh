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
        static Status create(const mxb::http::Result& response);

        Status(Status&& other) = default;
        Status& operator=(Status&& rhs) = default;

        bool is_valid() const
        {
            return response.ok() && sJson;
        }

        mxb::http::Result       response;
        cs::ClusterMode         cluster_mode;
        cs::DbrmMode            dbrm_mode;
        std::unique_ptr<json_t> sJson;

    private:
        Status(const mxb::http::Result& response,
               cs::ClusterMode cluster_mode,
               cs::DbrmMode dbrm_mode,
               std::unique_ptr<json_t>&& sJson)
            : response(response)
            , cluster_mode(cluster_mode)
            , dbrm_mode(dbrm_mode)
            , sJson(std::move(sJson))
        {
        }
    };

    using Statuses = std::pair<size_t, std::vector<Status>>;

    const char* name() const
    {
        return this->server->name();
    }

    json_t* config() const
    {
        return m_sConfig.get();
    }

    bool ping(json_t** ppError = nullptr);

    bool refresh_config(json_t** ppError = nullptr);
    Status fetch_status() const;

    bool set_config(const std::string& body, json_t** ppError = nullptr);

    bool update(cs::ClusterMode mode, json_t** ppError = nullptr);

    static Statuses fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                   const mxb::http::Config& config);
    static size_t shutdown(const std::vector<CsMonitorServer*>& servers,
                           const std::chrono::seconds& timeout,
                           const mxb::http::Config& config,
                           json_t** ppArray);
    static size_t start(const std::vector<CsMonitorServer*>& servers,
                        const mxb::http::Config& config,
                        json_t** ppArray);
    static bool update(const std::vector<CsMonitorServer*>& servers,
                       cs::ClusterMode mode,
                       const mxb::http::Config& config,
                       json_t** ppError = nullptr);

private:
    bool set_status(const mxb::http::Result& result, json_t** ppError);

    std::string create_url(cs::rest::Action action, const std::string& tail = std::string()) const;
    static std::vector<std::string> create_urls(const std::vector<CsMonitorServer*>& servers,
                                                cs::rest::Action action,
                                                const std::string& tail = std::string());

private:
    int64_t                  m_admin_port;
    const mxb::http::Config& m_http_config;
    std::unique_ptr<json_t>  m_sConfig;
    std::unique_ptr<xmlDoc>  m_sDoc;
};
