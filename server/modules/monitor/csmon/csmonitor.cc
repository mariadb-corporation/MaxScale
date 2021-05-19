/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "csmonitor.hh"

#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <mysql.h>

#include <maxscale/modinfo.hh>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.hh>
#include "../../../core/internal/config_runtime.hh"
#include "../../../core/internal/service.hh"
#include "columnstore.hh"

using namespace std;
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

constexpr const char* ZALIVE_QUERY_15 = "SELECT 1";

constexpr const char* get_alive_query(cs::Version version)
{
    switch (version)
    {
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

void run_in_mainworker(const function<void(void)>& func)
{
    auto* pMw = mxs::MainWorker::get();
    mxb_assert(pMw);

    pMw->execute(func, mxb::Worker::EXECUTE_QUEUED);
}

int get_status_mask(const cs::Status& status, size_t nServers)
{
    int mask = 0;

    if (status.ok())
    {
        // If services are empty, it is an indication that Columnstore actually
        // is not running _even_ if we were able to connect to the MariaDB server.
        if (!status.services.empty())
        {
            switch (status.dbrm_mode)
            {
            case cs::OFFLINE:
                break;

            case cs::MASTER:
                mask |= SERVER_RUNNING;

                // The node that claims to be master gets
                // - the master bit if the cluster is readwrite,
                // - the slave bit if it is the _only_ server (i.e. probably a single node installation),
                // - otherwise only the running bit (the master node in a readonly multi-node cluster).
                if (status.cluster_mode == cs::READWRITE)
                {
                    mask |= SERVER_MASTER;
                }
                else if (nServers == 1)
                {
                    mask |= SERVER_SLAVE;
                }
                break;

            case cs::SLAVE:
                mask |= (SERVER_RUNNING | SERVER_SLAVE);
            }
        }
    }

    return mask;
}

int fetch_status_mask(const CsMonitorServer& server, size_t nServers)
{
    int mask = 0;

    auto status = server.fetch_node_status();

    if (status.ok())
    {
        mask = get_status_mask(status, nServers);
    }
    else
    {
        MXS_ERROR("Could not fetch status using REST-API from '%s': (%d) %s",
                  server.name(), status.response.code, status.response.body.c_str());
    }

    return mask;
}

}

namespace
{

// Change this, if the schema is changed.
const int SCHEMA_VERSION = 1;

static const char SQL_BN_CREATE[] =
    "CREATE TABLE IF NOT EXISTS bootstrap_nodes "
    "(ip TEXT, mysql_port INT)";

static const char SQL_BN_INSERT_FORMAT[] =
    "INSERT INTO bootstrap_nodes (ip, mysql_port) "
    "VALUES %s";

static const char SQL_BN_DELETE[] =
    "DELETE FROM bootstrap_nodes";

static const char SQL_BN_SELECT[] =
    "SELECT ip, mysql_port FROM bootstrap_nodes";


static const char SQL_DN_CREATE[] =
    "CREATE TABLE IF NOT EXISTS dynamic_nodes "
    "(ip TEXT PRIMARY KEY, mysql_port INT)";

static const char SQL_DN_UPSERT_FORMAT[] =
    "INSERT OR REPLACE INTO dynamic_nodes (ip, mysql_port) "
    "VALUES ('%s', %d)";

static const char SQL_DN_DELETE_FORMAT[] =
    "DELETE FROM dynamic_nodes WHERE ip = '%s'";

static const char SQL_DN_DELETE[] =
    "DELETE FROM dynamic_nodes";

static const char SQL_DN_SELECT[] =
    "SELECT ip, mysql_port FROM dynamic_nodes";


using HostPortPair = std::pair<std::string, int>;
using HostPortPairs = std::vector<HostPortPair>;

// sqlite3 callback.
int select_cb(void* pData, int nColumns, char** ppColumn, char** ppNames)
{
    std::vector<HostPortPair>* pNodes = static_cast<std::vector<HostPortPair>*>(pData);

    mxb_assert(nColumns == 2);

    std::string host(ppColumn[0]);
    int port = atoi(ppColumn[1]);

    pNodes->emplace_back(host, port);

    return 0;
}

bool create_schema(sqlite3* pDb)
{
    char* pError = nullptr;
    int rv = sqlite3_exec(pDb, SQL_BN_CREATE, nullptr, nullptr, &pError);

    if (rv == SQLITE_OK)
    {
        rv = sqlite3_exec(pDb, SQL_DN_CREATE, nullptr, nullptr, &pError);
    }


    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not initialize sqlite3 database: %s", pError ? pError : "Unknown error");
    }

    return rv == SQLITE_OK;
}

sqlite3* open_or_create_db(const std::string& path)
{
    sqlite3* pDb = nullptr;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_CREATE;
    int rv = sqlite3_open_v2(path.c_str(), &pDb, flags, nullptr);

    if (rv == SQLITE_OK)
    {
        if (create_schema(pDb))
        {
            MXS_NOTICE("sqlite3 database %s open/created and initialized.", path.c_str());
        }
        else
        {
            MXS_ERROR("Could not create schema in sqlite3 database %s.", path.c_str());

            if (unlink(path.c_str()) != 0)
            {
                MXS_ERROR("Failed to delete database %s that could not be properly "
                          "initialized. It should be deleted manually.", path.c_str());
                sqlite3_close_v2(pDb);
                pDb = nullptr;
            }
        }
    }
    else
    {
        if (pDb)
        {
            // Memory allocation failure is explained by the caller. Don't close the handle, as the
            // caller will still use it even if open failed!!
            MXS_ERROR("Opening/creating the sqlite3 database %s failed: %s",
                      path.c_str(), sqlite3_errmsg(pDb));
        }
        MXS_ERROR("Could not open sqlite3 database for storing information "
                  "about dynamically detected Columnstore nodes. The Columnstore "
                  "monitor will remain dependent upon statically defined "
                  "bootstrap nodes.");
    }

    return pDb;
}

}

CsMonitor::CsMonitor(const std::string& name, const std::string& module, sqlite3* pDb)
    : MonitorWorkerSimple(name, module)
    , m_context(name)
    , m_pDb(pDb)
{
}

CsMonitor::~CsMonitor()
{
    sqlite3_close_v2(m_pDb);
}

// static
CsMonitor* CsMonitor::create(const std::string& name, const std::string& module)
{
    string path = mxs::datadir();

    path += "/";
    path += name;

    if (!mxs_mkdir_all(path.c_str(), 0744))
    {
        MXS_ERROR("Could not create the directory %s, MaxScale will not be "
                  "able to create database for persisting connection "
                  "information of dynamically detected Columnstore nodes.",
                  path.c_str());
    }

    path += "/columnstore_nodes-v";
    path += std::to_string(SCHEMA_VERSION);
    path += ".db";

    sqlite3* pDb = open_or_create_db(path);

    CsMonitor* pThis = nullptr;

    if (pDb)
    {
        // Even if the creation/opening of the sqlite3 database fails, we will still
        // get a valid database handle.
        pThis = new CsMonitor(name, module, pDb);
    }
    else
    {
        // The handle will be null, *only* if the opening fails due to a memory
        // allocation error.
        MXS_ALERT("sqlite3 memory allocation failed, the Columnstore monitor "
                  "cannot continue.");
    }

    return pThis;
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
            mxb_assert(m_context.config().version == cs::CS_15);
            status_mask = fetch_status_mask(*pServer);
        }
    }

    pServer->set_pending_status(status_mask);
}

int CsMonitor::fetch_status_mask(const CsMonitorServer &server)
{
    return ::fetch_status_mask(server, servers().size());
}

void CsMonitor::update_status_of_dynamic_servers()
{
    vector<CsMonitorServer*> servers;
    std::transform(m_nodes_by_id.begin(), m_nodes_by_id.end(), std::back_inserter(servers),
                   [](const auto& kv) {
                       return kv.second.get();
                   });

    Statuses statuses;
    CsMonitorServer::fetch_statuses(servers, m_context, &statuses);

    auto it = m_nodes_by_id.begin();
    for (const auto& status : statuses)
    {
        auto* pServer = it->second.get();

        if (!status.ok())
        {
            const auto& name = it->first;

            MXS_WARNING("Could not fetch status from %s: %s", name.c_str(), status.response.body.c_str());
            pServer->clear_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
        }
        else
        {
            pServer->set_status(get_status_mask(status, m_nodes_by_id.size()));
        }

        ++it;
    }
}

bool CsMonitor::configure(const mxs::ConfigParameters* pParams)
{
    bool rv = m_context.configure(*pParams);

    if (rv)
    {
        rv = MonitorWorkerSimple::configure(pParams);

        if (rv && m_context.config().dynamic_node_detection)
        {
            // Only if dynamic node detection is enabled do we need to check the
            // bootstrap servers. If disabled we are going to use them directly
            // as the servers to be monitored.
            rv = check_bootstrap_servers();
        }
    }

    if (rv)
    {
        if (m_context.config().dynamic_node_detection)
        {
            m_obsolete_bootstraps.clear();
            m_probe_cluster = true;
            m_last_probe = mxb::SteadyClock::now() - m_context.config().cluster_monitor_interval;

            probe_cluster();
        }
        else
        {
            populate_from_bootstrap_servers();
        }
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

CsMonitorServer* CsMonitor::get_monitored_server(SERVER* pServer)
{
    CsMonitorServer* pMs = get_dynamic_server(pServer);

    if (!pMs)
    {
        pMs = static_cast<CsMonitorServer*>(Base::get_monitored_server(pServer));
    }

    return pMs;
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

void CsMonitor::persist(const CsDynamicServer& node)
{
    if (!m_pDb)
    {
        return;
    }

    string id = node.address();

    char sql_upsert[sizeof(SQL_DN_UPSERT_FORMAT) + id.length() + 10];

    sprintf(sql_upsert, SQL_DN_UPSERT_FORMAT, id.c_str(), 3306);

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_upsert, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_NOTICE("Updated Columnstore node in bookkeeping: '%s'", id.c_str());
    }
    else
    {
        MXS_ERROR("Could not update Columnstore node ('%s') in bookkeeping: %s",
                  id.c_str(), pError ? pError : "Unknown error");
    }

}

void CsMonitor::unpersist(const CsDynamicServer& node)
{
    remove_dynamic_host(node.address());
}

void CsMonitor::remove_dynamic_host(const std::string& host)
{
    if (!m_pDb)
    {
        return;
    }

    char sql_delete[sizeof(SQL_DN_DELETE_FORMAT) + host.length()];

    sprintf(sql_delete, SQL_DN_DELETE_FORMAT, host.c_str());

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_delete, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_INFO("Deleted Columnstore node %s from bookkeeping.", host.c_str());
    }
    else
    {
        MXS_ERROR("Could not delete Columnstore node %s from bookkeeping: %s",
                  host.c_str(), pError ? pError : "Unknown error");
    }
}

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

    // Better safe than sorry, unconditionally probe after attempted cluster change.
    m_probe_cluster = true;
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

    // Better safe than sorry, unconditionally probe after attempted cluster change.
    m_probe_cluster = true;
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
        result = pServer->fetch_node_status();
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
    return new CsBootstrapServer(pServer, shared, &m_context);
}

void CsMonitor::populate_services()
{
    mxb_assert(!is_running());

    // The servers that the Columnstor monitor has been configured with are
    // only used for bootstrapping and services will not be populated
    // with them.
}

void CsMonitor::server_added(SERVER* pServer)
{
    // The servers explicitly added to the Columnstore monitor are only used
    // as bootstrap servers, so they are not added to any services.
}

void CsMonitor::server_removed(SERVER* pServer)
{
    // @see server_added(), no action is needed.
}

void CsMonitor::pre_loop()
{
    MonitorWorkerSimple::pre_loop();
}

void CsMonitor::pre_tick()
{
    if (m_context.config().dynamic_node_detection)
    {
        if (m_nodes_by_id.empty())
        {
            probe_cluster();
        }
        else
        {
            if (should_probe_cluster())
            {
                HostPortPairs nodes;
                for (const auto& kv : m_nodes_by_id)
                {
                    auto* pMs = kv.second.get();
                    nodes.push_back(HostPortPair(pMs->address(), pMs->port()));
                }

                probe_cluster(nodes);
            }

            update_status_of_dynamic_servers();
        }
    }
}

void CsMonitor::probe_cluster()
{
    HostPortPairs nodes;
    char* pError = nullptr;

    if (sqlite3_exec(m_pDb, SQL_DN_SELECT, select_cb, &nodes, &pError) != SQLITE_OK)
    {
        MXS_WARNING("Could not lookup earlier nodes: %s", pError ? pError : "Unknown error");
        nodes.clear();
    }

    if (nodes.empty())
    {
        MXS_NOTICE("Checking cluster using bootstrap nodes.");

        for (auto* pMs : servers())
        {
            nodes.push_back(HostPortPair(pMs->address(), pMs->port()));
        }
    }

    probe_cluster(nodes);
}

namespace
{

void set_status(CsDynamicServer& mserver, int status_mask)
{
    auto result = mserver.ping_or_connect();

    switch (result)
    {
    case mxs::MonitorServer::ConnectResult::OLDCONN_OK:
    case mxs::MonitorServer::ConnectResult::NEWCONN_OK:
        mserver.set_status(status_mask);
        break;

    case mxs::MonitorServer::ConnectResult::REFUSED:
    case mxs::MonitorServer::ConnectResult::TIMEOUT:
        mserver.clear_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
        break;

    case mxs::MonitorServer::ConnectResult::ACCESS_DENIED:
        mserver.set_status(SERVER_AUTH_ERROR);
    }
}

}

void CsMonitor::adjust_dynamic_servers(const Hosts& hosts)
{
    set<string> current_hosts;
    for (const auto& kv : m_nodes_by_id)
    {
        current_hosts.insert(kv.first);
    }

    if (!hosts.empty())
    {
        for (const auto& host : hosts)
        {
            auto it = m_nodes_by_id.find(host);

            if (it == m_nodes_by_id.end())
            {
                string server_name = create_dynamic_name(host);

                SERVER* pServer = SERVER::find_by_unique_name(server_name);

                if (!pServer)
                {
                    if (runtime_create_volatile_server(server_name, host, 3306))
                    {
                        pServer = SERVER::find_by_unique_name(server_name);

                        // New server, so it needs to be added to all services that
                        // use this monitor for defining its cluster of servers.
                        run_in_mainworker([this, pServer]() {
                                service_add_server(this, pServer);
                            });
                    }
                    else
                    {
                        MXS_ERROR("%s: Could not create server %s at %s:%d.",
                                  name(), server_name.c_str(), host.c_str(), mysql_port);
                    }
                }

                mxb_assert(pServer);

                if (pServer)
                {
                    CsDynamicServer* pMs = new CsDynamicServer(this, pServer,
                                                               this->settings().shared, &m_context);
                    unique_ptr<CsDynamicServer> sMs(pMs);

                    m_nodes_by_id.insert(make_pair(host, std::move(sMs)));
                }
            }
            else
            {
                current_hosts.erase(host);
            }
        }
    }

    if (!current_hosts.empty())
    {
        MXS_NOTICE("Node(s) %s no longer in cluster.", mxb::join(current_hosts).c_str());

        for (const auto& host : current_hosts)
        {
            auto it = m_nodes_by_id.find(host);
            mxb_assert(it != m_nodes_by_id.end());

            auto sMs = std::move(it->second);
            m_nodes_by_id.erase(it);

            sMs->set_excluded();
        }
    }

    for (const auto* pMs : servers())
    {
        const char* zAddress = pMs->address();

        if (m_nodes_by_id.find(zAddress) == m_nodes_by_id.end())
        {
            if (m_obsolete_bootstraps.find(zAddress) == m_obsolete_bootstraps.end())
            {
                MXS_WARNING("Bootstrap server '%s' of monitor '%s' is no longer part of the cluster, "
                            "it should be removed. This warning will be logged once per monitor start.",
                            pMs->name(), name());
                m_obsolete_bootstraps.insert(zAddress);
            }
        }
    }
}

void CsMonitor::probe_cluster(const HostPortPairs& nodes)
{
    m_probe_cluster = false;
    m_last_probe = mxb::SteadyClock::now();

    bool identical = true;

    map<string, set<string>> hosts_by_host;
    vector<string> hosts_to_remove;

    for (const auto& kv1 : nodes)
    {
        const auto& host = kv1.first;

        map<string,Status> status_by_host;
        const auto& config = m_context.config();
        auto result = cs::fetch_cluster_status(host,
                                               config.admin_port, config.admin_base_path,
                                               m_context.http_config(),
                                               &status_by_host);

        if (result.ok())
        {
            set<string> hosts;

            for (const auto& kv2 : status_by_host)
            {
                hosts.insert(kv2.first);
            }

            if (hosts.count(host) != 0)
            {
                if (!hosts.empty() && !hosts_by_host.empty())
                {
                    auto it = hosts_by_host.begin();

                    if (hosts != it->second)
                    {
                        MXS_WARNING("Node %s thinks the cluster consists of %s, while %s thinks "
                                    "it consists of %s.",
                                    host.c_str(), mxb::join(hosts).c_str(),
                                    it->first.c_str(), mxb::join(it->second).c_str());
                        identical = false;
                    }
                }

                hosts_by_host.insert(make_pair(host, hosts));
            }
            else
            {
                MXS_WARNING("Host %s thinks the cluster consists of %s, that is, "
                            "it is not included in it. Taking its word for it.",
                            host.c_str(), mxb::join(hosts).c_str());
                hosts_to_remove.push_back(host);
            }
        }
        else
        {
            MXS_ERROR("Could not fetch cluster status information from %s.", host.c_str());
            hosts_to_remove.push_back(host);
        }
    }

    if (!hosts_to_remove.empty())
    {
        for (const auto& host : hosts_to_remove)
        {
            auto it = m_nodes_by_id.find(host);

            if (it != m_nodes_by_id.end())
            {
                auto& sServer = it->second;
                sServer->set_excluded();

                m_nodes_by_id.erase(it);
            }
            else
            {
                remove_dynamic_host(host);
            }
        }
    }

    if (identical)
    {
        adjust_dynamic_servers(!hosts_by_host.empty() ? hosts_by_host.begin()->second : Hosts());
    }
    else
    {
        MXS_NOTICE("Nodes have different opinion regarding what nodes the cluster contains. "
                   "Figuring out whose opinion should count.");

        probe_fuzzy_cluster(hosts_by_host);
    }
}

void CsMonitor::probe_fuzzy_cluster(const HostsByHost& hosts_by_host)
{
    vector<string> hosts;
    for (const auto& kv : hosts_by_host)
    {
        hosts.push_back(kv.first);
    }

    vector<cs::Config> configs;
    if (fetch_configs(hosts, &configs))
    {
        set<int> revisions;
        int max_revision = -1;

        size_t i = 0;
        auto it = hosts.begin();
        auto jt = configs.begin();

        while (it != hosts.end())
        {
            const auto& config = *jt;

            int revision = config.revision();
            revisions.insert(revision);

            if (revision > max_revision)
            {
                i = it - hosts.begin();
                max_revision = revision;
            }

            ++it;
            ++jt;
        }

        if (revisions.size() == 1)
        {
            MXS_ERROR("All nodes claim to be of revision %d, yet their view of the cluster "
                      "is different.", max_revision);
        }
        else
        {
            string host = hosts[i];
            MXB_NOTICE("Using %s as defining node.", host.c_str());

            adjust_dynamic_servers(hosts_by_host.at(host));
        }
    }
    else
    {
        MXS_ERROR("Could not fetch configs from all hosts, cannot figure out "
                  "whose config to use.");
    }
}

bool CsMonitor::check_bootstrap_servers()
{
    bool rv = false;

    HostPortPairs nodes;
    char* pError = nullptr;

    if (sqlite3_exec(m_pDb, SQL_BN_SELECT, select_cb, &nodes, &pError) == SQLITE_OK)
    {
        set<string> prev_bootstrap_servers;

        for (const auto& node : nodes)
        {
            string s = node.first + ":" + std::to_string(node.second);
            prev_bootstrap_servers.insert(s);
        }

        set<string> current_bootstrap_servers;

        for (const auto* pMs : servers())
        {
            string s = string(pMs->address()) + ":" + std::to_string(pMs->port());
            current_bootstrap_servers.insert(s);
        }

        if (prev_bootstrap_servers == current_bootstrap_servers)
        {
            MXS_NOTICE("Current bootstrap servers are the same as the ones used on "
                       "previous run, using persisted connection information.");
            rv = true;
        }
        else
        {
            if (!prev_bootstrap_servers.empty())
            {
                MXS_NOTICE("Current bootstrap servers (%s) are different than the ones "
                           "used on the previous run (%s), NOT using persistent connection "
                           "information.",
                           mxb::join(current_bootstrap_servers, ", ").c_str(),
                           mxb::join(prev_bootstrap_servers, ", ").c_str());
            }

            rv = remove_persisted_information();

            if (rv)
            {
                rv = persist_bootstrap_servers();
            }
        }
    }
    else
    {
        MXS_WARNING("Could not lookup earlier bootstrap servers: %s", pError ? pError : "Unknown error");
    }

    return rv;
}

bool CsMonitor::remove_persisted_information()
{
    char* pError = nullptr;

    int rv = sqlite3_exec(m_pDb, SQL_BN_DELETE, nullptr, nullptr, &pError);
    if (rv != SQLITE_OK)
    {
        MXS_ERROR("Could not delete persisted bootstrap nodes: %s", pError ? pError : "Unknown error");
    }

    return rv == SQLITE_OK;
}

bool CsMonitor::persist_bootstrap_servers()
{
    bool rv = true;

    string values;

    for (const auto* pMs : servers())
    {
        if (!values.empty())
        {
            values += ", ";
        }

        SERVER* pServer = pMs->server;
        string value;
        value += string("'") + pServer->address() + string("'");
        value += ", ";
        value += std::to_string(pServer->port());

        values += "(";
        values += value;
        values += ")";
    }

    if (!values.empty())
    {
        char insert[sizeof(SQL_BN_INSERT_FORMAT) + values.length()];
        sprintf(insert, SQL_BN_INSERT_FORMAT, values.c_str());

        char* pError = nullptr;
        if (sqlite3_exec(m_pDb, insert, nullptr, nullptr, &pError) != SQLITE_OK)
        {
            MXS_ERROR("Could not persist information about current bootstrap nodes: %s",
                      pError ? pError : "Unknown error");
            rv = false;
        }
    }

    return rv;
}

void CsMonitor::populate_from_bootstrap_servers()
{
    for (auto* pMs : servers())
    {
        SERVER* pServer = pMs->server;

        run_in_mainworker([this, pServer]() {
                service_add_server(this, pServer);
            });
    }
}

bool CsMonitor::fetch_configs(const std::vector<std::string>& hosts, std::vector<Config>* pConfigs)
{
    const auto& config = m_context.config();

    return cs::fetch_configs(hosts,
                             config.admin_port,
                             config.admin_base_path,
                             m_context.http_config(),
                             pConfigs);
}

bool CsMonitor::should_probe_cluster() const
{
    bool rv = false;

    const auto& config = m_context.config();

    if (config.dynamic_node_detection)
    {
        auto now = mxb::SteadyClock::now();

        if (m_probe_cluster || (now - m_last_probe >= config.cluster_monitor_interval))
        {
            rv = true;
        }
    }

    return rv;
}

string CsMonitor::create_dynamic_name(const string& host) const
{
    return string("@@") + m_name + ":" + host;
}

CsDynamicServer* CsMonitor::get_dynamic_server(const SERVER* pServer) const
{
    CsDynamicServer* pDs = nullptr;

    if (m_context.config().dynamic_node_detection)
    {
        if (strncmp(pServer->name(), "@@", 2) == 0)
        {
            string s = pServer->name() + 2;
            auto i = s.find(':');

            if (i != string::npos)
            {
                auto name = s.substr(0, i);

                if (name == m_name)
                {
                    string host = s.substr(i + 1);

                    auto it = m_nodes_by_id.find(host);

                    if (it != m_nodes_by_id.end())
                    {
                        pDs = it->second.get();
                    }
                }
            }
        }
    }

    return pDs;
}
