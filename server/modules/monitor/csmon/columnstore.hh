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
#pragma once

#include "csmon.hh"
#include <vector>
#include <maxbase/xml.hh>

namespace cs
{

const char ZCS_10[] = "1.0";
const char ZCS_12[] = "1.2";
const char ZCS_15[] = "1.5";

enum Version
{
    CS_UNKNOWN,
    CS_10,
    CS_12,
    CS_15
};

const char* to_string(Version version);

int get_minor_version(int full_version);

enum ClusterMode
{
    READONLY,
    READWRITE,
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

using DbRootIdVector = std::vector<int>;
using ServiceVector  = std::vector<std::pair<std::string, int>>;

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp);
bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc);
bool dbroots_from_array(json_t* pArray, DbRootIdVector* pDbroots);
bool services_from_array(json_t* pArray, ServiceVector* pServices);

namespace xml
{
const char DBRM_CONTROLLER[] = "DBRM_Controller";
const char DDLPROC[]         = "DDLProc";
const char DMLPROC[]         = "DMLProc";
const char IPADDR[]          = "IPAddr";
}  // namespace xml

namespace rest
{
enum Action
{
    ADD_NODE,
    BEGIN,
    COMMIT,
    CONFIG,
    REMOVE_NODE,
    ROLLBACK,
    SHUTDOWN,
    START,
    STATUS,
};

const char* to_string(Action action);

enum Scope
{
    CLUSTER,
    NODE
};

std::string create_url(
    const SERVER& server, int64_t port, const std::string& rest_base, Scope scope, Action action);

inline std::string create_url(
    const mxs::MonitorServer& mserver, int64_t port, const std::string& rest_base, Scope scope, Action action)
{
    return create_url(*mserver.server, port, rest_base, scope, action);
}

}  // namespace rest

namespace body
{

const char CONFIG[]       = "config";
const char CLUSTER_MODE[] = "cluster_mode";
const char DBRM_MODE[]    = "dbrm_mode";
const char DBROOTS[]      = "dbroots";
const char ID[]           = "id";
const char MANAGER[]      = "manager";
const char MODE[]         = "mode";
const char NAME[]         = "name";
const char NODE[]         = "node";
const char PID[]          = "pid";
const char REVISION[]     = "revision";
const char SERVICES[]     = "services";
const char TIMEOUT[]      = "timeout";
const char TIMESTAMP[]    = "timestamp";
const char TXN[]          = "txn";

/**
 * @brief JSON body to be used with PUT /cluster/add-node
 *
 * @param node     The host or IP of the node.
 * @param timeout  The timeout of the operation.
 *
 * @return REST-API body.
 */
std::string add_node(const std::string& node, const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /node/begin
 *
 * @param timeout  The timeout.
 * @param id       The transaction id.
 *
 * @return REST-API body.
 */
std::string begin(const std::chrono::seconds& timeout, int id);

/**
 * @brief JSON body to be used with PUT /node/commit
 *
 * @param timeout  The timeout.
 * @param id       The transaction id.
 *
 * @return REST-API body.
 */
std::string commit(const std::chrono::seconds& timeout, int id);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * @param csXml     The Columnstore configuration.
 * @param revision  The revision of the configuration.
 * @param manager   The manager doing the modification.
 * @param timeout   The timeout.
 *
 * @return REST-API body.
 */
std::string config(
    const xmlDoc& csXml, int revision, const std::string& manager, const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /node/config
 *
 * @param mode     The cluster mode.
 * @param revision The revision of the configuration.
 * @param manager  The manager doing the modification.
 * @param timeout  The timeout.
 *
 * @return REST-API body.
 */
std::string config_set_cluster_mode(
    ClusterMode mode, int revision, const std::string& manager, const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /cluster/remove-node
 *
 * @param node     The host or IP of the node.
 * @param timeout  The timeout of the operation.
 *
 * @return REST-API body.
 */
std::string remove_node(const std::string& node, const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /node/rollback
 *
 * @param id  The transaction id.
 *
 * @return REST-API body.
 */
std::string rollback(int id);

/**
 * @brief JSON body to be used with PUT /cluster/shutdown
 *
 * @param timeout  The timeout.
 */
std::string shutdown(const std::chrono::seconds& timeout);

/**
 * @brief JSON body to be used with PUT /cluster/start
 *
 * @param timeout  The timeout.
 */
std::string start(const std::chrono::seconds& timeout);

}  // namespace body

}  // namespace cs
