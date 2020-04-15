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

        bool ok() const
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

    class Config
    {
    public:
        static Config create(const mxb::http::Result& response);

        Config(Config&& other) = default;
        Config& operator=(Config&& rhs) = default;

        bool ok() const
        {
            return response.ok() && sJson && sXml;
        }

        mxb::http::Result       response;
        std::unique_ptr<json_t> sJson;
        std::unique_ptr<xmlDoc> sXml;

    private:
        Config(const mxb::http::Result& response,
               std::unique_ptr<json_t>&& sJson,
               std::unique_ptr<xmlDoc>&& sXml)
            : response(response)
            , sJson(std::move(sJson))
            , sXml(std::move(sXml))
        {
        }
    };

    using Statuses    = std::pair<size_t, std::vector<Status>>;
    using Configs     = std::pair<size_t, std::vector<Config>>;
    using HttpResults = std::vector<mxb::http::Result>;

    const char* name() const
    {
        return this->server->name();
    }

    bool ping(json_t** ppError = nullptr);

    Config fetch_config() const;
    Status fetch_status() const;

    enum TrxState
    {
        TRX_ACTIVE,
        TRX_INACTIVE
    };

    TrxState trx_state() const
    {
        return m_trx_state;
    }

    bool in_trx() const
    {
        return m_trx_state == TRX_ACTIVE;
    }

    mxb::http::Result begin(const std::chrono::seconds& timeout, const std::string& id);
    mxb::http::Result rollback();
    mxb::http::Result commit();

    bool set_mode(cs::ClusterMode mode, json_t** ppError = nullptr);

    static Statuses fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                   const mxb::http::Config& config);
    static Configs fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                 const mxb::http::Config& config);

    static HttpResults begin(const std::vector<CsMonitorServer*>& servers,
                             const std::chrono::seconds& timeout,
                             const std::string& id,
                             const mxb::http::Config& config);
    static HttpResults commit(const std::vector<CsMonitorServer*>& servers,
                             const mxb::http::Config& config);
    static HttpResults rollback(const std::vector<CsMonitorServer*>& servers,
                                const mxb::http::Config& config);
    static HttpResults shutdown(const std::vector<CsMonitorServer*>& servers,
                                const std::chrono::seconds& timeout,
                                const mxb::http::Config& config);
    static HttpResults start(const std::vector<CsMonitorServer*>& servers,
                             const mxb::http::Config& config);
    static bool set_mode(const std::vector<CsMonitorServer*>& servers,
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
    TrxState                 m_trx_state = TRX_INACTIVE;
};
