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
using std::stringstream;
using std::unique_ptr;
using std::vector;

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

bool CsMonitorServer::refresh_config(json_t** ppError)
{
    bool rv = false;
    http::Result result = http::get(create_url(cs::rest::CONFIG), m_http_config);

    if (result.code == 200)
    {
        rv = set_config(result.body, ppError);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "Could not fetch config from '%s': %s",
                             this->server->name(), result.body.c_str());
    }

    return rv;
}

bool CsMonitorServer::refresh_status(json_t** ppError)
{
    bool rv = false;
    http::Result result = http::get(create_url(cs::rest::STATUS), m_http_config);

    if (result.code == 200)
    {
        rv = set_status(result.body, ppError);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "Could not fetch status from '%s': %s",
                             this->server->name(), result.body.c_str());
    }

    return rv;
}

bool CsMonitorServer::set_config(const string& body, json_t** ppError)
{
    bool rv = false;

    json_error_t error;
    unique_ptr<json_t> sConfig(json_loadb(body.c_str(), body.length(), 0, &error));

    if (sConfig)
    {
        json_t* pConfig = json_object_get(sConfig.get(), cs::keys::CONFIG);

        if (pConfig)
        {
            const char* zXml = json_string_value(pConfig);
            size_t xml_len = json_string_length(pConfig);

            unique_ptr<xmlDoc> sDoc(xmlReadMemory(zXml, xml_len, "columnstore.xml", NULL, 0));

            if (sDoc)
            {
                m_sConfig = std::move(sConfig);
                m_sDoc = std::move(sDoc);

                rv = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppError,
                                     "Failed to parse XML configuration of '%s'.", name());
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(ppError,
                                 "Obtained config object from '%s', but it does not have a '%s' key.",
                                 name(),
                                 cs::keys::CONFIG);
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError, "Could not parse JSON data from: %s", error.text);
    }

    return rv;
}

bool CsMonitorServer::set_status(const string& body, json_t** ppError)
{
    bool rv = false;

    json_error_t error;
    unique_ptr<json_t> sStatus(json_loadb(body.c_str(), body.length(), 0, &error));

    if (sStatus)
    {
        json_t* pCluster_mode = json_object_get(sStatus.get(), cs::keys::CLUSTER_MODE);
        json_t* pDbrm_mode = json_object_get(sStatus.get(), cs::keys::DBRM_MODE);
        // TODO: 'dbroots' and 'services'.

        if (pCluster_mode && pDbrm_mode)
        {
            cs::ClusterMode cluster_mode;
            cs::DbrmMode dbrm_mode;

            const char* zCluster_mode = json_string_value(pCluster_mode);
            const char* zDbrm_mode = json_string_value(pDbrm_mode);

            if (cs::from_string(zCluster_mode, &cluster_mode) && cs::from_string(zDbrm_mode, &dbrm_mode))
            {
                m_status.cluster_mode = cluster_mode;
                m_status.dbrm_mode = dbrm_mode;
                m_status.sJson = std::move(sStatus);
                rv = true;
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppError,
                                     "Could not convert '%s' and/or '%s' to actual values.",
                                     zCluster_mode, zDbrm_mode);
            }
        }
        else
        {
            PRINT_MXS_JSON_ERROR(ppError,
                                 "Obtained status object from '%s', but it does not have the "
                                 "key '%s' and/or '%s': %s",
                                 name(),
                                 cs::keys::CLUSTER_MODE, cs::keys::DBRM_MODE, body.c_str());
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError, "Could not parse JSON data from: %s", error.text);
    }

    m_status.valid = rv;

    return rv;
}

bool CsMonitorServer::set_status(const http::Result& result, json_t** ppError)
{
    bool rv = true;

    if (result.code == 200)
    {
        rv = set_status(result.body, ppError);
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "Could not fetch status from '%s': %s",
                             this->server->name(), result.body.c_str());
    }

    return rv;
}

bool CsMonitorServer::update(cs::ClusterMode mode, json_t** ppError)
{
    stringstream body;
    body << "{"
         << "\"" << cs::keys::MODE << "\": "
         << "\"" << cs::to_string(mode) << "\""
         << "}";

    string url = create_url(cs::rest::CONFIG);
    http::Result result = http::put(url, body.str(), m_http_config);

    if (!result.ok())
    {
        PRINT_MXS_JSON_ERROR(ppError, "Could not update cluster mode: %s", result.body.c_str());
    }

    // Whether the operation succeeds or not, the status is marked as being non-valid.
    m_status.valid = false;

    return result.ok();
}

//static
bool CsMonitorServer::refresh_status(const vector<CsMonitorServer*>& servers,
                                     const mxb::http::Config& config,
                                     json_t** ppError)
{
    vector<string> urls = create_urls(servers, cs::rest::STATUS);
    vector<http::Result> results = http::get(urls, config);

    mxb_assert(servers.size() == results.size());

    bool rv = true;

    auto it = servers.begin();
    auto jt = results.begin();

    while (it != servers.end())
    {
        auto* pServer = *it;

        if (!pServer->set_status(*jt, ppError))
        {
            rv = false;
        }

        ++it;
        ++jt;
    }

    return rv;
}

//static
bool CsMonitorServer::shutdown(const std::vector<CsMonitorServer*>& servers,
                               const std::chrono::seconds& timeout,
                               const mxb::http::Config& config,
                               json_t** ppOutput)
{
    string tail;

    if (timeout.count() != 0)
    {
        tail += "timeout=";
        tail += std::to_string(timeout.count());
    }

    vector<string> urls = create_urls(servers, cs::rest::SHUTDOWN, tail);
    vector<http::Result> results = http::put(urls, "{}", config);

    mxb_assert(urls.size() == results.size());

    auto it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    json_t* pOutput = json_array();

    bool rv = true;

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        json_t* pObject = json_object();
        json_object_set_new(pObject, "name", json_string(pServer->name()));
        json_object_set_new(pObject, "code", json_integer(result.code));

        if (!result.ok())
        {
            json_error_t error;
            unique_ptr<json_t> sError(json_loadb(result.body.c_str(), result.body.length(), 0, &error));

            if (!sError)
            {
                sError.reset(json_string(result.body.c_str()));
            }

            json_object_set_new(pObject, "error", sError.release());
            rv = false;
        }

        json_array_append_new(pOutput, pObject);

        ++it;
        ++jt;
    }

    *ppOutput = pOutput;

    return rv;
}

//static
bool CsMonitorServer::update(const std::vector<CsMonitorServer*>& servers,
                             cs::ClusterMode mode,
                             const mxb::http::Config& config,
                             json_t** ppError)
{
    bool rv = false;

    if (!CsMonitorServer::refresh_status(servers, config))
    {
        MXS_ERROR("Could not refresh the status of all nodes. Will continue with the mode change "
                  "if single DBMR master was refreshed.");
    }

    CsMonitorServer* pMaster = nullptr;
    int nMasters = 0;

    for (const auto& pServer : servers)
    {
        const auto& status = pServer->status();
        if (status.valid)
        {
            if (status.dbrm_mode == cs::MASTER)
            {
                ++nMasters;
                pMaster = pServer;
            }
        }
    }

    if (nMasters == 0)
    {
        PRINT_MXS_JSON_ERROR(ppError, "No DBRM master found, mode change cannot be performed.");
    }
    else if (nMasters != 1)
    {
        PRINT_MXS_JSON_ERROR(ppError,
                             "%d masters found. Splitbrain situation, mode change cannot be performed.",
                             nMasters);
    }
    else
    {
        rv = pMaster->update(mode, ppError);
    }

    return rv;
}

string CsMonitorServer::create_url(cs::rest::Action action, const std::string& tail)
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
