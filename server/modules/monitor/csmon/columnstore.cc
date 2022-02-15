/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#include "columnstore.hh"
#include <map>
#include "csxml.hh"

using std::map;
using std::string;
using std::vector;

namespace
{

bool get_number(const char* zNumber, long* pNumber)
{
    char* zEnd;
    errno       = 0;
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

}  // namespace

namespace cs
{

const char* to_string(Version version)
{
    switch (version)
    {
    case CS_10:
        return ZCS_10;

    case CS_12:
        return ZCS_12;

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
                auto pid   = json_integer_value(pPid);

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

string rest::create_url(
    const SERVER& server, int64_t port, const string& rest_base, Scope scope, rest::Action action)
{
    string url("https://");
    url += server.address();
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

namespace body
{

namespace
{
string begin_or_commit(const std::chrono::seconds& timeout, int id)
{
    std::ostringstream body;
    body << "{\"" << TIMEOUT << "\": " << timeout.count() << ", \"" << ID << "\": " << id << "}";

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
    body << "{\"" << TIMEOUT << "\": " << timeout.count() << ", \"" << NODE << "\": \"" << node << "\"}";

    return body.str();
}

}  // namespace

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

string config(const xmlDoc& csXml, int revision, const string& manager, const std::chrono::seconds& timeout)
{
    xmlChar* pConfig = nullptr;
    int size         = 0;

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

std::string config_set_cluster_mode(
    ClusterMode mode, int revision, const std::string& manager, const std::chrono::seconds& timeout)
{
    std::ostringstream body;
    body << "{"
         << "\"" << CLUSTER_MODE << "\": "
         << "\"" << cs::to_string(mode) << "\", "
         << "\"" << REVISION << "\": " << revision << ","
         << "\"" << TIMEOUT << "\": " << timeout.count() << ","
         << "\"" << MANAGER << "\": "
         << "\"" << manager << "\""
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
         << "\"" << ID << "\": " << id << "}";

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

}  // namespace body
}  // namespace cs
