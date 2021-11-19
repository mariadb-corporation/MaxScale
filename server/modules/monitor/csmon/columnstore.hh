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
#pragma once

#include "csmon.hh"
#include <vector>
#include <maxbase/http.hh>
#include <maxbase/xml.hh>

namespace cs
{

const char ZCS_15[] = "1.5";

enum Version
{
    CS_UNKNOWN,
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
    OFFLINE,
};

const char* to_string(DbrmMode dbrm_mode);
bool from_string(const char* zDbrm_mode, DbrmMode* pDbrm_mode);

using DbRootIdVector = std::vector<int>;
using ServiceVector = std::vector<std::pair<std::string,int>>;

bool from_string(const char* zTimestamp, std::chrono::system_clock::time_point* pTimestamp);
bool from_string(const char* zXml, std::unique_ptr<xmlDoc>* psDoc);
bool dbroots_from_array(json_t* pArray, DbRootIdVector* pDbroots);
bool services_from_array(json_t* pArray, ServiceVector* pServices);

namespace xml
{
const char CONFIGREVISION[]        = "ConfigRevision";
const char DBRM_CONTROLLER[]       = "DBRM_Controller";
const char DDLPROC[]               = "DDLProc";
const char DMLPROC[]               = "DMLProc";
const char IPADDR[]                = "IPAddr";
}

namespace rest
{
enum Action {
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

std::string create_url(const std::string& host,
                       int64_t port,
                       const std::string& rest_base,
                       Scope scope,
                       Action action);

inline std::string create_url(const SERVER& server,
                       int64_t port,
                       const std::string& rest_base,
                       Scope scope,
                       Action action)
{
    return create_url(server.address(), port, rest_base, scope, action);
}

inline std::string create_url(const mxs::MonitorServer& mserver,
                              int64_t port,
                              const std::string& rest_base,
                              Scope scope,
                              Action action)
{
    return create_url(*mserver.server, port, rest_base, scope, action);
}

std::vector<std::string> create_urls(const std::vector<std::string>& hosts,
                                     int64_t port,
                                     const std::string& rest_base,
                                     Scope scope,
                                     Action action);

}

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
std::string config(const xmlDoc& csXml,
                   int revision,
                   const std::string& manager,
                   const std::chrono::seconds& timeout);

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
std::string config_set_cluster_mode(ClusterMode mode,
                                    int revision,
                                    const std::string& manager,
                                    const std::chrono::seconds& timeout);

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

}

class Result
{
public:
    Result() {}
    Result(const mxb::http::Response& response);
    Result(const mxb::http::Response& response, std::unique_ptr<json_t> sJson);

    Result(Result&& other) = default;
    Result& operator=(Result&& rhs) = default;

    bool ok() const
    {
        return response.is_success() && sJson;
    }

    mxb::http::Response     response;
    std::unique_ptr<json_t> sJson;
};

class Status : public Result
{
public:
    Status(const mxb::http::Response& response);
    Status(const mxb::http::Response& response, std::unique_ptr<json_t> sJson);

    Status(Status&& other) = default;
    Status& operator=(Status&& rhs) = default;

    cs::ClusterMode      cluster_mode = cs::READONLY;
    cs::DbrmMode         dbrm_mode = cs::SLAVE;
    cs::DbRootIdVector   dbroots;
    cs::ServiceVector    services;
    std::chrono::seconds uptime;

private:
    void construct();

    static int64_t s_uptime;
};

class Config : public Result
{
public:
    Config(const mxb::http::Response& response);

    Config(Config&& other) = default;
    Config& operator=(Config&& rhs) = default;

    bool ok() const
    {
        return Result::ok() && sXml;
    }

    bool get_revision(int* pRevision, json_t* pOutput = nullptr) const
    {
        return get_value(cs::xml::CONFIGREVISION, pRevision, pOutput);
    }

    int revision() const
    {
        int r = -1;
        MXB_AT_DEBUG(bool rv=) get_revision(&r);
        mxb_assert(rv);
        return r;
    }

    bool get_dbrm_controller_ip(std::string* pIp, json_t* pOutput = nullptr) const
    {
        return get_value(cs::xml::DBRM_CONTROLLER, cs::xml::IPADDR, pIp, pOutput);
    }

    bool get_ddlproc_ip(std::string* pIp, json_t* pOutput = nullptr) const
    {
        return get_value(cs::xml::DDLPROC, cs::xml::IPADDR, pIp, pOutput);
    }

    bool get_dmlproc_ip(std::string* pIp, json_t* pOutput = nullptr) const
    {
        return get_value(cs::xml::DMLPROC, cs::xml::IPADDR, pIp, pOutput);
    }

    using time_point = std::chrono::system_clock::time_point;

    time_point              timestamp;
    std::unique_ptr<xmlDoc> sXml;

private:
    bool get_value(const char* zValue_name,
                   int* pValue,
                   json_t* pOutput) const;

    bool get_value(const char* zElement_name,
                   const char* zValue_name,
                   std::string* pValue,
                   json_t* pOutput) const;
};

/**
 * Fetch cluster status from a specific node.
 *
 * @param host              The hostname (or IP) of the node.
 * @param admin_port        The admin daemon port.
 * @param admin_base_path   The base part of the REST URL.
 * @param http_config       The http config to use.
 * @param pRv               On successful return the statuses of all nodes as seen
 *                          from @c host.
 *
 * @return The result of the operation. If the result is ok, then @c pRv
 *         will contain the statuses.
 */
Result fetch_cluster_status(const std::string& host,
                            int64_t admin_port,
                            const std::string& admin_base_path,
                            const mxb::http::Config& http_config,
                            std::map<std::string, Status>* pRv);

/**
 * Fetch cluster config from several hosts
 *
 * @param hosts             The hostnames (or IPs) of the nodes.
 * @param admin_port        The admin daemon port.
 * @param admin_base_path   The base part of the REST URL.
 * @param http_config       The http config to use.
 * @param pConfigs          The configs of each host.
 *
 * @return True, if all configs could be fetched, false otherwise.
 */
bool fetch_configs(const std::vector<std::string>& hosts,
                   int64_t admin_port,
                   const std::string& admin_base_path,
                   const mxb::http::Config& http_config,
                   std::vector<Config>* pConfigs);

}
