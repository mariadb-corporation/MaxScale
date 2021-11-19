/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "columnstore.hh"
#include <map>
#include "csxml.hh"

using namespace std;

namespace http = mxb::http;

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
               const char* zValue_name,
               string* pValue,
               json_t* pOutput)
{
    bool rv = false;

    const char* zValue = get_child_value(pNode, zValue_name);

    if (zValue)
    {
        *pValue = zValue;
        rv = true;
    }
    else
    {
        static const char FORMAT[] =
            "The Columnstore config does not contain the element '%s', or it lacks a value.";

        MXS_ERROR(FORMAT, zValue_name);

        if (pOutput)
        {
            mxs_json_error_append(pOutput, FORMAT, zValue_name);
        }
    }

    return rv;
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

bool get_number(const char* zNumber, long* pNumber)
{
    char* zEnd;
    errno = 0;
    long number = strtol(zNumber, &zEnd, 10);

    bool valid = (errno == 0 && zEnd != zNumber && *zEnd == 0);

    if (valid)
    {
        *pNumber = number;
    }

    return valid;
}

bool is_positive_number(const char* zNumber)
{
    long number;
    return get_number(zNumber, &number) && number > 0;
}

}

namespace cs
{

//static
int64_t Status::s_uptime = 1;

Result::Result(const http::Response& response)
    : response(response)
{
#if defined(SS_DEBUG)
    if (response.is_client_error())
    {
        MXS_ERROR("HTTP client error %d: %s", response.code, response.body.c_str());
        mxb_assert(!true);
    }
#endif

    if (response.is_fatal())
    {
        MXS_ERROR("REST-API call failed: (%d) %s: %s",
                  response.code,
                  http::Response::to_string(response.code),
                  response.body.empty() ? "" : response.body.c_str());
    }
    else
    {
        // In all other cases, a JSON body should be returned.
        if (!response.body.empty())
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

        if (response.is_server_error())
        {
            MXS_ERROR("Server error: (%d) %s",
                      response.code, http::Response::to_string(response.code));
        }
        else if (!response.is_success())
        {
            MXS_ERROR("Unexpected response from server: (%d) %s",
                      response.code, http::Response::to_string(response.code));
        }
    }
}

Result::Result(const http::Response& response,
               std::unique_ptr<json_t> sJson)
    : response(response)
    , sJson(std::move(sJson))
{
}

Config::Config(const http::Response& response)
    : Result(response)
{
    if (response.is_success() && sJson)
    {
        json_t* pConfig = json_object_get(sJson.get(), cs::body::CONFIG);
        json_t* pTimestamp = json_object_get(sJson.get(), cs::body::TIMESTAMP);

        if (pConfig && pTimestamp)
        {
            const char* zXml = json_string_value(pConfig);
            const char* zTimestamp = json_string_value(pTimestamp);

            bool b1 = cs::from_string(zXml, &sXml);
            bool b2 = cs::from_string(zTimestamp, &timestamp);

            if (!b1 || !b2)
            {
                MXS_ERROR("Could not convert '%s' and/or '%s' to actual values: %s",
                          zXml, zTimestamp, response.body.c_str());
                mxb_assert(!true);
            }
        }
        else
        {
            MXS_ERROR("Obtained config object does not have the keys '%s' and/or '%s': %s",
                      cs::body::CONFIG, cs::body::TIMESTAMP, response.body.c_str());
            mxb_assert(!true);
        }
    }
}

bool Config::get_value(const char* zValue_name,
                       int* pRevision,
                       json_t* pOutput) const
{
    bool rv = false;

    if (ok())
    {
        xmlNode* pNode = xmlDocGetRootElement(this->sXml.get());

        if (pNode)
        {
            string value;
            rv = ::get_value(pNode, zValue_name, &value, pOutput);

            if (rv)
            {
                *pRevision = atoi(value.c_str());
            }
        }
        else
        {
            const char FORMAT[] = "'%s' queried, but Columnstore XML config is empty.";

            if (pOutput)
            {
                mxs_json_error_append(pOutput, FORMAT, zValue_name);
            }

            MXS_ERROR(FORMAT, zValue_name);
        }
    }
    else
    {
        assert(!true);
        MXS_ERROR("'%s' queried of config that is not valid.", zValue_name);
    }

    return rv;
}

bool Config::get_value(const char* zElement_name,
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

Status::Status(const http::Response& response)
    : Result(response)
{
    construct();
}

Status::Status(const http::Response& response,
               std::unique_ptr<json_t> sJson)
    : Result(response, std::move(sJson))
{
    construct();
}

void Status::construct()
{
    if (response.is_success() && sJson)
    {
        json_t* pCluster_mode = json_object_get(sJson.get(), cs::body::CLUSTER_MODE);
        json_t* pDbrm_mode = json_object_get(sJson.get(), cs::body::DBRM_MODE);
        json_t* pDbroots = json_object_get(sJson.get(), cs::body::DBROOTS);
        json_t* pServices = json_object_get(sJson.get(), cs::body::SERVICES);

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
                          "to actual values: %s",
                          zCluster_mode, zDbrm_mode, cs::body::DBROOTS, cs::body::SERVICES, response.body.c_str());
                mxb_assert(!true);
            }
        }
        else
        {
            MXS_ERROR("Obtained status object does not have the keys '%s', '%s', '%s' or '%s: %s",
                      cs::body::CLUSTER_MODE,
                      cs::body::DBRM_MODE,
                      cs::body::DBROOTS,
                      cs::body::SERVICES,
                      response.body.c_str());
            mxb_assert(!true);
        }
    }
}

const char* to_string(Version version)
{
    switch (version)
    {
    case CS_15:
        return ZCS_15;

    case CS_UNKNOWN:
        return "unknown";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

const char* to_string(ClusterMode cluster_mode)
{
    switch (cluster_mode)
    {
    case READONLY:
        return "readonly";

    case READWRITE:
        return "readwrite";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode)
{
    bool rv = true;

    if (strcmp(zCluster_mode, "readonly") == 0)
    {
        *pCluster_mode = READONLY;
    }
    else if (strcmp(zCluster_mode, "readwrite") == 0)
    {
        *pCluster_mode = READWRITE;
    }
    else
    {
        rv = false;
    }

    return rv;
}

const char* to_string(DbrmMode dbrm_mode)
{
    switch (dbrm_mode)
    {
    case MASTER:
        return "master";

    case SLAVE:
        return "slave";

    case OFFLINE:
        return "offline";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zDbrm_mode, DbrmMode* pDbrm_mode)
{
    bool rv = true;

    if (strcmp(zDbrm_mode, "master") == 0)
    {
        *pDbrm_mode = MASTER;
    }
    else if (strcmp(zDbrm_mode, "slave") == 0)
    {
        *pDbrm_mode = SLAVE;
    }
    else if (strcmp(zDbrm_mode, "offline") == 0)
    {
        *pDbrm_mode = OFFLINE;
    }
    else
    {
        rv = false;
    }

    return rv;
}

const char* rest::to_string(rest::Action action)
{
    switch (action)
    {
    case ADD_NODE:
        return "add-node";

    case BEGIN:
        return "begin";

    case COMMIT:
        return "commit";

    case CONFIG:
        return "config";

    case REMOVE_NODE:
        return "remove-node";

    case ROLLBACK:
        return "rollback";

    case SHUTDOWN:
        return "shutdown";

    case STATUS:
        return "status";

    case START:
        return "start";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc)
{
    psDoc->reset(xmlReadMemory(zXml, strlen(zXml), "columnstore.xml", NULL, 0));
    return *psDoc ? true : false;
}

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp)
{
    struct tm tm;
    bool rv = strptime(zTimestamp, "%Y-%m-%d %H:%M:%S", &tm) != nullptr;

    if (rv)
    {
        *pTimestamp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    }

    return rv;
}

bool dbroots_from_array(json_t* pArray, DbRootIdVector* pDbroots)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        DbRootIdVector dbroots;

        size_t i;
        json_t* pValue;
        json_array_foreach(pArray, i, pValue)
        {
            dbroots.push_back(json_integer_value(json_array_get(pArray, i)));
        }

        pDbroots->swap(dbroots);
    }

    return rv;
}

bool services_from_array(json_t* pArray, ServiceVector* pServices)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        ServiceVector services;

        size_t i;
        json_t* pService;
        json_array_foreach(pArray, i, pService)
        {
            json_t* pName = json_object_get(pService, cs::body::NAME);
            mxb_assert(pName);
            json_t* pPid = json_object_get(pService, cs::body::PID);
            mxb_assert(pPid);

            if (pName && pPid)
            {
                auto zName = json_string_value(pName);
                auto pid = json_integer_value(pPid);

                services.emplace_back(zName, pid);
            }
            else
            {
                MXS_ERROR("Object in services array does not have 'name' and/or 'pid' fields.");
            }
        }

        pServices->swap(services);
    }

    return rv;
}

string rest::create_url(const string& host,
                        int64_t port,
                        const string& rest_base,
                        Scope scope,
                        rest::Action action)
{
    string url("https://");
    url += host;
    url += ":";
    url += std::to_string(port);
    url += rest_base;

    if (scope == NODE)
    {
        url += "/node/";
    }
    else
    {
        mxb_assert(scope == CLUSTER);
        url += "/cluster/";
    }

    url += to_string(action);

    return url;
}

vector<string> rest::create_urls(const std::vector<std::string>& hosts,
                                 int64_t port,
                                 const std::string& rest_base,
                                 Scope scope,
                                 Action action)
{
    vector<string> urls;

    for (const auto& host : hosts)
    {
        urls.push_back(create_url(host, port, rest_base, scope, action));
    }

    return urls;
}

namespace body
{

namespace
{
string begin_or_commit(const std::chrono::seconds& timeout, int id)
{
    std::ostringstream body;
    body << "{\"" << TIMEOUT << "\": "
         << timeout.count()
         << ", \"" << ID << "\": "
         << id
         << "}";

    return body.str();
}

string start_or_shutdown(const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{";

    if (timeout.count() != 0)
    {
        body << "\"" << TIMEOUT << "\": " << timeout.count();
    }

    body << "}";

    return body.str();
}

string add_or_remove_node(const std::string& node, const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{\"" << TIMEOUT << "\": "
         << timeout.count()
         << ", \"" << NODE << "\": \""
         << node
         << "\"}";

    return body.str();
}

}

string add_node(const std::string& node, const std::chrono::seconds& timeout)
{
    return add_or_remove_node(node, timeout);
}

string begin(const std::chrono::seconds& timeout, int id)
{
    return begin_or_commit(timeout, id);
}

string commit(const std::chrono::seconds& timeout, int id)
{
    return begin_or_commit(timeout, id);
}

string config(const xmlDoc& csXml,
              int revision,
              const string& manager,
              const std::chrono::seconds& timeout)
{
    xmlChar* pConfig = nullptr;
    int size = 0;

    xmlDocDumpMemory(const_cast<xmlDoc*>(&csXml), &pConfig, &size);

    json_t* pBody = json_object();
    json_object_set_new(pBody, CONFIG, json_stringn(reinterpret_cast<const char*>(pConfig), size));
    json_object_set_new(pBody, REVISION, json_integer(revision));
    json_object_set_new(pBody, MANAGER, json_string(manager.c_str()));
    json_object_set_new(pBody, TIMEOUT, json_integer(timeout.count()));

    xmlFree(pConfig);

    char* zBody = json_dumps(pBody, 0);
    json_decref(pBody);

    string body(zBody);
    MXS_FREE(zBody);

    return body;
}

std::string config_set_cluster_mode(ClusterMode mode,
                                    int revision,
                                    const std::string& manager,
                                    const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{"
         << "\"" << CLUSTER_MODE << "\": " << "\"" << cs::to_string(mode) << "\", "
         << "\"" << REVISION << "\": " << revision << ","
         << "\"" << TIMEOUT << "\": " << timeout.count() << ","
         << "\"" << MANAGER << "\": " << "\"" << manager << "\""
         << "}";

    return body.str();
}

string remove_node(const std::string& node, const std::chrono::seconds& timeout)
{
    return add_or_remove_node(node, timeout);
}

string rollback(int id)
{
    std::ostringstream body;
    body << "{"
         << "\"" << ID << "\": " << id
         << "}";

    return body.str();
}

string shutdown(const std::chrono::seconds& timeout)
{
    return start_or_shutdown(timeout);
}

string start(const std::chrono::seconds& timeout)
{
    return start_or_shutdown(timeout);
}

}

Result fetch_cluster_status(const std::string& host,
                            int64_t admin_port,
                            const std::string& admin_base_path,
                            const mxb::http::Config& http_config,
                            std::map<std::string, Status>* pStatuses)
{
    string url = rest::create_url(host, admin_port, admin_base_path,
                                  cs::rest::CLUSTER,
                                  cs::rest::STATUS);
    auto response = http::get(url, http_config);
    Result result { response };

    if (result.ok())
    {
        if (result.sJson)
        {
            map<string,Status> statuses;
            const char* zKey;
            json_t* pValue;
            json_object_foreach(result.sJson.get(), zKey, pValue)
            {
                // There are values like 'timestamp' as well. If it is an
                // object, we assume it is a status.
                if (json_typeof(pValue) == JSON_OBJECT)
                {
                    unique_ptr<json_t> sJson(json_incref(pValue));

                    statuses.insert(make_pair(zKey, Status(response, std::move(sJson))));
                }
            }

            pStatuses->swap(statuses);
        }
    }

    return result;
}

bool fetch_configs(const std::vector<std::string>& hosts,
                   int64_t admin_port,
                   const std::string& admin_base_path,
                   const mxb::http::Config& http_config,
                   std::vector<Config>* pConfigs)
{
    auto urls = create_urls(hosts, admin_port, admin_base_path, rest::NODE, rest::CONFIG);
    vector<http::Response> responses = http::get(urls, http_config);

    mxb_assert(hosts.size() == responses.size());

    bool rv = true;

    vector<Config> configs;
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

}
