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

#include "csmonitorserver.hh"
#include <sstream>
#include <maxbase/http.hh>
#include "columnstore.hh"
#include "csconfig.hh"
#include "cscontext.hh"

namespace http = mxb::http;
using std::string;
using std::ostringstream;
using std::unique_ptr;
using std::vector;

namespace
{

xmlNode* get_child_node(xmlNode* pNode, const char* zName)
{
    pNode = pNode->xmlChildrenNode;

    while (pNode)
    {
        if (pNode->type == XML_ELEMENT_NODE && xmlStrcmp(pNode->name, (const xmlChar*) zName) == 0)
        {
            break;
        }

        pNode = pNode->next;
    }

    return pNode;
}

const char* get_child_value(xmlNode* pNode, const char* zName)
{
    const char* zValue = nullptr;

    pNode = get_child_node(pNode, zName);

    if (pNode)
    {
        zValue = reinterpret_cast<const char*>(xmlNodeGetContent(pNode));
    }

    return zValue;
}

bool get_value(xmlNode* pNode,
               const char* zElement_name,
               const char* zValue_name,
               string* pValue,
               json_t* pOutput)
{
    bool rv = false;

    pNode = get_child_node(pNode, zElement_name);

    if (pNode)
    {
        const char* zValue = get_child_value(pNode, zValue_name);

        if (zValue)
        {
            *pValue = zValue;
            rv = true;
        }
        else
        {
            static const char FORMAT[] =
                "The Columnstore config contains the element '%s', but either its "
                "child node '%s' is missing or it lacks a value.";

            MXS_ERROR(FORMAT, zElement_name, zValue_name);

            if (pOutput)
            {
                mxs_json_error_append(pOutput, FORMAT, zElement_name, zValue_name);
            }
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Columnstore config does not contain the element '%s'.",
                              zElement_name);
    }

    return rv;
}

}

//static
int64_t CsMonitorServer::Status::s_uptime = 1;

CsMonitorServer::Result::Result(const http::Response& response)
    : response(response)
{
    if (response.is_success())
    {
        json_error_t error;
        sJson.reset(json_loadb(response.body.c_str(), response.body.length(), 0, &error));

        if (!sJson)
        {
            MXS_ERROR("Could not parse returned response '%s' as JSON: %s",
                      response.body.c_str(),
                      error.text);
            mxb_assert(!true);
        }
    }
    else if (response.is_error())
    {
        MXS_ERROR("REST-API call failed: (%d) %s",
                  response.code, http::Response::to_string(response.code));
    }
    else
    {
        MXS_ERROR("Unexpected response from server: (%d) %s",
                  response.code, http::Response::to_string(response.code));
    }
}

CsMonitorServer::Config::Config(const http::Response& response)
    : Result(response)
{
    if (sJson)
    {
        json_t* pConfig = json_object_get(sJson.get(), cs::keys::CONFIG);
        json_t* pTimestamp = json_object_get(sJson.get(), cs::keys::TIMESTAMP);

        if (pConfig && pTimestamp)
        {
            const char* zXml = json_string_value(pConfig);
            const char* zTimestamp = json_string_value(pTimestamp);

            bool b1 = cs::from_string(zXml, &sXml);
            bool b2 = cs::from_string(zTimestamp, &timestamp);

            if (!b1 || !b2)
            {
                MXS_ERROR("Could not convert '%s' and/or '%s' to actual values.",
                          zXml, zTimestamp);
                mxb_assert(!true);
            }
        }
        else
        {
            MXS_ERROR("Obtained config object does not have the keys '%s' and/or '%s': %s",
                      cs::keys::CONFIG, cs::keys::TIMESTAMP, response.body.c_str());
            mxb_assert(!true);
        }
    }
}

bool CsMonitorServer::Config::get_value(const char* zElement_name,
                                        const char* zValue_name,
                                        std::string* pIp,
                                        json_t* pOutput) const
{
    bool rv = false;

    if (ok())
    {
        xmlNode* pNode = xmlDocGetRootElement(this->sXml.get());

        if (pNode)
        {
            rv = ::get_value(pNode, zElement_name, zValue_name, pIp, pOutput);
        }
        else
        {
            const char FORMAT[] = "'%s' of '%s' queried, but Columnstore XML config is empty.";

            if (pOutput)
            {
                mxs_json_error_append(pOutput, FORMAT, zValue_name, zElement_name);
            }

            MXS_ERROR(FORMAT, zValue_name, zElement_name);
        }
    }
    else
    {
        assert(!true);
        MXS_ERROR("'%s' of '%s' queried of config that is not valid.",
                  zValue_name, zElement_name);
    }

    return rv;
}

CsMonitorServer::Status::Status(const http::Response& response)
    : Result(response)
{
    if (sJson)
    {
        json_t* pCluster_mode = json_object_get(sJson.get(), cs::keys::CLUSTER_MODE);
        json_t* pDbrm_mode = json_object_get(sJson.get(), cs::keys::DBRM_MODE);
        json_t* pDbroots = json_object_get(sJson.get(), cs::keys::DBROOTS);
        json_t* pServices = json_object_get(sJson.get(), cs::keys::SERVICES);

        if (pCluster_mode && pDbrm_mode && pDbroots && pServices)
        {
            const char* zCluster_mode = json_string_value(pCluster_mode);
            const char* zDbrm_mode = json_string_value(pDbrm_mode);

            bool b1 = cs::from_string(zCluster_mode, &cluster_mode);
            bool b2 = cs::from_string(zDbrm_mode, &dbrm_mode);
            bool b3 = cs::dbroots_from_array(pDbroots, &dbroots);
            bool b4 = cs::services_from_array(pServices, &services);

            if (!b1 || !b2 || !b3 || !b4)
            {
                MXS_ERROR("Could not convert values '%s' and/or '%s', and/or arrays '%s' and/or '%s' "
                          "to actual values.",
                          zCluster_mode, zDbrm_mode, cs::keys::DBROOTS, cs::keys::SERVICES);
                mxb_assert(!true);
            }
        }
        else
        {
            MXS_ERROR("Obtained status object does not have the keys '%s', '%s', '%s' or '%s: %s",
                      cs::keys::CLUSTER_MODE,
                      cs::keys::DBRM_MODE,
                      cs::keys::DBROOTS,
                      cs::keys::SERVICES,
                      response.body.c_str());
            mxb_assert(!true);
        }
    }
}

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
    http::Response response = http::get(create_url(cs::rest::CONFIG), m_context.http_config());

    return Config(response);
}

bool CsMonitorServer::update_state(const Config& config, json_t* pOutput)
{
    bool rv = true;
    mxb_assert(config.ok());

    string ip;
    if (config.get_dbrm_controller_ip(&ip))
    {
        if (ip == "127.0.0.1")
        {
            set_state(CsMonitorServer::SINGLE_NODE);
        }
        else if (ip == address())
        {
            set_state(CsMonitorServer::MULTI_NODE);
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

Status CsMonitorServer::fetch_status() const
{
    http::Response response = http::get(create_url(cs::rest::STATUS), m_context.http_config());

    return Status(response);
}

namespace
{

string begin_body(const std::chrono::seconds& timeout, const std::string& id)
{
    ostringstream body;
    body << "{\"" << cs::keys::TIMEOUT << "\": "
         << timeout.count()
         << ", \"" << cs::keys::TXN // MaxScale uses TRX, but Columnstore uses TXN.
         << "\"" << id << "\""
         << "}";

    return body.str();
}

}

Result CsMonitorServer::begin(const std::chrono::seconds& timeout, const std::string& id)
{
    if (m_trx_state != TRX_INACTIVE)
    {
        MXS_WARNING("Transaction begin, when transaction state is not inactive.");
        mxb_assert(!true);
    }

    http::Response response = http::put(create_url(cs::rest::BEGIN), begin_body(timeout, id),
                                        m_context.http_config());

    if (response.is_success())
    {
        m_trx_state = TRX_ACTIVE;
    }

    return Result(response);
}

Result CsMonitorServer::commit()
{
    if (m_trx_state != TRX_ACTIVE)
    {
        MXS_WARNING("Transaction commit, when state is not active.");
        mxb_assert(!true);
    }

    http::Response response = http::put(create_url(cs::rest::COMMIT), "{}", m_context.http_config());

    // Whatever the response, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    return Result(response);
}

Result CsMonitorServer::rollback()
{
    // Always ok to send a rollback.
    http::Response response = http::put(create_url(cs::rest::ROLLBACK), "{}", m_context.http_config());

    // Whatever the response, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    return Result(response);
}

bool CsMonitorServer::set_mode(cs::ClusterMode mode, json_t** ppError)
{
    ostringstream body;
    body << "{"
         << "\"" << cs::keys::MODE << "\": "
         << "\"" << cs::to_string(mode) << "\""
         << "}";

    string url = create_url(cs::rest::CONFIG);
    http::Response response = http::put(url, body.str(), m_context.http_config());

    if (!response.is_success())
    {
        LOG_APPEND_JSON_ERROR(ppError, "Could not set cluster mode.");

        json_error_t error;
        unique_ptr<json_t> sError(json_loadb(response.body.c_str(), response.body.length(), 0, &error));

        if (sError)
        {
            mxs_json_error_push_back_new(*ppError, sError.release());
        }
        else
        {
            MXS_ERROR("Body returned by Columnstore is not JSON: %s", response.body.c_str());
        }
    }

    return response.is_success();
}

bool CsMonitorServer::set_config(const std::string& body, json_t** ppError)
{
    string url = create_url(cs::rest::CONFIG);
    http::Response response = http::put(url, body, m_context.http_config());

    if (!response.is_success())
    {
        LOG_APPEND_JSON_ERROR(ppError, "Could not update configuration.");

        json_error_t error;
        unique_ptr<json_t> sError(json_loadb(response.body.c_str(), response.body.length(), 0, &error));

        if (sError)
        {
            mxs_json_error_push_back_new(*ppError, sError.release());
        }
        else
        {
            MXS_ERROR("Body returned by Columnstore is not JSON: %s", response.body.c_str());
        }
    }

    return response.is_success();
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
    vector<string> urls = create_urls(servers, cs::rest::STATUS);
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
    vector<string> urls = create_urls(servers, cs::rest::CONFIG);
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
                            const std::string& id,
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

    vector<string> urls = create_urls(servers, cs::rest::BEGIN);
    vector<http::Response> responses = http::put(urls, begin_body(timeout, id), context.http_config());

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
                               const std::string& id,
                               CsContext& context)
{
    Results rv;
    begin(servers, timeout, id, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::commit(const std::vector<CsMonitorServer*>& servers,
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

    vector<string> urls = create_urls(servers, cs::rest::COMMIT);
    vector<http::Response> responses = http::put(urls, "{}", context.http_config());

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
                                CsContext& context)
{
    Results rv;
    commit(servers, context, &rv);
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

    vector<string> urls = create_urls(servers, cs::rest::ROLLBACK);
    vector<http::Response> responses = http::put(urls, "{}", context.http_config());

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
Results CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                                  const std::chrono::seconds& timeout,
                                  CsContext& context)
{
    Results rv;
    shutdown(servers, timeout, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                               const std::chrono::seconds& timeout,
                               CsContext& context,
                               Results* pResults)
{
    bool rv = true;

    string tail;
    if (timeout.count() != 0)
    {
        tail += "timeout=";
        tail += std::to_string(timeout.count());
    }

    vector<string> urls = create_urls(servers, cs::rest::SHUTDOWN, tail);
    vector<http::Response> responses = http::put(urls, "{}", context.http_config());

    mxb_assert(urls.size() == responses.size());

    Results results;
    for (const auto& response : responses)
    {
        Result result(response);

        if (!result.ok())
        {
            rv = false;
        }

        results.emplace_back(std::move(result));
    }

    pResults->swap(results);

    return rv;
}

//static
Results CsMonitorServer::start(const std::vector<CsMonitorServer*>& servers,
                               CsContext& context)
{
    Results rv;
    start(servers, context, &rv);
    return rv;
}

//static
bool CsMonitorServer::start(const std::vector<CsMonitorServer*>& servers,
                            CsContext& context,
                            Results* pResults)
{
    bool rv = true;

    vector<string> urls = create_urls(servers, cs::rest::START);
    vector<http::Response> responses = http::put(urls, "{}", context.http_config());

    mxb_assert(urls.size() == responses.size());

    Results results;
    for (const auto& response : responses)
    {
        Result result(response);

        if (!result.ok())
        {
            rv = false;
        }

        results.emplace_back(std::move(result));
    }

    pResults->swap(results);

    return rv;
}

//static
bool CsMonitorServer::set_mode(const std::vector<CsMonitorServer*>& servers,
                               cs::ClusterMode mode,
                               CsContext& context,
                               json_t** ppError)
{
    bool rv = false;

    Statuses statuses;

    if (!fetch_statuses(servers, context, &statuses))
    {
        MXS_ERROR("Could not fetch the status of all servers. Will continue with the mode change "
                  "if single DBMR master was refreshed.");
    }

    CsMonitorServer* pMaster = nullptr;
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
        LOG_APPEND_JSON_ERROR(ppError, "No DBRM master found, mode change cannot be performed.");
    }
    else if (nMasters != 1)
    {
        LOG_APPEND_JSON_ERROR(ppError,
                              "%d masters found. Splitbrain situation, mode change cannot be performed.",
                              nMasters);
    }
    else
    {
        rv = pMaster->set_mode(mode, ppError);
    }

    return rv;
}

//static
bool CsMonitorServer::set_config(const std::vector<CsMonitorServer*>& servers,
                                 const std::string& body,
                                 CsContext& context,
                                 Results* pResults)
{
    bool rv = true;

    vector<string> urls = create_urls(servers, cs::rest::CONFIG);
    vector<http::Response> responses = http::put(urls, body, context.http_config());

    Results results;
    for (const auto& response : responses)
    {
        Result result(response);

        if (!result.ok())
        {
            rv = false;
        }

        results.emplace_back(std::move(result));
    }

    pResults->swap(results);

    return rv;
}

//static
Results CsMonitorServer::set_config(const std::vector<CsMonitorServer*>& servers,
                                    const std::string& body,
                                    CsContext& context)
{
    Results rv;
    set_config(servers, body, context, &rv);
    return rv;
}

string CsMonitorServer::create_url(cs::rest::Action action, const std::string& tail) const
{
    string url = cs::rest::create_url(*this->server,
                                      m_context.config().admin_port,
                                      m_context.config().admin_base_path,
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
                                            cs::rest::Action action,
                                            const std::string& tail)
{
    vector<string> urls;

    for (const auto* pS : servers)
    {
        string url = cs::rest::create_url(*pS,
                                          pS->m_context.config().admin_port,
                                          pS->m_context.config().admin_base_path,
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
