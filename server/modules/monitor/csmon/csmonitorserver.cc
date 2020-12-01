/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "csmonitorserver.hh"
#include <sstream>
#include <maxbase/http.hh>
#include "columnstore.hh"
#include "csconfig.hh"
#include "cscontext.hh"

namespace http = mxb::http;
using namespace std;

using Config = CsMonitorServer::Config;
using Result = CsMonitorServer::Result;
using Status = CsMonitorServer::Status;

using Configs = CsMonitorServer::Configs;
using Results = CsMonitorServer::Results;
using Statuses = CsMonitorServer::Statuses;

CsMonitorServer::CsMonitorServer(SERVER* pServer,
                                 const SharedSettings& shared,
                                 CsContext* pCs_context)
    : mxs::MonitorServer(pServer, shared)
    , m_context(*pCs_context)
{
}

CsMonitorServer::~CsMonitorServer()
{
}

Config CsMonitorServer::fetch_config() const
{
    http::Response response = http::get(create_url(cs::rest::NODE, cs::rest::CONFIG),
                                        m_context.http_config());

    return Config(response);
}

bool CsMonitorServer::set_node_mode(const Config& config, json_t* pOutput)
{
    bool rv = true;
    mxb_assert(config.ok());

    string ip;
    if (config.get_dbrm_controller_ip(&ip))
    {
        if (ip == "127.0.0.1")
        {
            set_node_mode(CsMonitorServer::SINGLE_NODE);
        }
        else if (ip == address())
        {
            set_node_mode(CsMonitorServer::MULTI_NODE);
        }
        else
        {
            MXS_ERROR("MaxScale thinks the IP address of the server '%s' is %s, "
                      "while the server itself thinks it is %s.",
                      name(), address(), ip.c_str());
            rv = false;
        }
    }
    else
    {
        MXS_ERROR("Could not get DMRM_Controller IP of '%s'.", name());
        rv = false;
    }

    return rv;
}

Status CsMonitorServer::fetch_node_status() const
{
    http::Response response = http::get(create_url(cs::rest::NODE, cs::rest::STATUS),
                                        m_context.http_config());

    return Status(response);
}

Result CsMonitorServer::fetch_cluster_status(map<string,Status>* pStatuses) const
{
    const auto& config = m_context.config();

    return cs::fetch_cluster_status(this->server->address(),
                                    config.admin_port, config.admin_base_path,
                                    m_context.http_config(),
                                    pStatuses);
}

Result CsMonitorServer::add_node(const std::vector<CsMonitorServer*>& servers,
                                 const std::string& host,
                                 const std::chrono::seconds& timeout,
                                 CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::CLUSTER, cs::rest::ADD_NODE);
        response = http::put(url, cs::body::add_node(host, timeout), context.http_config(timeout));
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

Result CsMonitorServer::begin(const std::chrono::seconds& timeout, json_t* pOutput)
{
    if (m_trx_state != TRX_INACTIVE)
    {
        MXS_WARNING("Transaction begin, when transaction state is not inactive.");
        mxb_assert(!true);
    }

    http::Response response = http::put(create_url(cs::rest::NODE, cs::rest::BEGIN),
                                        cs::body::begin(timeout, m_context.next_trx_id()),
                                        m_context.http_config(timeout));

    if (response.is_success())
    {
        m_trx_state = TRX_ACTIVE;
    }

    Result result(response);

    if (!result.ok() && pOutput && result.sJson)
    {
        mxs_json_error_push_back(pOutput, result.sJson.get());
    }

    return result;
}

Result CsMonitorServer::commit(const std::chrono::seconds& timeout, json_t* pOutput)
{
    if (m_trx_state != TRX_ACTIVE)
    {
        MXS_WARNING("Transaction commit, when state is not active.");
        mxb_assert(!true);
    }

    http::Response response = http::put(create_url(cs::rest::NODE, cs::rest::COMMIT),
                                        cs::body::commit(timeout, m_context.current_trx_id()),
                                        m_context.http_config(timeout));

    // Whatever the response, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    Result result(response);

    if (!result.ok() && pOutput && result.sJson)
    {
        mxs_json_error_push_back(pOutput, result.sJson.get());
    }

    return result;
}

Result CsMonitorServer::remove_node(const std::vector<CsMonitorServer*>& servers,
                                    const std::string& host,
                                    const std::chrono::seconds& timeout,
                                    CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::CLUSTER, cs::rest::REMOVE_NODE);
        response = http::put(url, cs::body::remove_node(host, timeout), context.http_config(timeout));
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

Result CsMonitorServer::rollback(json_t* pOutput)
{
    // Always ok to send a rollback.
    http::Response response = http::put(create_url(cs::rest::NODE, cs::rest::ROLLBACK),
                                        cs::body::rollback(m_context.current_trx_id()),
                                        m_context.http_config());

    // Whatever the response, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    Result result(response);

    if (!result.ok() && pOutput && result.sJson)
    {
        mxs_json_error_push_back(pOutput, result.sJson.get());
    }

    return result;
}

bool CsMonitorServer::set_cluster_mode(cs::ClusterMode mode,
                                       const std::chrono::seconds& timeout,
                                       json_t* pOutput)
{
    auto body = cs::body::config_set_cluster_mode(mode, m_context.revision(), m_context.manager(), timeout);

    string url = create_url(cs::rest::NODE, cs::rest::CONFIG);
    http::Response response = http::put(url, body, m_context.http_config(timeout));

    if (!response.is_success())
    {
        Result result(response);

        if (result.sJson)
        {
            mxs_json_error_push_back(pOutput, result.sJson.get());
        }
    }

    return response.is_success();
}

//static
CsMonitorServer::Result CsMonitorServer::fetch_status(const std::vector<CsMonitorServer*>& servers,
                                                      CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::CLUSTER, cs::rest::STATUS);
        response = http::get(url, context.http_config());
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

//static
CsMonitorServer::Statuses CsMonitorServer::fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                                          CsContext& context)
{
    Statuses rv;
    fetch_statuses(servers, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                     CsContext& context,
                                     Statuses* pStatuses)
{
    vector<string> urls = create_urls(servers, cs::rest::NODE, cs::rest::STATUS);
    vector<http::Response> responses = http::get(urls, context.http_config());

    mxb_assert(servers.size() == responses.size());

    bool rv = true;

    Statuses statuses;
    for (auto& response : responses)
    {
        Status status(response);

        if (!status.ok())
        {
            rv = false;
        }

        statuses.emplace_back(std::move(status));
    }

    pStatuses->swap(statuses);

    return rv;
}

//static
Result CsMonitorServer::fetch_config(const std::vector<CsMonitorServer*>& servers,
                                     CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::NODE, cs::rest::CONFIG);
        response = http::get(url, context.http_config());
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

//static
Configs CsMonitorServer::fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                       CsContext& context)
{
    Configs rv;
    fetch_configs(servers, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                    CsContext& context,
                                    Configs* pConfigs)
{
    vector<string> urls = create_urls(servers, cs::rest::NODE, cs::rest::CONFIG);
    vector<http::Response> responses = http::get(urls, context.http_config());

    mxb_assert(servers.size() == responses.size());

    bool rv = true;

    Configs configs;
    for (auto& response : responses)
    {
        Config config(response);

        if (!config.ok())
        {
            rv = false;
        }

        configs.emplace_back(std::move(config));
    }

    pConfigs->swap(configs);

    return rv;
}

//static
bool CsMonitorServer::begin(const std::vector<CsMonitorServer*>& servers,
                            const std::chrono::seconds& timeout,
                            CsContext& context,
                            Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return pServer->in_trx();
        });

    if (it != servers.end())
    {
        MXB_WARNING("Transaction begin, when at least '%s' is already in a transaction.",
                    (*it)->name());
        mxb_assert(!true);
    }

    vector<string> urls = create_urls(servers, cs::rest::NODE, cs::rest::BEGIN);
    vector<http::Response> responses = http::put(urls,
                                                 cs::body::begin(timeout, context.next_trx_id()),
                                                 context.http_config(timeout));

    mxb_assert(urls.size() == responses.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = responses.begin();

    Results results;
    while (it != end)
    {
        auto* pServer = *it;
        const auto& response = *jt;

        Result result(response);

        if (result.ok())
        {
            pServer->m_trx_state = TRX_ACTIVE;
        }
        else
        {
            MXS_ERROR("Transaction begin on '%s' failed: %s",
                      pServer->name(), response.body.c_str());
            rv = false;
            pServer->m_trx_state = TRX_INACTIVE;
        }

        results.emplace_back(std::move(result));

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//static
Results CsMonitorServer::begin(const std::vector<CsMonitorServer*>& servers,
                               const std::chrono::seconds& timeout,
                               CsContext& context)
{
    Results rv;
    begin(servers, timeout, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::commit(const std::vector<CsMonitorServer*>& servers,
                             const std::chrono::seconds& timeout,
                             CsContext& context,
                             Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return !pServer->in_trx();
        });

    if (it != servers.end())
    {
        MXB_WARNING("Transaction commit, when at least '%s' is not in a transaction.",
                    (*it)->name());
        mxb_assert(!true);
    }

    vector<string> urls = create_urls(servers, cs::rest::NODE, cs::rest::COMMIT);
    vector<http::Response> responses = http::put(urls,
                                                 cs::body::commit(timeout, context.current_trx_id()),
                                                 context.http_config(timeout));

    mxb_assert(urls.size() == responses.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = responses.begin();

    Results results;
    while (it != end)
    {
        auto* pServer = *it;
        const auto& response = *jt;

        Result result(response);

        if (!result.ok())
        {
            MXS_ERROR("Committing transaction on '%s' failed: %s",
                      pServer->name(), response.body.c_str());
            rv = false;
        }

        pServer->m_trx_state = TRX_INACTIVE;

        results.emplace_back(std::move(result));

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//statis
Results CsMonitorServer::commit(const std::vector<CsMonitorServer*>& servers,
                                const std::chrono::seconds& timeout,
                                CsContext& context)
{
    Results rv;
    commit(servers, timeout, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::rollback(const std::vector<CsMonitorServer*>& servers,
                               CsContext& context,
                               Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return !pServer->in_trx();
        });

    if (it != servers.end())
    {
        MXB_WARNING("Transaction rollback, when at least '%s' is not in a transaction.",
                    (*it)->name());
        mxb_assert(!true);
    }

    vector<string> urls = create_urls(servers, cs::rest::NODE, cs::rest::ROLLBACK);
    vector<http::Response> responses = http::put(urls,
                                                 cs::body::rollback(context.current_trx_id()),
                                                 context.http_config());

    mxb_assert(urls.size() == responses.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = responses.begin();

    Results results;
    while (it != end)
    {
        auto* pServer = *it;
        const auto& response = *jt;

        Result result(response);

        if (!result.ok())
        {
            MXS_ERROR("Rollbacking transaction on '%s' failed: %s",
                      pServer->name(), response.body.c_str());
            rv = false;
        }

        pServer->m_trx_state = TRX_INACTIVE;

        results.emplace_back(std::move(result));

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//static
Results CsMonitorServer::rollback(const std::vector<CsMonitorServer*>& servers,
                                  CsContext& context)
{
    Results rv;
    rollback(servers, context, &rv);
    return rv;
}

//static
Result CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                                 const std::chrono::seconds& timeout,
                                 CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::CLUSTER, cs::rest::SHUTDOWN);
        response = http::put(url, cs::body::shutdown(timeout), context.http_config(timeout));
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

Result CsMonitorServer::start(const std::vector<CsMonitorServer*>& servers,
                              const std::chrono::seconds& timeout,
                              CsContext& context)
{
    http::Response response;

    if (!servers.empty())
    {
        string url = servers.front()->create_url(cs::rest::CLUSTER, cs::rest::START);
        response = http::put(url, cs::body::start(timeout), context.http_config(timeout));
    }
    else
    {
        response.code = http::Response::ERROR;
        response.body = "No servers specified.";
    }

    return Result(response);
}

//static
bool CsMonitorServer::set_cluster_mode(const std::vector<CsMonitorServer*>& servers,
                                       cs::ClusterMode mode,
                                       const std::chrono::seconds& timeout,
                                       CsContext& context,
                                       json_t* pOutput)
{
    bool rv = false;

    CsMonitorServer* pMaster = get_master(servers, context, pOutput);

    if (pMaster)
    {
        Result result = pMaster->begin(timeout, pOutput);

        if (result.ok())
        {
            if (pMaster->set_cluster_mode(mode, timeout, pOutput))
            {
                rv = true;
            }

            if (rv)
            {
                result = pMaster->commit(timeout, pOutput);

                if (!result.ok())
                {
                    rv = false;
                }
            }

            if (!rv)
            {
                result = pMaster->rollback(pOutput);
                if (!result.ok())
                {
                    MXS_ERROR("Could not rollback transaction.");
                }
            }
        }
    }

    return rv;
}

//static
CsMonitorServer* CsMonitorServer::get_master(const std::vector<CsMonitorServer*>& servers,
                                             CsContext& context,
                                             json_t* pOutput)
{
    CsMonitorServer* pMaster = nullptr;

    Statuses statuses;
    if (!fetch_statuses(servers, context, &statuses))
    {
        MXS_ERROR("Could not fetch the status of all servers. Will continue with the mode change "
                  "if single DBMR master was refreshed.");
    }

    int nMasters = 0;

    auto it = servers.begin();
    auto end = servers.end();
    auto jt = statuses.begin();

    while (it != end)
    {
        CsMonitorServer* pServer = *it;
        const Status& status = *jt;

        if (status.ok())
        {
            if (status.dbrm_mode == cs::MASTER)
            {
                ++nMasters;
                pMaster = pServer;
            }
        }

        ++it;
        ++jt;
    }

    if (nMasters == 0)
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "No DBRM master found, mode change cannot be performed.");
    }
    else if (nMasters != 1)
    {
        LOG_APPEND_JSON_ERROR(&pOutput,
                              "%d masters found. Splitbrain situation, mode change cannot be performed.",
                              nMasters);
    }

    return pMaster;
}

string CsMonitorServer::create_url(cs::rest::Scope scope,
                                   cs::rest::Action action,
                                   const std::string& tail) const
{
    string url = cs::rest::create_url(*this->server,
                                      m_context.config().admin_port,
                                      m_context.config().admin_base_path,
                                      scope,
                                      action);

    if (!tail.empty())
    {
        url += "?";
        url += tail;
    }

    return url;
}

//static
vector<string> CsMonitorServer::create_urls(const std::vector<CsMonitorServer*>& servers,
                                            cs::rest::Scope scope,
                                            cs::rest::Action action,
                                            const std::string& tail)
{
    vector<string> urls;

    for (const auto* pS : servers)
    {
        string url = cs::rest::create_url(*pS,
                                          pS->m_context.config().admin_port,
                                          pS->m_context.config().admin_base_path,
                                          scope,
                                          action);

        if (!tail.empty())
        {
            url += "?";
            url += tail;
        }

        urls.push_back(url);
    }

    return urls;
}
