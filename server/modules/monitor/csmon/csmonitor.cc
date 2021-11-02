/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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
                else
                {
                    // If the IP is anything but 127.0.0.1, we assume it is a node
                    // setup for a multi-node cluster.
                    //
                    // TODO: Check that every server is configured for use in
                    // TODO: the _same_ cluster.
                    pServer->set_node_mode(CsMonitorServer::MULTI_NODE);
                }
            }
            else
            {
                MXS_WARNING("Could not get DMRM_Controller IP of '%s'.", pServer->name());
            }
        }
        else
        {
            MXS_ERROR("Could not fetch config from '%s': (%d) %s",
                      pServer->name(), config.response.code, config.response.body.c_str());
        }

        ++it;
        ++jt;
    }

    if (nSingle_nodes >= 1 && servers.size() > 1)
    {
        MXS_WARNING("Out of %d servers in total, %d are configured as single-nodes. "
                    "You are likely to see multiple servers marked as being master, "
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
                                 const string& host,
                                 const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, host, timeout, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cs_add_node(ppOutput, &sem, host, timeout);
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
                                    const string& host,
                                    const std::chrono::seconds& timeout)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, host, timeout] () {
        if (ready_to_run(ppOutput))
        {
            cs_remove_node(ppOutput, &sem, host, timeout);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "remove-node", cmd);
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

void CsMonitor::cs_add_node(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            const string& host,
                            const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    Result result = CsMonitorServer::add_node(servers(), host, timeout, m_context);
    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Node " << host << " successfully added to cluster.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Could not add node " << host << " to the cluster.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_config_get(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    Result result;

    if (pServer)
    {
        result = pServer->fetch_config();
    }
    else
    {
        result = CsMonitorServer::fetch_config(servers(), m_context);
    }

    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Config successfully fetched.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Could not fetch status.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

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
                               mxb::Semaphore* pSem, const string& host,
                               const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    Result result = CsMonitorServer::remove_node(servers(), host, timeout, m_context);
    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Node " << host << " removed from the cluster.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Could not remove node " << host << " from the cluster.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_shutdown(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    Result result = CsMonitorServer::shutdown(sv, timeout, m_context);
    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Cluster shut down.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Could not shut down cluster.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_start(json_t** ppOutput, mxb::Semaphore* pSem, const std::chrono::seconds& timeout)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    const ServerVector& sv = servers();

    Result result = CsMonitorServer::start(sv, timeout, m_context);
    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Cluster started successfully.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Cluster did not start successfully.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

    *ppOutput = pOutput;

    pSem->post();
}

void CsMonitor::cs_status(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    json_t* pOutput = json_object();
    bool success = false;
    ostringstream message;

    Result result;

    if (pServer)
    {
        result = pServer->fetch_status();
    }
    else
    {
        result = CsMonitorServer::fetch_status(servers(), m_context);
    }

    json_t* pResult = nullptr;

    if (result.ok())
    {
        message << "Status successfully fetched.";
        pResult = result.sJson.get();
        json_incref(pResult);
        success = true;
    }
    else
    {
        message << "Could not fetch status.";
        pResult = mxs_json_error("%s", result.response.body.c_str());
    }

    json_object_set_new(pOutput, csmon::keys::SUCCESS, json_boolean(success));
    json_object_set_new(pOutput, csmon::keys::MESSAGE, json_string(message.str().c_str()));
    json_object_set(pOutput, csmon::keys::RESULT, pResult);

    json_decref(pResult);

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

CsMonitorServer* CsMonitor::create_server(SERVER* pServer,
                                          const mxs::MonitorServer::SharedSettings& shared)
{
    return new CsMonitorServer(pServer, shared, &m_context);
}
