/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "csmonitor.hh"

#include <regex>
#include <vector>
#include <string>
#include <sstream>
#include <mysql.h>

#include <maxscale/modinfo.hh>
#include <maxscale/mysql_utils.hh>
#include "columnstore.hh"

using std::ostringstream;
using std::string;
using std::unique_ptr;
using std::vector;
using maxscale::MonitorServer;
namespace http = mxb::http;

namespace
{

constexpr const char* alive_query = "SELECT mcsSystemReady() = 1 && mcsSystemReadOnly() <> 2";
constexpr const char* role_query = "SELECT mcsSystemPrimary()";

// Helper for extracting string results from queries
static std::string do_query(MonitorServer* srv, const char* query)
{
    std::string rval;
    MYSQL_RES* result;

    if (mxs_mysql_query(srv->con, query) == 0 && (result = mysql_store_result(srv->con)))
    {
        MYSQL_ROW row = mysql_fetch_row(result);

        if (row && row[0])
        {
            rval = row[0];
        }

        mysql_free_result(result);
    }
    else
    {
        srv->mon_report_query_error();
    }

    return rval;
}

// Returns a numeric version similar to mysql_get_server_version
int get_cs_version(MonitorServer* srv)
{
    int rval = -1;
    std::string prefix = "Columnstore ";
    std::string result = do_query(srv, "SELECT @@version_comment");
    auto pos = result.find(prefix);

    auto to_version = [](std::string str) {
            std::istringstream os(str);
            int major = 0, minor = 0, patch = 0;
            char dot;
            os >> major;
            os >> dot;
            os >> minor;
            os >> dot;
            os >> patch;
            return major * 10000 + minor * 100 + patch;
    };

    if (pos != std::string::npos)
    {
        rval = to_version(result.substr(pos + prefix.length()));
    }
    else
    {
        auto cs_version = do_query(srv, "SELECT VARIABLE_VALUE FROM information_schema.GLOBAL_STATUS "
                                   "WHERE VARIABLE_NAME = 'Columnstore_version'");

        if (!cs_version.empty())
        {
            rval = to_version(cs_version);
        }
    }
    // TODO: Temporary fix, the mock server does not return the right version.
    rval = 10500;
    return rval;
}

json_t* create_response(const vector<CsMonitorServer*>& servers,
                        const vector<http::Result>&     results,
                        CsMonitor::ResponseHandler      handler = nullptr)
{
    mxb_assert(servers.size() == results.size());

    json_t* pResponse = json_object();

    auto it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        if (handler)
        {
            handler(pServer, result, pResponse);
        }
        else
        {
            json_t* pResult = json_object();
            json_object_set_new(pResult, "code", json_integer(result.code));
            json_object_set_new(pResult, "message", json_string(result.body.c_str()));

            json_object_set_new(pResponse, pServer->name(), pResult);
        }

        ++it;
        ++jt;
    }

    return pResponse;
}

json_t* create_response(const vector<CsMonitorServer*>& servers,
                        const http::Async&              result,
                        CsMonitor::ResponseHandler      handler = nullptr)
{
    json_t* pResult = nullptr;

    if (result.status() == http::Async::ERROR)
    {
        LOG_APPEND_JSON_ERROR(&pResult, "Fatal HTTP error.");
    }
    else
    {
        pResult = create_response(servers, result.results(), handler);
    }

    return pResult;
}


vector<http::Result>::const_iterator find_first_failed(const vector<http::Result>& results)
{
    return std::find_if(results.begin(), results.end(), [](const http::Result& result) -> bool {
            return result.code != 200;
        });
}

vector<http::Result>::iterator find_first_failed(vector<http::Result>& results)
{
    return std::find_if(results.begin(), results.end(), [](const http::Result& result) -> bool {
            return result.code != 200;
        });
}

int code_from_result(const CsMonitorServer::Config& config)
{
    return config.response.code;
}

int code_from_result(const CsMonitorServer::Status& status)
{
    return status.response.code;
}

int code_from_result(const http::Result& response)
{
    return response.code;
}

json_t* result_to_json(const CsMonitorServer& server, const CsMonitorServer::Config& config)
{
    json_t* pResult = nullptr;

    if (config.sJson)
    {
        pResult = config.sJson.get();
        json_incref(pResult);
    }

    return pResult;
}

json_t* result_to_json(const CsMonitorServer& server, const CsMonitorServer::Status& status)
{
    json_t* pResult = nullptr;

    if (status.sJson)
    {
        pResult = status.sJson.get();
        json_incref(pResult);

#if defined(CSMON_EXPOSE_TRANSACTIONS)
        json_object_set_new(pResult, "csmon_trx_active", json_boolean(server.in_trx()));
#endif
    }

    return pResult;
}

json_t* result_to_json(const CsMonitorServer& server, const http::Result& result)
{
    json_t* pResult = nullptr;

    if (!result.body.empty())
    {
        json_error_t error;
        pResult = json_loadb(result.body.c_str(), result.body.length(), 0, &error);

        if (!pResult)
        {
            MXS_ERROR("Server '%s' returned '%s' that is not valid JSON: %s",
                      server.name(), result.body.c_str(), error.text);
        }
    }

    return pResult;
}

template<class T>
inline bool is_ok(const T& t)
{
    return t.ok();
}

inline bool is_ok(const http::Result& result)
{
    return result.is_success();
}


template<class T>
size_t results_to_json(const vector<CsMonitorServer*>& servers,
                       const vector<T>& results,
                       json_t** ppArray)
{
    auto it = servers.begin();
    auto end = servers.end();
    auto jt = results.begin();

    size_t n = 0;

    json_t* pArray = json_array();

    while (it != end)
    {
        auto* pServer = *it;
        const auto& result = *jt;

        if (is_ok(result))
        {
            ++n;
        }

        json_t* pResult = result_to_json(*pServer, result);

        json_t* pObject = json_object();
        json_object_set_new(pObject, "name", json_string(pServer->name()));
        json_object_set_new(pObject, "code", json_integer(code_from_result(result)));
        if (pResult)
        {
            json_object_set_new(pObject, "result", pResult);
        }

        json_array_append_new(pArray, pObject);

        ++it;
        ++jt;
    }

    *ppArray = pArray;

    return n;
}

string next_trx_id(const char* zWhat)
{
    static int64_t id = 1;

    return string("maxscale-") + zWhat + string("-") + std::to_string(id++);
}

}


CsMonitor::CsMonitor(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
    , m_config(name)
{
    // The CS daemon uses a self-signed certificate.
    m_http_config.ssl_verifypeer = false;
    m_http_config.ssl_verifyhost = false;
}

CsMonitor::~CsMonitor()
{
}

// static
CsMonitor* CsMonitor::create(const std::string& name, const std::string& module)
{
    return new CsMonitor(name, module);
}

bool CsMonitor::has_sufficient_permissions()
{
    bool rv = test_permissions(alive_query);

    if (rv)
    {
        CsMonitorServer* pSmallest_server = nullptr;
        m_version = 0;

        const auto& sv = servers();
        for (auto* pServer : sv)
        {
            auto result = pServer->ping_or_connect();

            if (connection_is_ok(result))
            {
                auto version = get_cs_version(pServer);

                if (m_version == 0)
                {
                    m_version = version;
                }
                else if (version != m_version)
                {
                    MXS_WARNING("Version %d of '%s' is at least different than version %d of '%s'.",
                                version, pServer->name(), m_version, pSmallest_server->name());

                    if (version < m_version)
                    {
                        m_version = version;
                        pSmallest_server = pServer;
                    }
                }
            }
            else
            {
                // This should not happen as test_permissions() succeeded.
                MXS_ERROR("Could not connect to '%s'.", pServer->name());
            }
        }

        if (m_version == 0)
        {
            MXS_WARNING("Could not connect to any server. The Columnstore monitor will "
                        "initially assume nothing of the cluster.");
        }
        else
        {
            MXS_NOTICE("Columnstore monitor will adjust its behaviour according to version %d.",
                       m_version);
        }

        if (m_version >= 10500)
        {
            auto configs = CsMonitorServer::fetch_configs(sv, m_http_config);

            auto it = sv.begin();
            auto end = sv.end();
            auto jt = configs.begin();

            size_t n = 0;

            while (it != end)
            {
                auto* pServer = *it;
                const auto& config = *jt;

                if (config.ok())
                {
                    string ip;
                    if (config.get_dbrm_controller_ip(&ip))
                    {
                        if (ip == "127.0.0.1")
                        {
                            pServer->set_state(CsMonitorServer::SINGLE_NODE);

                            if (sv.size() > 1)
                            {
                                MXS_WARNING("Server '%s' must be added as a node to the cluster "
                                            "before MaxScale can use it.",
                                            pServer->name());
                                ++n;
                            }
                        }
                        else if (ip == pServer->address())
                        {
                            pServer->set_state(CsMonitorServer::MULTI_NODE);
                        }
                        else
                        {
                            MXS_ERROR("MaxScale thinks the IP address of the server '%s' is %s, "
                                      "while the server itself thinks it is %s.",
                                      pServer->name(), pServer->address(), ip.c_str());
                            rv = false;
                        }
                    }
                    else
                    {
                        MXS_ERROR("Could not get DMRM_Controller IP of '%s'.", pServer->name());
                        rv = false;
                    }
                }
                else
                {
                    MXS_ERROR("Could not fetch config from '%s': (%d) %s",
                              pServer->name(), config.response.code, config.response.body.c_str());
                    rv = false;
                }

                ++it;
                ++jt;
            }

            if (n == sv.size())
            {
                MXS_WARNING("No servers are properly configured. They must be added as nodes "
                            "to the cluster.");
            }
        }
    }

    return rv;
}

void CsMonitor::update_server_status(MonitorServer* pS)
{
    CsMonitorServer* pServer = static_cast<CsMonitorServer*>(pS);

    pServer->clear_pending_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
    int status_mask = 0;

    if (do_query(pServer, alive_query) == "1")
    {
        if (m_version >= 0)
        {
            status_mask |= SERVER_RUNNING;

            if (m_version >= 10500)
            {
                auto status = pServer->fetch_status();

                int status_bit = 0;

                if (status.ok())
                {
                    if (!status.services.empty())
                    {
                        // Seems to be running.
                        if (status.dbrm_mode == cs::MASTER)
                        {
                            status_bit |= SERVER_MASTER;
                        }
                        else
                        {
                            status_bit |= SERVER_SLAVE;
                        }
                    }
                    else
                    {
                        MXS_ERROR("Columnstore daemon on '%s' replied, but with no services so "
                                  "result cannot be trusted.", pServer->name());
                    }
                }
                else
                {
                    MXS_ERROR("Could not fetch status using REST-API from '%s': (%d) %s",
                              pServer->name(), status.response.code, status.response.body.c_str());
                }

                if (status_bit == 0)
                {
                    MXS_NOTICE("Could not get server status using REST-API, reverting to SQL.");
                    status_bit = do_query(pServer, role_query) == "1" ? SERVER_MASTER : SERVER_SLAVE;
                }

                status_mask |= status_bit;
            }
            else if (m_version >= 10200)
            {
                // 1.2 supports the mcsSystemPrimary function
                status_mask |= do_query(pServer, role_query) == "1" ? SERVER_MASTER : SERVER_SLAVE;
            }
            else
            {
                status_mask |= pServer->server == m_config.pPrimary ? SERVER_MASTER : SERVER_SLAVE;
            }
        }
    }

    pServer->set_pending_status(status_mask);
}

bool CsMonitor::configure(const mxs::ConfigParameters* pParams)
{
    bool rv = m_config.configure(*pParams);

    if (rv)
    {
        m_http_config.headers["X-API-KEY"] = m_config.api_key;
        m_http_config.headers["Content-Type"] = "application/json";

        rv = MonitorWorkerSimple::configure(pParams);
    }

    return rv;
}

namespace
{

void reject_not_running(json_t** ppOutput, const char* zCmd)
{
    LOG_APPEND_JSON_ERROR(ppOutput,
                          "The Columnstore monitor is not running, cannot "
                          "execute the command '%s'.", zCmd);
}

void reject_call_failed(json_t** ppOutput, const char* zCmd)
{
    LOG_APPEND_JSON_ERROR(ppOutput, "Failed to queue the command '%s' for execution.", zCmd);
}

void reject_command_pending(json_t** ppOutput, const char* zPending)
{
    LOG_APPEND_JSON_ERROR(ppOutput,
                          "The command '%s' is running; another command cannot "
                          "be started until that has finished. Cancel or wait.", zPending);
}

}

bool CsMonitor::command_add_node(json_t** ppOutput,
                                 CsMonitorServer* pServer,
                                 const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, timeout, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_add_node(ppOutput, &sem, pServer, timeout);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "add-node", cmd);
}

bool CsMonitor::command_config_get(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_config_get(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "config-get", cmd);
}

bool CsMonitor::command_config_set(json_t** ppOutput, const char* zJson, CsMonitorServer* pServer)
{
    bool rv = false;

    auto len = strlen(zJson);
    if (is_valid_json(ppOutput, zJson, len))
    {
        mxb::Semaphore sem;
        string body(zJson, zJson + len);

        auto cmd = [this, ppOutput, &sem, &body, pServer] () {
            if (ready_to_run(ppOutput))
            {
                cs_config_set(ppOutput, &sem, std::move(body), pServer);
            }
            else
            {
                sem.post();
            }
        };

        rv = command(ppOutput, sem, "config-put", cmd);
    }

    return rv;
}

bool CsMonitor::command_mode_set(json_t** ppOutput, const char* zMode)
{
    bool rv = false;
    cs::ClusterMode mode;

    if (cs::from_string(zMode, &mode))
    {
        mxb::Semaphore sem;

        auto cmd = [this, ppOutput, &sem, mode] () {
            if (ready_to_run(ppOutput))
            {
                cs_mode_set(ppOutput, &sem, mode);
            }
            else
            {
                sem.post();
            }
        };

        rv = command(ppOutput, sem, "mode-set", cmd);
    }
    else
    {
        LOG_APPEND_JSON_ERROR(ppOutput, "'%s' is not a valid argument.", zMode);
    }

    return rv;
}

bool CsMonitor::command_ping(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_ping(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "ping", cmd);
}

bool CsMonitor::command_remove_node(json_t** ppOutput,
                                    CsMonitorServer* pServer,
                                    const std::chrono::seconds& timeout,
                                    bool force)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, pServer, timeout, force] () {
        if (ready_to_run(ppOutput))
        {
            cs_remove_node(ppOutput, &sem, pServer, timeout, force);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "remove-node", cmd);
}

bool CsMonitor::command_scan(json_t** ppOutput,
                             CsMonitorServer* pServer,
                             const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, timeout, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_scan(ppOutput, &sem, pServer, timeout);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "scan", cmd);
}

bool CsMonitor::command_shutdown(json_t** ppOutput, const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, timeout, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_shutdown(ppOutput, &sem, timeout);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "shutdown", cmd);
}

bool CsMonitor::command_start(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_start(ppOutput, &sem);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "start", cmd);
}

bool CsMonitor::command_status(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_status(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "status", cmd);
}

#if defined(CSMON_EXPOSE_TRANSACTIONS)
bool CsMonitor::command_begin(json_t** ppOutput,
                              const std::chrono::seconds& timeout,
                              CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, timeout, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cs_begin(ppOutput, &sem, timeout, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "begin", cmd);
}

bool CsMonitor::command_commit(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cs_commit(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "commit", cmd);
}

bool CsMonitor::command_rollback(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cs_rollback(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "rollback", cmd);
}
#endif


bool CsMonitor::ready_to_run(json_t** ppOutput) const
{
    // TODO: When asynchronicity is added back, here is where it must be checked
    // TODO: whether there is a command in process or a command whose result must
    // TODO: first be retrieved.

    return true;
}

//static
bool CsMonitor::is_valid_json(json_t** ppOutput, const char* zJson, size_t len)
{
    bool rv = false;

    json_error_t error;
    json_t* pJson = json_loadb(zJson, len, 0, &error);

    if (pJson)
    {
        json_decref(pJson);
        rv = true;
    }
    else
    {
        *ppOutput = mxs_json_error_append(nullptr, "Provided string '%s' is not valid JSON: %s",
                                          zJson, error.text);

    }

    return rv;
}

bool CsMonitor::command(json_t** ppOutput, mxb::Semaphore& sem, const char* zCmd, std::function<void()> cmd)
{
    bool rv = false;

    if (!is_running())
    {
        reject_not_running(ppOutput, zCmd);
    }
    else
    {
        if (execute(cmd, EXECUTE_QUEUED))
        {
            sem.wait();
            rv = true;
        }
        else
        {
            reject_call_failed(ppOutput, zCmd);
        }
    }

    return rv;
}

namespace
{

bool is_node_part_of_cluster(const CsMonitorServer* pServer)
{
    // TODO: Only a node that exists in the MaxScale configuration but *not* in the
    // TODO: Columnstore configuration can be added.
    return false;
}

}

void CsMonitor::cs_add_node(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            CsMonitorServer* pServer,
                            const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;
    json_t* pServers = nullptr;

    const ServerVector& sv = servers();

    if (pServer->is_multi_node())
    {
        mxs_json_error_append(pOutput,
                              "The server '%s' is already a node in a cluster.",
                              pServer->name());
    }
    else if (sv.size() == 1)
    {
        success = cs_add_first_multi_node(pOutput, pServer, timeout);
    }
    else
    {
        success = cs_add_additional_multi_node(pOutput, pServer, timeout);
    }

    if (success)
    {
        message << "Server '" << pServer->name() << "' added to cluster.";
    }
    else
    {
        message << "Adding server '" << pServer->name() << "' to cluster failed.";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_config_get(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    CsMonitorServer::Configs configs = CsMonitorServer::fetch_configs(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, configs, &pServers);

    if (n == sv.size())
    {
        message << "Fetched the config from all servers.";
        success = true;
    }
    else
    {
        message << "Successfully fetched config from " << n
                << " servers out of " << servers().size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_config_set(json_t** ppOutput, mxb::Semaphore* pSem,
                              string&& body, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    http::Results results = CsMonitorServer::set_config(sv, body, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == servers().size())
    {
        message << "Config set on all servers.";
        success = true;
    }
    else
    {
        message << "Config successfully set on " << n
                << " servers out of " << servers().size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_mode_set(json_t** ppOutput, mxb::Semaphore* pSem, cs::ClusterMode mode)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    success = CsMonitorServer::set_mode(sv, mode, m_http_config, &pOutput);

    if (success)
    {
        message << "Cluster mode successfully set.";
    }
    else
    {
        message << "Could not set cluster mode.";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_ping(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    http::Results results = CsMonitorServer::ping(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == servers().size())
    {
        message << "Pinged all servers.";
        success = true;
    }
    else
    {
        message << "Successfully pinged " << n
                << " servers out of " << servers().size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_remove_node(json_t** ppOutput,
                               mxb::Semaphore* pSem, CsMonitorServer* pRemove_server,
                               const std::chrono::seconds& timeout,
                               bool force)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;
    json_t* pServers = nullptr;

    ServerVector sv = servers();

    string trx_id = next_trx_id("remove-node");

    http::Results results;
    if (CsMonitorServer::begin(sv, timeout, trx_id, m_http_config, &results))
    {
        CsMonitorServer::Statuses statuses;
        if (CsMonitorServer::fetch_statuses(sv, m_http_config, &statuses))
        {
            CsMonitorServer::Configs configs;
            if (CsMonitorServer::fetch_configs(sv, m_http_config, &configs))
            {
                auto it = std::find(sv.begin(), sv.end(), pRemove_server);
                mxb_assert(it != sv.end());
                auto offset = it - sv.begin();

                // Store status and config of server to be removed.
                auto remove_status = std::move(*(statuses.begin() + offset));
                auto remove_config = std::move(*(configs.begin() + offset));

                // Remove corresponding entry from all vectors.
                sv.erase(sv.begin() + offset);
                statuses.erase(statuses.begin() + offset);
                configs.erase(configs.begin() + offset);

                // Configs should be the same, but nonetheless the one whose uptime is
                // the longest should be chosen.
                auto jt = std::max_element(statuses.begin(), statuses.end(), [](const auto&l, const auto& r)
                                           {
                                               return l.uptime < r.uptime;
                                           });

                offset = jt - statuses.begin();

                auto& config = *(configs.begin() + offset);

                string ddlproc_ip;
                string dmlproc_ip;
                if (config.get_ddlproc_ip(&ddlproc_ip, pOutput)
                    && config.get_dmlproc_ip(&dmlproc_ip, pOutput))
                {
                    bool is_critical =
                        pRemove_server->address() == ddlproc_ip
                        || pRemove_server->address() == dmlproc_ip;

                    string body = create_remove_config(config, pRemove_server, force, is_critical);

                    http::Results results;
                    if (CsMonitorServer::set_config(sv, body, m_http_config, &results))
                    {
                        success = true;
                    }
                    else
                    {
                        LOG_APPEND_JSON_ERROR(&pOutput, "Could not send new config to all servers.");
                        results_to_json(sv, results, &pServers);
                    }
                }
                else
                {
                    LOG_PREPEND_JSON_ERROR(&pOutput, "Could not find current DDLProc/DMLProc.");
                }
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput, "Could not fetch configs from nodes.");
                results_to_json(sv, configs, &pServers);
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not fetch statuses from nodes.");
            results_to_json(sv, statuses, &pServers);
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on all nodes.");
        results_to_json(sv, results, &pServers);
    }

    if (success)
    {
        success = CsMonitorServer::commit(sv, m_http_config, &results);

        if (success)
        {
            std::chrono::seconds shutdown_timeout(0);
            if (!CsMonitorServer::shutdown({pRemove_server}, shutdown_timeout, m_http_config, &results))
            {
                MXS_ERROR("Could not shutdown '%s'.", pRemove_server->name());
            }

            pRemove_server->set_status(SERVER_MAINT);
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes, will attempt rollback.");
            results_to_json(sv, results, &pServers);
        }
    }

    if (!success)
    {
        if (!CsMonitorServer::rollback(sv, m_http_config, &results))
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not rollback changes, cluster state unknown.");
            if (pServers)
            {
                json_decref(pServers);
            }
            results_to_json(sv, results, &pServers);
        }
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    if (pServers)
    {
        json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);
    }

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_scan(json_t** ppOutput,
                        mxb::Semaphore* pSem,
                        CsMonitorServer* pServer,
                        const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;
    json_t* pServers = nullptr;

    const ServerVector& sv = servers();

    string trx_id = next_trx_id("scan");

    http::Results results;
    if (CsMonitorServer::begin(sv, timeout, trx_id, m_http_config, &results))
    {
        auto status = pServer->fetch_status();
        if (status.ok())
        {
            auto config = pServer->fetch_config();
            if (config.ok())
            {
                // TODO: Check roots from status.
                // TODO: Update roots in config accordingly.
                string body = config.response.body;

                http::Results results;
                if (CsMonitorServer::set_config(sv, body, m_http_config, &results))
                {
                    success = true;
                }
                else
                {
                    LOG_APPEND_JSON_ERROR(&pOutput, "Could not set the configuration to all nodes.");
                    results_to_json(sv, results, &pServers);
                }
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput, "Could not fetch the config from '%s'.",
                                      pServer->name());
                if (config.sJson.get())
                {
                    mxs_json_error_push_back(pOutput, config.sJson.get());
                }
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(ppOutput, "Could not fetch the status of '%s'.",
                                  pServer->name());
            if (status.sJson.get())
            {
                mxs_json_error_push_back(pOutput, status.sJson.get());
            }
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on all nodes.");
        results_to_json(sv, results, &pServers);
    }

    if (success)
    {
        success = CsMonitorServer::commit(sv, m_http_config, &results);

        if (!success)
        {
            LOG_APPEND_JSON_ERROR(ppOutput, "Could not commit changes, will attempt rollback.");
            results_to_json(sv, results, &pServers);
        }
    }

    if (!success)
    {
        if (!CsMonitorServer::rollback(sv, m_http_config, &results))
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not rollback changes, cluster state unknown.");
            if (pServers)
            {
                json_decref(pServers);
            }
            results_to_json(sv, results, &pServers);
        }
    }

    if (success)
    {
        message << "Scanned '" << pServer->name() << "' for dbroots and updated cluster.";
    }
    else
    {
        message << "Failed to scan '" << pServer->name() << "' for dbroots and/or to update cluster.";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    if (pServers)
    {
        json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);
    }

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_shutdown(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = true;
    ostringstream message;
    json_t* pServers = nullptr;

    const ServerVector& sv = servers();

    if (timeout != std::chrono::seconds(0))
    {
        // If there is a timeout, then the cluster must first be made read-only.
        success = CsMonitorServer::set_mode(sv, cs::READ_ONLY, m_http_config, &pOutput);

        if (!success)
        {
            message << "Could not make cluster readonly. Timed out shutdown is not possible.";
        }
    }

    if (success)
    {
        vector<http::Result> results = CsMonitorServer::shutdown(sv, timeout, m_http_config);

        size_t n = results_to_json(sv, results, &pServers);

        if (n == sv.size())
        {
            message << "Columnstore cluster shut down.";
        }
        else
        {
            message << n << " servers out of " << servers().size() << " shut down.";
            success = false;
        }
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));

    if (pServers)
    {
        json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);
    }

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_start(json_t** ppOutput, mxb::Semaphore* pSem)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    vector<http::Result> results = CsMonitorServer::start(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == sv.size())
    {
        success = CsMonitorServer::set_mode(sv, cs::READ_WRITE, m_http_config, &pOutput);

        if (success)
        {
            message << "All servers in cluster started successfully and cluster made readwrite.";
        }
        else
        {
            message << "All servers in cluster started successfully, but cluster could not be "
                    << "made readwrite.";
        }
    }
    else
    {
        message << n << " servers out of " << servers().size() << " started successfully.";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_status(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    CsMonitorServer::Statuses statuses = CsMonitorServer::fetch_statuses(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, statuses, &pServers);

    if (n == servers().size())
    {
        message << "Fetched the status from all servers.";
        success = true;
    }
    else
    {
        message << "Successfully fetched status from " << n
                << " servers out of " << sv.size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

#if defined(CSMON_EXPOSE_TRANSACTIONS)
void CsMonitor::cs_begin(json_t** ppOutput,
                         mxb::Semaphore* pSem,
                         const std::chrono::seconds& timeout,
                         CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    string trx_id = next_trx_id("begin");
    http::Results results = CsMonitorServer::begin(sv, timeout, trx_id, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == sv.size())
    {
        message << "Transaction started.";
        success = true;
    }
    else
    {
        message << "Transaction started on " << n << " servers, out of " << sv.size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_commit(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    http::Results results = CsMonitorServer::commit(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == sv.size())
    {
        message << "Transaction committed.";
        success = true;
    }
    else
    {
        message << "Transaction committed on " << n << " servers, out of " << sv.size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_rollback(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    http::Results results = CsMonitorServer::rollback(sv, m_http_config);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == sv.size())
    {
        message << "Transaction rolled back.";
        success = true;
    }
    else
    {
        message << "Transaction rolled back on " << n << " servers, out of " << sv.size() << ".";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}
#endif

bool CsMonitor::cs_add_first_multi_node(json_t* pOutput,
                                        CsMonitorServer* pServer,
                                        const std::chrono::seconds& timeout)
{
    bool success = false;

    mxb_assert(pServer->is_single_node());
    mxb_assert(servers().size() == 1);
    mxb_assert(servers().front() == pServer);

    string trx_id = next_trx_id("add-node");

    auto result = pServer->begin(timeout, trx_id);

    if (result.is_success())
    {
        const char* zTrx_id = trx_id.c_str();
        const char* zName = pServer->name();

        MXS_NOTICE("%s: Started transaction on '%s'.", zTrx_id, zName);
        auto config = pServer->fetch_config();

        if (config.ok())
        {
            MXS_NOTICE("%s: Fetched current config from '%s'.", zTrx_id, zName);

            // TODO: Add ClusterManager to config.
            int n = cs::update_if(*config.sXml, "//IPAddr", pServer->address(), "127.0.0.1");
            mxb_assert(n >= 0);

            xmlChar* pConfig = nullptr;
            int size = 0;

            xmlDocDumpMemory(config.sXml.get(), &pConfig, &size);

            json_t* pBody = json_object();
            json_object_set_new(pBody, cs::keys::CONFIG,
                                json_stringn(reinterpret_cast<const char*>(pConfig), size));
            xmlFree(pConfig);

            char* zBody = json_dumps(pBody, 0);
            json_decref(pBody);

            if (pServer->set_config(zBody, &pOutput))
            {
                MXS_NOTICE("%s: Updated config on '%s'.", zTrx_id, zName);

                result = pServer->commit();

                if (result.is_success())
                {
                    MXS_NOTICE("%s: Committed changes on '%s'.", zTrx_id, zName);
                    success = true;
                }
                else
                {
                    LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes to '%s': %s",
                                          pServer->name(),
                                          result.body.c_str());
                }
            }
            else
            {
                LOG_PREPEND_JSON_ERROR(&pOutput, "Could not set new config of '%s'.", zName);
            }

            MXS_FREE(zBody);
        }
        else
        {
            mxs_json_error_append(pOutput, "Could not fetch config of '%s'.", zName);
            if (config.sJson.get())
            {
                mxs_json_error_push_back(pOutput, config.sJson.get());
            }
        }

        if (!success)
        {
            result = pServer->rollback();

            if (!result.is_success())
            {
                MXS_ERROR("Could not perform a rollback on '%s': %s", zName, result.body.c_str());
            }
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on '%s'.", pServer->name());
    }

    return success;
}

bool CsMonitor::cs_add_additional_multi_node(json_t* pOutput,
                                             CsMonitorServer* pServer,
                                             const std::chrono::seconds& timeout)
{
    bool success = false;
    json_t* pServers = nullptr;

    const ServerVector& sv = servers();

    string trx_id = next_trx_id("add-node");

    http::Results results;
    if (CsMonitorServer::begin(sv, timeout, trx_id, m_http_config, &results))
    {
        auto status = pServer->fetch_status();

        if (status.ok())
        {
            ServerVector existing_servers;
            auto sb = sv.begin();
            auto se = sv.end();

            std::copy_if(sb, se, std::back_inserter(existing_servers), [pServer](auto* pS) {
                    return pServer != pS;
                });

            CsMonitorServer::Configs configs;
            if (CsMonitorServer::fetch_configs(existing_servers, m_http_config, &configs))
            {
                auto cb = configs.begin();
                auto ce = configs.end();

                auto it = std::max_element(cb, ce, [](const auto& l, const auto& r) {
                        return l.timestamp < r.timestamp;
                    });

                CsMonitorServer* pSource = *(sb + (it - cb));

                MXS_NOTICE("Using config of '%s' for configuring '%s'.",
                           pSource->name(), pServer->name());

                CsMonitorServer::Config& config = *it;

                string body = create_add_config(config, pServer);

                if (pServer->set_config(config.response.body, &pOutput))
                {
                    if (CsMonitorServer::set_config(sv, body, m_http_config, &results))
                    {
                        success = true;
                    }
                    else
                    {
                        LOG_APPEND_JSON_ERROR(&pOutput, "Could not update configs of existing nodes.");
                        results_to_json(sv, results, &pServers);
                    }
                }
                else
                {
                    LOG_PREPEND_JSON_ERROR(&pOutput, "Could not update config of new node.");
                }
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput, "Could not fetch configs from existing nodes.");
                results_to_json(sv, configs, &pServers);
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not fetch status from node to be added.");
            if (status.sJson.get())
            {
                mxs_json_error_push_back(pOutput, status.sJson.get());
            }
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on all nodes.");
        results_to_json(sv, results, &pServers);
    }

    if (success)
    {
        success = CsMonitorServer::commit(sv, m_http_config, &results);

        if (!success)
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes, will attempt rollback.");
            results_to_json(sv, results, &pServers);
        }
    }

    if (!success)
    {
        if (!CsMonitorServer::rollback(sv, m_http_config, &results))
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not rollback changes, cluster state unknown.");
            if (pServers)
            {
                json_decref(pServers);
            }
            results_to_json(sv, results, &pServers);
        }
    }

    if (pServers)
    {
        json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);
    }

    return success;
}

string CsMonitor::create_add_config(CsMonitorServer::Config& config, CsMonitorServer* pServer)
{
    // TODO: Add relevant information.
    return config.response.body;
}

string CsMonitor::create_remove_config(CsMonitorServer::Config& config,
                                       CsMonitorServer* pServer,
                                       bool force,
                                       bool is_critical)
{
    // TODO: Remove relevant information
    return config.response.body;
}

CsMonitorServer* CsMonitor::create_server(SERVER* pServer,
                                          const mxs::MonitorServer::SharedSettings& shared)
{
    return new CsMonitorServer(pServer, shared, &m_config, &m_http_config);
}
