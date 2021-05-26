/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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

    virtual ~CsMonitorServer();

    using Result = cs::Result;
    using Status = cs::Status;
    using Config = cs::Config;

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

    int port() const
    {
        return this->server->port();
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
        else
        {
            m_minor_version = cs::CS_UNKNOWN;
        }

        m_version_number = vn;
    }

    void clear_status(uint64_t mask)
    {
        this->server->clear_status(mask);
    }

    void set_status(uint64_t mask)
    {
        if (mask != this->server->status())
        {
            this->server->clear_status(~mask);
            this->server->set_status(mask);
        }
    }

    Config fetch_config() const;
    Status fetch_node_status() const;
    Result fetch_cluster_status(std::map<std::string, Status>* pRv) const;

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

protected:
    CsMonitorServer(SERVER* pServer,
                    const SharedSettings& shared,
                    CsContext* pCs_context);

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

class CsBootstrapServer final : public CsMonitorServer
{
public:
    CsBootstrapServer(const CsBootstrapServer&) = delete;
    CsBootstrapServer& operator = (const CsBootstrapServer&) = delete;

    CsBootstrapServer(SERVER* pServer,
                      const SharedSettings& shared,
                      CsContext* pCs_context)
        : CsMonitorServer(pServer, shared, pCs_context)
    {
    };
};

class CsDynamicServer final : public CsMonitorServer
{
public:
    class Persister
    {
    public:
        virtual void persist(const CsDynamicServer& node) = 0;
        virtual void unpersist(const CsDynamicServer& node) = 0;
    };

    CsDynamicServer(const CsDynamicServer&) = delete;
    CsDynamicServer& operator = (const CsDynamicServer&) = delete;

    CsDynamicServer(Persister* pPersister,
                    SERVER* pServer,
                    const SharedSettings& shared,
                    CsContext* pCs_context)
        : CsMonitorServer(pServer, shared, pCs_context)
        , m_persister(*pPersister)
    {
        m_persister.persist(*this);
    };

    void set_excluded()
    {
        clear_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
        m_persister.unpersist(*this);
    }

private:
    Persister& m_persister;
};
