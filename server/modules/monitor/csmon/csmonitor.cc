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

using Config = CsMonitorServer::Config;
using Result = CsMonitorServer::Result;
using Status = CsMonitorServer::Status;

using Configs = CsMonitorServer::Configs;
using Results = CsMonitorServer::Results;
using Statuses = CsMonitorServer::Statuses;

namespace
{

constexpr const char* ZALIVE_QUERY_10 = "SELECT mcsSystemReady() = 1 && mcsSystemReadOnly() <> 2";
constexpr const char* ZALIVE_QUERY_12 = ZALIVE_QUERY_10;
constexpr const char* ZALIVE_QUERY_15 = "SELECT 1";

constexpr const char* ZROLE_QUERY_12 = "SELECT mcsSystemPrimary()";

constexpr const char* get_alive_query(cs::Version version)
{
    switch (version)
    {
    case cs::CS_10:
        return ZALIVE_QUERY_10;

    case cs::CS_12:
        return ZALIVE_QUERY_12;

    case cs::CS_15:
        return ZALIVE_QUERY_15;

    case cs::CS_UNKNOWN:
    default:
        return nullptr;
    }
}

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
int get_full_version(MonitorServer* srv)
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

    return rval;
}

bool get_minor_version(const vector<CsMonitorServer*>& servers, cs::Version* pMinor_version)
{
    bool rv = true;

    cs::Version minor_version = cs::CS_UNKNOWN;

    if (!servers.empty())
    {
        CsMonitorServer* pCurrent = nullptr;
        for (auto* pServer : servers)
        {
            auto result = pServer->ping_or_connect();

            if (mxs::Monitor::connection_is_ok(result))
            {
                auto version_number = get_full_version(pServer);

                pServer->set_version_number(version_number);

                if (minor_version == cs::CS_UNKNOWN)
                {
                    minor_version = pServer->minor_version();
                    pCurrent = pServer;
                }
                else if (pServer->minor_version() != minor_version)
                {
                    MXS_ERROR("Minor version %s of '%s' is at least different than minor version %s "
                              "of '%s'.",
                              cs::to_string(pServer->minor_version()), pServer->name(),
                              cs::to_string(pCurrent->minor_version()), pCurrent->name());
                    rv = false;
                }
            }
            else
            {
                MXS_ERROR("Could not connect to '%s'.", pServer->name());
            }
        }
    }

    if (rv)
    {
        *pMinor_version = minor_version;
    }

    return rv;
}

vector<http::Response>::const_iterator find_first_failed(const vector<http::Response>& responses)
{
    return std::find_if(responses.begin(), responses.end(), [](const http::Response& response) -> bool {
            return response.code != 200;
        });
}

vector<http::Response>::iterator find_first_failed(vector<http::Response>& responses)
{
    return std::find_if(responses.begin(), responses.end(), [](const http::Response& response) -> bool {
            return response.code != 200;
        });
}

json_t* result_to_json(const CsMonitorServer& server, const CsMonitorServer::Result& result)
{
    json_t* pResult = nullptr;

    if (result.sJson)
    {
        pResult = result.sJson.get();
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

        if (result.ok())
        {
            ++n;
        }

        json_t* pResult = result_to_json(*pServer, result);

        json_t* pObject = json_object();
        json_object_set_new(pObject, "name", json_string(pServer->name()));
        json_object_set_new(pObject, "code", json_integer(result.response.code));
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

}


CsMonitor::CsMonitor(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
    , m_context(name)
{
}

CsMonitor::~CsMonitor()
{
}

// static
CsMonitor* CsMonitor::create(const std::string& name, const std::string& module)
{
    return new CsMonitor(name, module);
}

namespace
{

bool check_15_server_states(const char* zName,
                            const vector<CsMonitorServer*>& servers,
                            CsContext& context)
{
    bool rv = true;

    auto configs = CsMonitorServer::fetch_configs(servers, context);

    auto it = servers.begin();
    auto end = servers.end();
    auto jt = configs.begin();

    int nSingle_nodes = 0;

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
                    pServer->set_node_mode(CsMonitorServer::SINGLE_NODE);

                    if (servers.size() > 1)
                    {
                        MXS_WARNING("Server '%s' configured as a single node, even though multiple "
                                    "servers has been specified.", pServer->name());
                    }
                    ++nSingle_nodes;
                }
                else if (ip == pServer->address())
                {
                    pServer->set_node_mode(CsMonitorServer::MULTI_NODE);
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

    if (nSingle_nodes >= 1 && servers.size() > 1)
    {
        MXS_WARNING("Out of %d servers in total, %d are configured as single-nodes. "
                    "You are likely to see multiples servers marked as being master, "
                    "which is not likely to work as intended.",
                    (int)servers.size(), nSingle_nodes);
    }

    return rv;
}

}

bool CsMonitor::has_sufficient_permissions()
{
    bool rv = test_permissions(get_alive_query(m_context.config().version));

    if (rv)
    {
        cs::Version version;
        rv = get_minor_version(servers(), &version);

        if (rv)
        {
            if (version == m_context.config().version)
            {
                if (m_context.config().version == cs::CS_15)
                {
                    rv = check_15_server_states(name(), servers(), m_context);
                }
            }
            else if (version == cs::CS_UNKNOWN)
            {
                // If we cannot establish the actual version, we give the cluster the benefit of the
                // doubt and assume it is the one expected. In update_server_status() it will eventually
                // be revealed anyway.
            }
            else
            {
                MXS_ERROR("%s: The monitor is configured for Columnstore %s, but the cluster "
                          "is Columnstore %s. You need specify 'version=%s' in the configuration "
                          "file.",
                          name(),
                          cs::to_string(m_context.config().version),
                          cs::to_string(version),
                          cs::to_string(version));
                rv = false;
            }
        }
        else
        {
            MXS_ERROR("The minor version of the servers is not identical, monitoring is not possible.");
        }
    }

    return rv;
}

void CsMonitor::update_server_status(MonitorServer* pS)
{
    CsMonitorServer* pServer = static_cast<CsMonitorServer*>(pS);

    pServer->clear_pending_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);

    if (pServer->minor_version() == cs::CS_UNKNOWN)
    {
        MXS_WARNING("Version of '%s' is not known, trying to find out.", pServer->name());

        int version_number = get_full_version(pServer);

        if (version_number == -1)
        {
            MXS_ERROR("Could not find out version of '%s'.", pServer->name());
        }
        else
        {
            pServer->set_version_number(version_number);

            if (pServer->minor_version() != m_context.config().version)
            {
                MXS_ERROR("Version of '%s' is different from the cluster version; server will be ignored.",
                          pServer->name());
            }
        }
    }

    int status_mask = 0;

    if (pServer->minor_version() == m_context.config().version)
    {
        if (do_query(pServer, get_alive_query(m_context.config().version)) == "1")
        {
            if (m_context.config().version == cs::CS_15)
            {
                status_mask = get_15_server_status(pServer);
            }
            else
            {
                status_mask |= SERVER_RUNNING;

                switch (m_context.config().version)
                {
                case cs::CS_10:
                    status_mask |= get_10_server_status(pServer);
                    break;

                case cs::CS_12:
                    status_mask |= get_12_server_status(pServer);
                    break;

                case cs::CS_15:
                default:
                    mxb_assert(!true);
                }
            }
        }
    }

    pServer->set_pending_status(status_mask);
}

int CsMonitor::get_10_server_status(CsMonitorServer* pServer)
{
    return pServer->server == m_context.config().pPrimary ? SERVER_MASTER : SERVER_SLAVE;
}

int CsMonitor::get_12_server_status(CsMonitorServer* pServer)
{
    return do_query(pServer, ZROLE_QUERY_12) == "1" ? SERVER_MASTER : SERVER_SLAVE;
}

int CsMonitor::get_15_server_status(CsMonitorServer* pServer)
{
    int status_mask = 0;

    auto status = pServer->fetch_status();

    if (status.ok())
    {
        // If services are empty, it is an indication that Columnstore actually
        // is not running _even_ if we were able to connect to the MariaDB server.
        if (!status.services.empty())
        {
            status_mask |= SERVER_RUNNING;

            // Seems to be running.
            if (status.dbrm_mode == cs::MASTER)
            {
                // The node that claims to be master gets
                // - the master bit if the cluster is readwrite,
                // - the slave bit if it is the only server (i.e. probably a single node installation),
                // - otherwise only the running bit (the master node in a readonly multi-node cluster).
                if (status.cluster_mode == cs::READWRITE)
                {
                    status_mask |= SERVER_MASTER;
                }
                else if (servers().size() == 1)
                {
                    status_mask |= SERVER_SLAVE;
                }
            }
            else
            {
                status_mask |= SERVER_SLAVE;
            }
        }
    }
    else
    {
        MXS_ERROR("Could not fetch status using REST-API from '%s': (%d) %s",
                  pServer->name(), status.response.code, status.response.body.c_str());
    }

    return status_mask;
}

bool CsMonitor::configure(const mxs::ConfigParameters* pParams)
{
    bool rv = m_context.configure(*pParams);

    if (rv)
    {
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

bool CsMonitor::command_config_set(json_t** ppOutput,
                                   const char* zJson,
                                   const std::chrono::seconds& timeout,
                                   CsMonitorServer* pServer)
{
    bool rv = false;

    auto len = strlen(zJson);
    if (is_valid_json(ppOutput, zJson, len))
    {
        mxb::Semaphore sem;
        string body(zJson, zJson + len);

        auto cmd = [this, ppOutput, &sem, &body, timeout, pServer] () {
            if (ready_to_run(ppOutput))
            {
                cs_config_set(ppOutput, &sem, std::move(body), timeout, pServer);
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

bool CsMonitor::command_mode_set(json_t** ppOutput, const char* zMode, const std::chrono::seconds& timeout)
{
    bool rv = false;
    cs::ClusterMode mode;

    if (cs::from_string(zMode, &mode))
    {
        mxb::Semaphore sem;

        auto cmd = [this, ppOutput, &sem, mode, timeout] () {
            if (ready_to_run(ppOutput))
            {
                cs_mode_set(ppOutput, &sem, mode, timeout);
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

bool CsMonitor::command_start(json_t** ppOutput, const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, timeout, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_start(ppOutput, &sem, timeout);
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

bool CsMonitor::command_commit(json_t** ppOutput,
                               const std::chrono::seconds& timeout,
                               CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, timeout, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cs_commit(ppOutput, &sem, timeout, pServer);
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

    if (pServer->is_unknown_mode())
    {
        auto config = pServer->fetch_config();
        // TODO: Propagate any errors to caller.
        success = config.ok() && pServer->set_node_mode(config, pOutput);

        if (!success)
        {
            json_t* pError = mxs_json_error("Can't establish whether server '%s' has been configured "
                                            "already. It cannot be added to the cluster.",
                                            pServer->name());
            mxs_json_error_push_front(pOutput, pError);
        }
    }

    if (pServer->is_multi_node())
    {
        mxs_json_error_append(pOutput,
                              "The server '%s' is already a node in a cluster.",
                              pServer->name());
    }
    else if (pServer->is_single_node())
    {
        const auto& sv = servers();

        auto it = std::find_if(sv.begin(), sv.end(), std::mem_fun(&CsMonitorServer::is_multi_node));

        if (it == sv.end())
        {
            success = cs_add_first_multi_node(pOutput, pServer, timeout);
        }
        else
        {
            success = cs_add_additional_multi_node(pOutput, pServer, timeout);
        }
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

    CsMonitorServer::Configs configs = CsMonitorServer::fetch_configs(sv, m_context);

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

void CsMonitor::cs_config_set(json_t** ppOutput,
                              mxb::Semaphore* pSem,
                              string&& body,
                              const std::chrono::seconds& timeout,
                              CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;
    json_t* pServers = nullptr;

    ServerVector sv;

    if (pServer)
    {
        sv.push_back(pServer);
    }
    else
    {
        sv = servers();
    }

    Results results;
    if (CsMonitorServer::begin(sv, timeout, m_context, &results))
    {
        if (CsMonitorServer::set_config(sv, body, m_context, &results))
        {
            if (CsMonitorServer::commit(sv, timeout, m_context, &results))
            {
                message << "Config set on all servers.";
                results_to_json(sv, results, &pServers);
                success = true;
            }
            else
            {
                LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes, will attempt rollback.");
                results_to_json(sv, results, &pServers);
            }
        }
        else
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not set config on all nodes.");
            results_to_json(sv, results, &pServers);
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on all nodes.");
        results_to_json(sv, results, &pServers);
    }

    if (!success)
    {
        if (!CsMonitorServer::rollback(sv, m_context, &results))
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
        message << "Config applied to all servers.";
    }
    else
    {
        message << "Could not set config to all servers.";
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set_new(pOutput, csmon::keys::SERVERS, pServers);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_mode_set(json_t** ppOutput, mxb::Semaphore* pSem, cs::ClusterMode mode,
                            const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    success = CsMonitorServer::set_cluster_mode(sv, mode, timeout, m_context, pOutput);

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

    Results results;
    if (CsMonitorServer::begin(sv, timeout, m_context, &results))
    {
        CsMonitorServer::Statuses statuses;
        if (CsMonitorServer::fetch_statuses(sv, m_context, &statuses))
        {
            CsMonitorServer::Configs configs;
            if (CsMonitorServer::fetch_configs(sv, m_context, &configs))
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

                if (sv.size() != 0)
                {
                    // Configs should be the same, but nonetheless the one whose uptime is
                    // the longest should be chosen.
                    auto jt = std::max_element(statuses.begin(), statuses.end(),
                                               [](const auto&l, const auto& r)
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

                        Results results;
                        if (CsMonitorServer::set_config(sv, body, m_context, &results))
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
                    // If we are in the process of removing the last server, then at this point
                    // we are all set.
                    success = true;
                }

                if (success)
                {
                    cs::xml::convert_to_single_node(*remove_config.sXml);

                    auto body = cs::body::config(*remove_config.sXml,
                                                 m_context.revision(),
                                                 m_context.manager(),
                                                 timeout);

                    if (pRemove_server->set_config(body, &pOutput))
                    {
                        MXS_NOTICE("Updated config on '%s'.", pRemove_server->name());
                    }
                    else
                    {
                        success = false;
                    }
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

    sv = servers();

    if (success)
    {
        success = CsMonitorServer::commit(sv, timeout, m_context, &results);

        if (success)
        {
            std::chrono::seconds shutdown_timeout(0);
            if (!CsMonitorServer::shutdown({pRemove_server}, shutdown_timeout, m_context, &results))
            {
                MXS_ERROR("Could not shutdown '%s'.", pRemove_server->name());
            }

            pRemove_server->set_node_mode(CsMonitorServer::SINGLE_NODE);
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
        if (!CsMonitorServer::rollback(sv, m_context, &results))
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
        message << "Server '" << pRemove_server->name() << "' removed from the cluster.";
    }
    else
    {
        message << "The removing of server '" << pRemove_server->name() << "' from the cluster failed.";
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
    cs::xml::DbRoots::Status dbroots_status = cs::xml::DbRoots::ERROR;
    ostringstream message;
    json_t* pServers = nullptr;

    const ServerVector& sv = servers();

    Results results;
    if (CsMonitorServer::begin(sv, timeout, m_context, &results))
    {
        auto status = pServer->fetch_status();
        if (status.ok())
        {
            auto config = pServer->fetch_config();
            if (config.ok())
            {
                dbroots_status = cs::xml::update_dbroots(*config.sXml.get(),
                                                         pServer->address(),
                                                         status.dbroots,
                                                         pOutput);

                if (dbroots_status == cs::xml::DbRoots::UPDATED)
                {
                    string body = cs::body::config(*config.sXml.get(),
                                                   m_context.revision(),
                                                   m_context.manager(),
                                                   timeout);

                    if (!CsMonitorServer::set_config(sv, body, m_context, &results))
                    {
                        LOG_APPEND_JSON_ERROR(&pOutput, "Could not set the configuration to all nodes.");
                        results_to_json(sv, results, &pServers);
                        dbroots_status = cs::xml::DbRoots::ERROR;
                    }
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

    if (dbroots_status == cs::xml::DbRoots::UPDATED)
    {
        if (!CsMonitorServer::commit(sv, timeout, m_context, &results))
        {
            LOG_APPEND_JSON_ERROR(ppOutput, "Could not commit changes, will attempt rollback.");
            results_to_json(sv, results, &pServers);
            dbroots_status = cs::xml::DbRoots::ERROR;
        }
    }

    if (dbroots_status != cs::xml::DbRoots::UPDATED)
    {
        if (!CsMonitorServer::rollback(sv, m_context, &results))
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not rollback changes, cluster state unknown.");
            if (pServers)
            {
                json_decref(pServers);
            }
            results_to_json(sv, results, &pServers);
        }
    }

    bool success = false;

    switch (dbroots_status)
    {
    case cs::xml::DbRoots::NO_CHANGE:
        success = true;
        message << "No change in DB roots of '" << pServer->name() << "', nothing needs to be done.";
        break;

    case cs::xml::DbRoots::ERROR:
        message << "Failed to scan '" << pServer->name() << "' for dbroots and/or to update cluster.";
        break;

    case cs::xml::DbRoots::UPDATED:
        success = true;
        message << "Scanned '" << pServer->name() << "' for dbroots and updated cluster.";
        break;
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
        success = CsMonitorServer::set_cluster_mode(sv, cs::READONLY, timeout, m_context, pOutput);

        if (!success)
        {
            message << "Could not make cluster readonly. Timed out shutdown is not possible.";
        }
    }

    if (success)
    {
        Results results = CsMonitorServer::shutdown(sv, timeout, m_context);

        size_t n = results_to_json(sv, results, &pServers);

        if (n == sv.size())
        {
            message << "Cluster shut down.";
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

void CsMonitor::cs_start(json_t** ppOutput, mxb::Semaphore* pSem, const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    Results results = CsMonitorServer::start(sv, m_context);

    json_t* pServers = nullptr;
    size_t n = results_to_json(sv, results, &pServers);

    if (n == sv.size())
    {
        message << "Cluster started successfully, ";

        if (CsMonitorServer::set_cluster_mode(sv, cs::READWRITE, timeout, m_context, pOutput))
        {
            success = true;
            message << "and made readwrite.";
        }
        else
        {
            message << "but could not be made readwrite.";
        }
    }
    else
    {
        message << n << " servers out of " << servers().size() << " started successfully, "
                << "cluster left in a readonly state.";
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

    Statuses statuses = CsMonitorServer::fetch_statuses(sv, m_context);

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

    Results results = CsMonitorServer::begin(sv, timeout, m_context);

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

void CsMonitor::cs_commit(json_t** ppOutput,
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

    Results results = CsMonitorServer::commit(sv, timeout, m_context);

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

    Results results = CsMonitorServer::rollback(sv, m_context);

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

    auto result = pServer->begin(timeout);

    if (result.ok())
    {
        const char* zName = pServer->name();

        CS_DEBUG("Started transaction on '%s'.", zName);
        auto config = pServer->fetch_config();

        if (config.ok())
        {
            CS_DEBUG("Fetched current config from '%s'.", zName);

            if (cs::xml::convert_to_first_multi_node(*config.sXml,
                                                     m_context.manager(),
                                                     pServer->address(),
                                                     pOutput))
            {
                auto body = cs::body::config(*config.sXml,
                                             m_context.revision(),
                                             m_context.manager(),
                                             timeout);

                if (pServer->set_config(body, &pOutput))
                {
                    MXS_NOTICE("Updated config on '%s'.", zName);

                    result = pServer->commit(timeout);

                    if (result.ok())
                    {
                        MXS_NOTICE("Committed changes on '%s'.", zName);
                        success = true;
                    }
                    else
                    {
                        LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes to '%s': %s",
                                              pServer->name(),
                                              result.response.body.c_str());
                    }
                }
                else
                {
                    LOG_PREPEND_JSON_ERROR(&pOutput, "Could not set new config of '%s'.", zName);
                }
            }
            else
            {
                LOG_PREPEND_JSON_ERROR(&pOutput, "Could not convert single node configuration to "
                                       "first multi-node configuration.");
            }
        }
        else
        {
            mxs_json_error_append(pOutput, "Could not fetch config of '%s'.", zName);
            if (config.sJson.get())
            {
                mxs_json_error_push_back(pOutput, config.sJson.get());
            }
        }

        if (success)
        {
            pServer->set_node_mode(CsMonitorServer::MULTI_NODE);
        }
        else
        {
            result = pServer->rollback();

            if (!result.ok())
            {
                MXS_ERROR("Could not perform a rollback on '%s': %s", zName, result.response.body.c_str());
            }
        }
    }
    else
    {
        LOG_APPEND_JSON_ERROR(&pOutput, "Could not start a transaction on '%s': %s",
                              pServer->name(), result.response.body.c_str());
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

    Results results;
    if (CsMonitorServer::begin(sv, timeout, m_context, &results))
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
            if (CsMonitorServer::fetch_configs(existing_servers, m_context, &configs))
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
                    if (CsMonitorServer::set_config(sv, body, m_context, &results))
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
        success = CsMonitorServer::commit(sv, timeout, m_context, &results);

        if (!success)
        {
            LOG_APPEND_JSON_ERROR(&pOutput, "Could not commit changes, will attempt rollback.");
            results_to_json(sv, results, &pServers);
        }
    }

    if (!success)
    {
        if (!CsMonitorServer::rollback(sv, m_context, &results))
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
    return new CsMonitorServer(pServer, shared, &m_context);
}
