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
    const char* pValue = nullptr;

    pNode = pNode->xmlChildrenNode;

    if (pNode)
    {
        if (pNode->content)
        {
            pValue = (const char*)pNode->content;
        }
    }

    return pValue;
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
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "The Columnstore config contains the element '%s', but either its "
                                  "child node '%s' is missing or it lacks a value.",
                                  zElement_name, zValue_name);
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

CsMonitorServer::Config CsMonitorServer::Config::create(const http::Result& response)
{
    std::chrono::system_clock::time_point timestamp;
    unique_ptr<xmlDoc> sXml;

    json_error_t error;
    unique_ptr<json_t> sJson(json_loadb(response.body.c_str(), response.body.length(), 0, &error));

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
                mxb_assert(!true);
                MXS_ERROR("Could not convert '%s' and/or '%s' to actual values.",
                          zXml, zTimestamp);
            }
        }
        else
        {
            mxb_assert(!true);
            MXS_ERROR("Obtained config object does not have the keys '%s' and/or '%s': %s",
                      cs::keys::CONFIG, cs::keys::TIMESTAMP, response.body.c_str());
        }
    }
    else
    {
        mxb_assert(!true);
        MXS_ERROR("Could not parse JSON data from: %s", error.text);
    }

    return Config(response, std::move(timestamp), std::move(sJson), std::move(sXml));
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
            LOG_APPEND_JSON_ERROR(&pOutput,
                                  "'%s' of '%s' queried, but Columnstore XML config is empty.",
                                  zValue_name, zElement_name);
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

CsMonitorServer::CsMonitorServer(SERVER* pServer,
                                 const SharedSettings& shared,
                                 int64_t admin_port,
                                 http::Config* pConfig)
    : mxs::MonitorServer(pServer, shared)
    , m_admin_port(admin_port)
    , m_http_config(*pConfig)
{
}

CsMonitorServer::~CsMonitorServer()
{
}

CsMonitorServer::Config CsMonitorServer::fetch_config() const
{
    http::Result result = http::get(create_url(cs::rest::CONFIG), m_http_config);

    return Config::create(result);
}

CsMonitorServer::Status CsMonitorServer::Status::create(const http::Result& response)
{
    cs::ClusterMode cluster_mode = cs::READ_ONLY;
    cs::DbrmMode dbrm_mode = cs::SLAVE;
    vector<int> dbroots;

    json_error_t error;
    unique_ptr<json_t> sJson(json_loadb(response.body.c_str(), response.body.length(), 0, &error));

    if (sJson)
    {
        json_t* pCluster_mode = json_object_get(sJson.get(), cs::keys::CLUSTER_MODE);
        json_t* pDbrm_mode = json_object_get(sJson.get(), cs::keys::DBRM_MODE);
        json_t* pDbroots = json_object_get(sJson.get(), cs::keys::DBROOTS);
        // TODO: 'services'.

        if (pCluster_mode && pDbrm_mode && pDbroots)
        {
            const char* zCluster_mode = json_string_value(pCluster_mode);
            const char* zDbrm_mode = json_string_value(pDbrm_mode);

            bool b1 = cs::from_string(zCluster_mode, &cluster_mode);
            bool b2 = cs::from_string(zDbrm_mode, &dbrm_mode);
            bool b3 = cs::dbroots_from_array(pDbroots, &dbroots);

            if (!b1 || !b2 || !b3)
            {
                mxb_assert(!true);
                MXS_ERROR("Could not convert '%s' and/or '%s' to actual values.",
                          zCluster_mode, zDbrm_mode);
            }
        }
        else
        {
            mxb_assert(!true);
            MXS_ERROR("Obtained status object does not have the keys '%s', '%s' or '%s: %s",
                      cs::keys::CLUSTER_MODE,
                      cs::keys::DBRM_MODE,
                      cs::keys::DBROOTS,
                      response.body.c_str());
        }
    }
    else
    {
        mxb_assert(!true);
        MXS_ERROR("Could not parse JSON data from: %s", error.text);
    }

    return Status(response, cluster_mode, dbrm_mode, dbroots, std::move(sJson));
}

CsMonitorServer::Status CsMonitorServer::fetch_status() const
{
    http::Result result = http::get(create_url(cs::rest::STATUS), m_http_config);

    return Status::create(result);
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

http::Result CsMonitorServer::begin(const std::chrono::seconds& timeout, const std::string& id)
{
    if (m_trx_state != TRX_INACTIVE)
    {
        mxb_assert(!true);
        MXS_WARNING("Transaction begin, when transaction state is not inactive.");
    }

#ifdef TRX_SUPPORTED
    http::Result result = http::put(create_url(cs::rest::BEGIN), begin_body(timeout, id), m_http_config);
#else
    http::Result result(http::Result::SUCCESS);
#endif

    if (result.ok())
    {
        m_trx_state = TRX_ACTIVE;
    }

    return result;
}

http::Result CsMonitorServer::commit()
{
    if (m_trx_state != TRX_ACTIVE)
    {
        mxb_assert(!true);
        MXS_WARNING("Transaction commit, when state is not active.");
    }

#ifdef TRX_SUPPORTED
    http::Result result = http::get(create_url(cs::rest::COMMIT), m_http_config);
#else
    http::Result result(http::Result::SUCCESS);
#endif

    // Whatever the result, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    return result;
}

http::Result CsMonitorServer::rollback()
{
    if (m_trx_state != TRX_ACTIVE)
    {
        mxb_assert(!true);
        MXS_WARNING("Transaction rollback, when state is not active.");
    }

#ifdef TRX_SUPPORTED
    http::Result result = http::get(create_url(cs::rest::ROLLBACK), m_http_config);
#else
    http::Result result(http::Result::SUCCESS);
#endif

    // Whatever the result, we consider a transaction as not being active.
    m_trx_state = TRX_INACTIVE;

    return result;
}

bool CsMonitorServer::set_mode(cs::ClusterMode mode, json_t** ppError)
{
    ostringstream body;
    body << "{"
         << "\"" << cs::keys::MODE << "\": "
         << "\"" << cs::to_string(mode) << "\""
         << "}";

    string url = create_url(cs::rest::CONFIG);
    http::Result result = http::put(url, body.str(), m_http_config);

    if (!result.ok())
    {
        LOG_APPEND_JSON_ERROR(ppError, "Could not set cluster mode.");

        json_error_t error;
        unique_ptr<json_t> sError(json_loadb(result.body.c_str(), result.body.length(), 0, &error));

        if (sError)
        {
            mxs_json_error_push_back_new(*ppError, sError.release());
        }
        else
        {
            MXS_ERROR("Body returned by Columnstore is not JSON: %s", result.body.c_str());
        }
    }

    return result.ok();
}

bool CsMonitorServer::set_config(const std::string& body, json_t** ppError)
{
    string url = create_url(cs::rest::CONFIG);
    http::Result result = http::put(url, body, m_http_config);

    if (!result.ok())
    {
        LOG_APPEND_JSON_ERROR(ppError, "Could not update configuration.");

        json_error_t error;
        unique_ptr<json_t> sError(json_loadb(result.body.c_str(), result.body.length(), 0, &error));

        if (sError)
        {
            mxs_json_error_push_back_new(*ppError, sError.release());
        }
        else
        {
            MXS_ERROR("Body returned by Columnstore is not JSON: %s", result.body.c_str());
        }
    }

    return result.ok();
}

//static
CsMonitorServer::Statuses CsMonitorServer::fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                                          const http::Config& http_config)
{
    Statuses rv;
    fetch_statuses(servers, http_config, &rv);
    return rv;
}

//static
bool CsMonitorServer::fetch_statuses(const std::vector<CsMonitorServer*>& servers,
                                     const http::Config& http_config,
                                     Statuses* pStatuses)
{
    vector<string> urls = create_urls(servers, cs::rest::STATUS);
    vector<http::Result> results = http::get(urls, http_config);

    mxb_assert(servers.size() == results.size());

    bool rv = true;

    vector<Status> statuses;
    for (auto& result : results)
    {
        statuses.emplace_back(Status::create(result));

        if (!result.ok() || !statuses.back().sJson)
        {
            rv = false;
        }
    }

    pStatuses->swap(statuses);

    return rv;
}

//static
CsMonitorServer::Configs CsMonitorServer::fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                                        const http::Config& http_config)
{
    Configs rv;
    fetch_configs(servers, http_config, &rv);
    return rv;
}

//static
bool CsMonitorServer::fetch_configs(const std::vector<CsMonitorServer*>& servers,
                                    const http::Config& http_config,
                                    Configs* pConfigs)
{
    vector<string> urls = create_urls(servers, cs::rest::CONFIG);
    vector<http::Result> results = http::get(urls, http_config);

    mxb_assert(servers.size() == results.size());

    bool rv = true;

    vector<Config> configs;
    for (auto& result : results)
    {
        configs.emplace_back(Config::create(result));

        if (!result.ok() || !configs.back().sJson)
        {
            rv = false;
        }
    }

    pConfigs->swap(configs);

    return rv;
}

//static
bool CsMonitorServer::begin(const std::vector<CsMonitorServer*>& servers,
                            const std::chrono::seconds& timeout,
                            const std::string& id,
                            const http::Config& http_config,
                            http::Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return pServer->in_trx();
        });

    if (it != servers.end())
    {
        mxb_assert(!true);
        MXB_WARNING("Transaction begin, when at least '%s' is already in a transaction.",
                    (*it)->name());
    }

    vector<string> urls = create_urls(servers, cs::rest::BEGIN);
#ifdef TRX_SUPPORTED
    vector<http::Result> results = http::put(urls, begin_body(timeout, id), http_config);
#else
    vector<http::Result> results;
    http::Result result(http::Result::SUCCESS);
    for (size_t i = 0; i < urls.size(); ++i)
    {
        results.push_back(result);
    }
#endif

    mxb_assert(urls.size() == results.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        if (result.ok())
        {
            pServer->m_trx_state = TRX_ACTIVE;
        }
        else
        {
            MXS_ERROR("Transaction begin on '%s' failed: %s",
                      pServer->name(), result.body.c_str());
            rv = false;
            pServer->m_trx_state = TRX_INACTIVE;
        }

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//static
http::Results CsMonitorServer::begin(const std::vector<CsMonitorServer*>& servers,
                                     const std::chrono::seconds& timeout,
                                     const std::string& id,
                                     const http::Config& http_config)
{
    Results results;
    begin(servers, timeout, id, http_config, &results);
    return results;
}

//static
bool CsMonitorServer::commit(const std::vector<CsMonitorServer*>& servers,
                             const http::Config& http_config,
                             Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return !pServer->in_trx();
        });

    if (it != servers.end())
    {
        mxb_assert(!true);
        MXB_WARNING("Transaction commit, when at least '%s' is not in a transaction.",
                    (*it)->name());
    }

    vector<string> urls = create_urls(servers, cs::rest::COMMIT);
#ifdef TRX_SUPPORTED
    vector<http::Result> results = http::put(urls, "{}", http_config);
#else
    vector<http::Result> results;
    http::Result result(http::Result::SUCCESS);
    for (size_t i = 0; i < urls.size(); ++i)
    {
        results.push_back(result);
    }
#endif

    mxb_assert(urls.size() == results.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        if (!result.ok())
        {
            MXS_ERROR("Committing transaction on '%s' failed: %s",
                      pServer->name(), result.body.c_str());
            rv = false;
        }

        pServer->m_trx_state = TRX_INACTIVE;

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//statis
http::Results CsMonitorServer::commit(const std::vector<CsMonitorServer*>& servers,
                                      const http::Config& http_config)
{
    Results results;
    commit(servers, http_config, &results);
    return results;
}

//static
bool CsMonitorServer::ping(const std::vector<CsMonitorServer*>& servers,
                           const mxb::http::Config& http_config,
                           Results* pResults)
{
    vector<string> urls = create_urls(servers, cs::rest::PING);
    http::Results results = http::get(urls, http_config);

    bool rv = std::all_of(results.begin(), results.end(), std::mem_fun_ref(&http::Result::ok));

    pResults->swap(results);

    return rv;
}

//static
http::Results CsMonitorServer::ping(const std::vector<CsMonitorServer*>& servers,
                                    const mxb::http::Config& config)
{
    Results results;
    ping(servers, config, &results);
    return results;
}

//static
bool CsMonitorServer::rollback(const std::vector<CsMonitorServer*>& servers,
                               const http::Config& http_config,
                               Results* pResults)
{
    auto it = std::find_if(servers.begin(), servers.end(), [](const CsMonitorServer* pServer) {
            return !pServer->in_trx();
        });

    if (it != servers.end())
    {
        mxb_assert(!true);
        MXB_WARNING("Transaction rollback, when at least '%s' is not in a transaction.",
                    (*it)->name());
    }

    vector<string> urls = create_urls(servers, cs::rest::ROLLBACK);
#ifdef TRX_SUPPORTED
    vector<http::Result> results = http::put(urls, "{}", http_config);
#else
    vector<http::Result> results;
    http::Result result(http::Result::SUCCESS);
    for (size_t i = 0; i < urls.size(); ++i)
    {
        results.push_back(result);
    }
#endif

    mxb_assert(urls.size() == results.size());

    bool rv = true;

    it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        if (!result.ok())
        {
            MXS_ERROR("Rollbacking transaction on '%s' failed: %s",
                      pServer->name(), result.body.c_str());
            rv = false;
        }

        pServer->m_trx_state = TRX_INACTIVE;

        ++it;
        ++jt;
    }

    pResults->swap(results);

    return rv;
}

//static
http::Results CsMonitorServer::rollback(const std::vector<CsMonitorServer*>& servers,
                                        const http::Config& http_config)
{
    Results results;
    rollback(servers, http_config, &results);
    return results;
}

//static
http::Results CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                                        const std::chrono::seconds& timeout,
                                        const http::Config& http_config)
{
    string tail;

    if (timeout.count() != 0)
    {
        tail += "timeout=";
        tail += std::to_string(timeout.count());
    }

    vector<string> urls = create_urls(servers, cs::rest::SHUTDOWN, tail);
    vector<http::Result> results = http::put(urls, "{}", http_config);

    mxb_assert(urls.size() == results.size());

    return results;
}

//static
bool CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                               const std::chrono::seconds& timeout,
                               const mxb::http::Config& http_config,
                               Results* pResults)
{
    Results results = shutdown(servers, timeout, http_config);

    bool rv = true;

    for (const auto& result : results)
    {
        if (!result.ok())
        {
            rv = false;
            break;
        }
    }

    pResults->swap(results);

    return rv;
}

//static
http::Results CsMonitorServer::start(const std::vector<CsMonitorServer*>& servers,
                                     const http::Config& http_config)
{
    vector<string> urls = create_urls(servers, cs::rest::START);
    vector<http::Result> results = http::put(urls, "{}", http_config);

    mxb_assert(urls.size() == results.size());

    return results;
}

//static
bool CsMonitorServer::set_mode(const std::vector<CsMonitorServer*>& servers,
                               cs::ClusterMode mode,
                               const http::Config& http_config,
                               json_t** ppError)
{
    bool rv = false;

    Statuses statuses;

    if (!fetch_statuses(servers, http_config, &statuses))
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
                                 const mxb::http::Config& http_config,
                                 Results* pResults)
{
    Results results = set_config(servers, body, http_config);

    bool rv = true;

    for (const auto& result : results)
    {
        if (!result.ok())
        {
            rv = false;
            break;
        }
    }

    pResults->swap(results);

    return rv;
}

//static
http::Results CsMonitorServer::set_config(const std::vector<CsMonitorServer*>& servers,
                                          const std::string& body,
                                          const mxb::http::Config& http_config)
{
    vector<string> urls = create_urls(servers, cs::rest::CONFIG);

    return http::put(urls, body, http_config);
}

string CsMonitorServer::create_url(cs::rest::Action action, const std::string& tail) const
{
    string url = cs::rest::create_url(*this->server, m_admin_port, action);

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
        string url = cs::rest::create_url(*pS, pS->m_admin_port, action);

        if (!tail.empty())
        {
            url += "?";
            url += tail;
        }

        urls.push_back(url);
    }

    return urls;
}
