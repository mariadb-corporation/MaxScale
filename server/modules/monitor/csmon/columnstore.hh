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
#pragma once

#include "csmon.hh"
#include <vector>


namespace cs
{

enum ClusterMode
{
    READ_ONLY,
    READ_WRITE,
};

const char* to_string(ClusterMode cluster_mode);
bool from_string(const char* zCluster_mode, ClusterMode* pCluster_mode);

enum DbrmMode
{
    MASTER,
    SLAVE,
};

const char* to_string(DbrmMode dbrm_mode);
bool from_string(const char* zDbrm_mode, DbrmMode* pDbrm_mode);

using DbRoots = std::vector<int>;
using Services = std::vector<std::pair<std::string,int>>;

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp);
bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc);
bool dbroots_from_array(json_t* pArray, DbRoots* pDbroots);
bool services_from_array(json_t* pArray, Services* pServices);

/**
 * Update value of key(s) in XML document.
 *
 * @param xmlDoc      The XML document.
 * @param zXpath      The XML path that identifies the key(s).
 * @param zNew_value  The new value.
 * @param zIf_value   If non-NULL, what the previous value must be for the replacement to be done.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of replacements made.
 */
int update_if(xmlDoc& xmlDoc, const char* zXpath, const char* zNew_value, const char* zIf_value = nullptr);

enum class XmlLocation
{
    AT_BEGINNING,
    AT_END
};

/**
 * Insert new key/value to XML document.
 *
 * @param xmlDoc   The XML document.
 * @param zKey     The key.
 * @param zValue   The value.
 * @param location Where the element should be added.
 *
 * @return True, whether the value could be added.
 */
bool insert(xmlDoc& xmlDoc,
            const char* zKey,
            const char* zValue,
            XmlLocation location = XmlLocation::AT_BEGINNING);

/**
 * Update or insert a key/value to XML document.
 *
 * @param xmlDoc   The XML document.
 * @param zXpath   The XML path identifying the key(s).
 * @param zValue   The value.
 * @param location If inserted, where the element should be added.
 *
 * If an existing key is not found, the new key is deduced from @c zXpath.
 *
 * @return -1 in case of some low-level error (that outside development should not occur), otherwise
 *         the number of updates/inserts made.
 */
int upsert(xmlDoc& xmlDoc,
           const char* zXpath,
           const char* zValue,
           XmlLocation location = XmlLocation::AT_BEGINNING);

namespace keys
{

const char CONFIG[]       = "config";
const char CLUSTER_MODE[] = "cluster_mode";
const char DBRM_MODE[]    = "dbrm_mode";
const char DBROOTS[]      = "dbroots";
const char MODE[]         = "mode";
const char NAME[]         = "name";
const char PID[]          = "pid";
const char SERVICES[]     = "services";
const char TIMEOUT[]      = "timeout";
const char TIMESTAMP[]    = "timestamp";
const char TXN[]          = "txn";

}

namespace xml
{
const char DBRM_CONTROLLER[] = "DBRM_Controller";
const char DDLPROC[]         = "DDLProc";
const char DMLPROC[]         = "DMLProc";
const char IPADDR[]          = "IPAddr";
}

namespace rest
{
enum Action {
    BEGIN,
    COMMIT,
    CONFIG,
    PING,
    ROLLBACK,
    SHUTDOWN,
    START,
    STATUS,
};

const char* to_string(Action action);

std::string create_url(const SERVER& server, int64_t port, const std::string& rest_base, Action action);

inline std::string create_url(const mxs::MonitorServer& mserver,
                              int64_t port,
                              const std::string& rest_base,
                              Action action)
{
    return create_url(*mserver.server, port, rest_base, action);
}

}
}
