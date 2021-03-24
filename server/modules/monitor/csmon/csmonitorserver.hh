/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
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

class CsContext;

class CsMonitorServer : public maxscale::MonitorServer
{
public:
    CsMonitorServer(const CsMonitorServer&) = delete;
    CsMonitorServer& operator=(const CsMonitorServer&) = delete;

    CsMonitorServer(SERVER* pServer,
                    const SharedSettings& shared,
                    CsContext* pCs_context);
    virtual ~CsMonitorServer();

    class Result
    {
    public:
        Result() {}
        Result(const mxb::http::Response& response);

        Result(Result&& other) = default;
        Result& operator=(Result&& rhs) = default;

        bool ok() const
        {
            return response.is_success() && sJson;
        }

        mxb::http::Response     response;
        std::unique_ptr<json_t> sJson;
    };

    class Status : public Result
    {
    public:
        Status(const mxb::http::Response& response);

        Status(Status&& other) = default;
        Status& operator=(Status&& rhs) = default;

        cs::ClusterMode      cluster_mode = cs::READONLY;
        cs::DbrmMode         dbrm_mode = cs::SLAVE;
        cs::DbRootIdVector   dbroots;
        cs::ServiceVector    services;
        std::chrono::seconds uptime;

    private:
        static int64_t s_uptime;
    };

    class Config : public Result
    {
    public:
        Config(const mxb::http::Response& response);

        Config(Config&& other) = default;
        Config& operator=(Config&& rhs) = default;

        bool ok() const
        {
            return Result::ok() && sXml;
        }

        bool get_dbrm_controller_ip(std::string* pIp, json_t* pOutput = nullptr) const
        {
            return get_value(cs::xml::DBRM_CONTROLLER, cs::xml::IPADDR, pIp, pOutput);
        }

        bool get_ddlproc_ip(std::string* pIp, json_t* pOutput = nullptr) const
        {
            return get_value(cs::xml::DDLPROC, cs::xml::IPADDR, pIp, pOutput);
        }

        bool get_dmlproc_ip(std::string* pIp, json_t* pOutput = nullptr) const
        {
            return get_value(cs::xml::DMLPROC, cs::xml::IPADDR, pIp, pOutput);
        }

        using time_point = std::chrono::system_clock::time_point;

        time_point              timestamp;
        std::unique_ptr<xmlDoc> sXml;

    private:
        bool get_value(const char* zElement_name,
                       const char* zValue_name,
                       std::string* pIp,
                       json_t* pOutput) const;
    };

    using Response  = mxb::http::Response;
    using Responses = mxb::http::Responses;
    using Results   = std::vector<Result>;
    using Statuses  = std::vector<Status>;
    using Configs   = std::vector<Config>;

    const char* name() const
    {
        return this->server->name();
    }

    const char* address() const
    {
        return this->server->address();
    }

    cs::Version minor_version() const
    {
        return m_minor_version;
    }

    int version_number() const
    {
        return m_version_number;
    }

    void set_version_number(int vn)
    {
        if (vn >= 10500)
        {
            m_minor_version = cs::CS_15;
        }
        else if (vn >= 10200)
        {
            m_minor_version = cs::CS_12;
        }
        else if (vn > 10000)
        {
            m_minor_version = cs::CS_10;
        }
        else
        {
            m_minor_version = cs::CS_UNKNOWN;
        }

        m_version_number = vn;
    }

    void set_status(uint64_t bit)
    {
        this->server->set_status(bit);
    }

    Config fetch_config() const;
    Status fetch_status() const;

    enum NodeMode
    {
        MULTI_NODE,  // The server is configured for a multi-node cluster.
        SINGLE_NODE, // The server is not configured for a multi-node cluster.
        UNKNOWN_MODE // We do not know or care what the server is configured for.
    };

    NodeMode node_mode() const
    {
        return m_node_mode;
    }

    void set_node_mode(NodeMode node_mode)
    {
        m_node_mode = node_mode;
    }

    bool set_node_mode(const Config& config, json_t* pOutput);

    bool is_multi_node() const
    {
        return m_node_mode == MULTI_NODE;
    }

    bool is_single_node() const
    {
        return m_node_mode == SINGLE_NODE;
    }

    bool is_unknown_mode() const
    {
        return m_node_mode == UNKNOWN_MODE;
    }

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

    Result begin(const std::chrono::seconds& timeout, json_t* pOutput = nullptr);
    Result commit(const std::chrono::seconds& timeout, json_t* pOutput = nullptr);
    Result rollback(json_t* pOutput = nullptr);

    bool set_cluster_mode(cs::ClusterMode mode,
                          const std::chrono::seconds& timeout,
                          json_t* pOutput = nullptr);

    static Result fetch_status(const std::vector<CsMonitorServer*>& servers,
                               CsContext& context);
    static Statuses fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                   CsContext& context);
    static bool fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                               CsContext& context,
                               Statuses* pStatuses);

    static Result fetch_config(const std::vector<CsMonitorServer*>& servers,
                               CsContext& context);
    static Configs fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                 CsContext& context);
    static bool fetch_configs(const std::vector<CsMonitorServer*>& servers,
                              CsContext& context,
                              Configs* pConfigs);

    static Result add_node(const std::vector<CsMonitorServer*>& servers,
                           const std::string& host,
                           const std::chrono::seconds& timeout,
                           CsContext& context);
    static Results begin(const std::vector<CsMonitorServer*>& servers,
                         const std::chrono::seconds& timeout,
                         CsContext& context);
    static bool begin(const std::vector<CsMonitorServer*>& servers,
                      const std::chrono::seconds& timeout,
                      CsContext& context,
                      Results* pResults);
    static Results commit(const std::vector<CsMonitorServer*>& servers,
                          const std::chrono::seconds& timeout,
                          CsContext& context);
    static bool commit(const std::vector<CsMonitorServer*>& servers,
                       const std::chrono::seconds& timeout,
                       CsContext& context,
                       Results* pResults);
    static Result remove_node(const std::vector<CsMonitorServer*>& servers,
                              const std::string& host,
                              const std::chrono::seconds& timeout,
                              CsContext& context);
    static Results rollback(const std::vector<CsMonitorServer*>& servers,
                            CsContext& context);
    static bool rollback(const std::vector<CsMonitorServer*>& servers,
                         CsContext& context,
                         Results* pResults);
    static Result shutdown(const std::vector<CsMonitorServer*>& servers,
                           const std::chrono::seconds& timeout,
                           CsContext& context);
    static Result start(const std::vector<CsMonitorServer*>& servers,
                        const std::chrono::seconds& timeout,
                        CsContext& context);
    static bool set_cluster_mode(const std::vector<CsMonitorServer*>& servers,
                                 cs::ClusterMode mode,
                                 const std::chrono::seconds& timeout,
                                 CsContext& context,
                                 json_t* pOutput = nullptr);
    static CsMonitorServer* get_master(const std::vector<CsMonitorServer*>& servers,
                                       CsContext& context,
                                       json_t* pOutput = nullptr);

private:
    bool set_status(const mxb::http::Response& response, json_t** ppError);

    std::string create_url(cs::rest::Scope scope,
                           cs::rest::Action action,
                           const std::string& tail = std::string()) const;
    static std::vector<std::string> create_urls(const std::vector<CsMonitorServer*>& servers,
                                                cs::rest::Scope scope,
                                                cs::rest::Action action,
                                                const std::string& tail = std::string());

private:
    NodeMode    m_node_mode = UNKNOWN_MODE;
    CsContext&  m_context;
    TrxState    m_trx_state = TRX_INACTIVE;
    cs::Version m_minor_version = cs::CS_UNKNOWN;
    int         m_version_number = -1;
};
