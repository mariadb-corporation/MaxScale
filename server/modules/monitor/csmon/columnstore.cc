/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
#include "columnstore.hh"

using std::string;
using std::vector;

namespace
{
// TODO: This is just the mockup Columnstore daemon.
const char REST_BASE[] = "/drrtuy/cmapi/0.0.2/node/";
}

namespace cs
{

const char* to_string(ClusterMode cluster_mode)
{
    switch (cluster_mode)
    {
    case READ_ONLY:
        return "read-only";

    case READ_WRITE:
        return "read-write";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode)
{
    bool rv = true;

    if (strcmp(zCluster_mode, "read-only") == 0
        || strcmp(zCluster_mode, "read_only") == 0)
    {
        *pCluster_mode = READ_ONLY;
    }
    else if (strcmp(zCluster_mode, "read-write") == 0
             || strcmp(zCluster_mode, "read_write") == 0)
    {
        *pCluster_mode = READ_WRITE;
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
    case BEGIN:
        return "begin";

    case COMMIT:
        return "commit";

    case CONFIG:
        return "config";

    case PING:
        return "ping";

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

bool dbroots_from_array(json_t* pArray, std::vector<int>* pDbroots)
{
    bool rv = json_is_array(pArray);

    if (rv)
    {
        vector<int> dbroots;
        size_t size = json_array_size(pArray);
        for (size_t i = 0; i < size; ++i)
        {
            dbroots.push_back(json_integer_value(json_array_get(pArray, i)));
        }

        pDbroots->swap(dbroots);
    }

    return rv;
}

std::string rest::create_url(const SERVER& server, int64_t port, rest::Action action)
{
    string url("https://");
    url += server.address();
    url += ":";
    url += std::to_string(port);
    url += REST_BASE;

    url += to_string(action);

    return url;
}

}
