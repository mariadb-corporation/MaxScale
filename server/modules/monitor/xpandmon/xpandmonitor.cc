/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "xpandmonitor.hh"
#include <algorithm>
#include <set>
#include <maxbase/string.hh>
#include <maxscale/json_api.hh>
#include <maxscale/paths.hh>
#include <maxscale/secrets.hh>
#include <maxscale/sqlite3.h>
#include "../../../core/internal/config_runtime.hh"
#include "../../../core/internal/service.hh"

namespace http = mxb::http;
using namespace std;
using maxscale::MonitorServer;

#define LOG_JSON_ERROR(ppJson, format, ...)                             \
    do {                                                                \
        MXS_ERROR(format, ##__VA_ARGS__);                               \
        if (ppJson)                                                     \
        {                                                               \
            *ppJson = mxs_json_error_append(*ppJson, format, ##__VA_ARGS__); \
        }                                                               \
    } while (false)

namespace
{

namespace xpandmon
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::MONITOR);

config::ParamDuration<std::chrono::milliseconds>
cluster_monitor_interval(&specification,
                         "cluster_monitor_interval",
                         "How frequently the Xpand monitor should perform a cluster check.",
                         mxs::config::INTERPRET_AS_MILLISECONDS,
                         std::chrono::milliseconds(DEFAULT_CLUSTER_MONITOR_INTERVAL));

config::ParamCount
    health_check_threshold(&specification,
                           "health_check_threshold",
                           "How many failed health port pings before node is assumed to be down.",
                           DEFAULT_HEALTH_CHECK_THRESHOLD,
                           1, std::numeric_limits<uint32_t>::max());    // min, max

config::ParamBool
    dynamic_node_detection(&specification,
                           "dynamic_node_detection",
                           "Should cluster configuration be figured out at runtime.",
                           DEFAULT_DYNAMIC_NODE_DETECTION);

config::ParamInteger
    health_check_port(&specification,
                      "health_check_port",
                      "Port number for Xpand health check.",
                      DEFAULT_HEALTH_CHECK_PORT,
                      0, std::numeric_limits<uint16_t>::max());     // min, max
}

const int DEFAULT_MYSQL_PORT = 3306;
const int DEFAULT_HEALTH_PORT = 3581;

// Change this, if the schema is changed.
const int SCHEMA_VERSION = 1;

static const char SQL_BN_CREATE[] =
    "CREATE TABLE IF NOT EXISTS bootstrap_nodes "
    "(ip CARCHAR(255), mysql_port INT)";

static const char SQL_BN_INSERT_FORMAT[] =
    "INSERT INTO bootstrap_nodes (ip, mysql_port) "
    "VALUES %s";

static const char SQL_BN_DELETE[] =
    "DELETE FROM bootstrap_nodes";

static const char SQL_BN_SELECT[] =
    "SELECT ip, mysql_port FROM bootstrap_nodes";


static const char SQL_DN_CREATE[] =
    "CREATE TABLE IF NOT EXISTS dynamic_nodes "
    "(id INT PRIMARY KEY, ip VARCHAR(255), mysql_port INT, health_port INT)";

static const char SQL_DN_UPSERT_FORMAT[] =
    "INSERT OR REPLACE INTO dynamic_nodes (id, ip, mysql_port, health_port) "
    "VALUES (%d, '%s', %d, %d)";

static const char SQL_DN_DELETE_FORMAT[] =
    "DELETE FROM dynamic_nodes WHERE id = %d";

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
}

namespace
{

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
                  "about dynamically detected Xpand nodes. The Xpand "
                  "monitor will remain dependent upon statically defined "
                  "bootstrap nodes.");
    }

    return pDb;
}

void run_in_mainworker(const function<void(void)>& func)
{
    auto mw = mxs::MainWorker::get();
    // Using the semaphore-version of 'execute' to wait until completion causes deadlock. Reason unclear.
    mw->execute(func, mxb::Worker::EXECUTE_AUTO);
}
}

XpandMonitor::Config::Config(const std::string& name)
    : config::Configuration(name, &xpandmon::specification)
    , m_cluster_monitor_interval(this, &xpandmon::cluster_monitor_interval)
    , m_health_check_threshold(this, &xpandmon::health_check_threshold)
    , m_dynamic_node_detection(this, &xpandmon::dynamic_node_detection)
    , m_health_check_port(this, &xpandmon::health_check_port)
{
}

// static
void XpandMonitor::Config::populate(MXS_MODULE& module)
{
    module.specification = &xpandmon::specification;
}

XpandMonitor::XpandMonitor(const string& name, const string& module, sqlite3* pDb)
    : MonitorWorker(name, module)
    , m_config(name)
    , m_pDb(pDb)
{
}

XpandMonitor::~XpandMonitor()
{
    sqlite3_close_v2(m_pDb);
}

// static
XpandMonitor* XpandMonitor::create(const string& name, const string& module)
{
    string path = mxs::datadir();

    path += "/";
    path += name;

    if (!mxs_mkdir_all(path.c_str(), 0744))
    {
        MXS_ERROR("Could not create the directory %s, MaxScale will not be "
                  "able to create database for persisting connection "
                  "information of dynamically detected Xpand nodes.",
                  path.c_str());
    }

    path += "/xpand_nodes-v";
    path += std::to_string(SCHEMA_VERSION);
    path += ".db";

    sqlite3* pDb = open_or_create_db(path);

    XpandMonitor* pThis = nullptr;

    if (pDb)
    {
        // Even if the creation/opening of the sqlite3 database fails, we will still
        // get a valid database handle.
        pThis = new XpandMonitor(name, module, pDb);
    }
    else
    {
        // The handle will be null, *only* if the opening fails due to a memory
        // allocation error.
        MXS_ALERT("sqlite3 memory allocation failed, the Xpand monitor "
                  "cannot continue.");
    }

    return pThis;
}

using std::chrono::milliseconds;

bool XpandMonitor::configure(const mxs::ConfigParameters* pParams)
{
    if (!xpandmon::specification.validate(*pParams))
    {
        return false;
    }

    if (!MonitorWorker::configure(pParams))
    {
        return false;
    }

    check_bootstrap_servers();

    m_health_urls.clear();
    m_nodes_by_id.clear();

    // Since they were validated above, failure should not be an option now.
    MXB_AT_DEBUG(bool configured = ) m_config.configure(*pParams);
    mxb_assert(configured);

    return true;
}

void XpandMonitor::populate_services()
{
    mxb_assert(!is_running());

    // The servers that the Xpand monitor has been configured with are
    // only used for bootstrapping and services will not be populated
    // with them.
}

bool XpandMonitor::softfail(SERVER* pServer, json_t** ppError)
{
    bool rv = false;

    if (is_running())
    {
        call([this, pServer, ppError, &rv]() {
                rv = perform_softfail(pServer, ppError);
            },
            EXECUTE_QUEUED);
    }
    else
    {
        LOG_JSON_ERROR(ppError,
                       "%s: The monitor is not running and hence "
                       "SOFTFAIL cannot be performed for %s.",
                       name(), pServer->address());
    }

    return true;
}

bool XpandMonitor::unsoftfail(SERVER* pServer, json_t** ppError)
{
    bool rv = false;

    if (is_running())
    {
        call([this, pServer, ppError, &rv]() {
                rv = perform_unsoftfail(pServer, ppError);
            },
            EXECUTE_QUEUED);
    }
    else
    {
        LOG_JSON_ERROR(ppError,
                       "%s: The monitor is not running and hence "
                       "UNSOFTFAIL cannot be performed for %s.",
                       name(), pServer->address());
    }

    return true;
}

void XpandMonitor::server_added(SERVER* pServer)
{
    // The servers explicitly added to the Cluster monitor are only used
    // as bootstrap servers, so they are not added to any services.
}

void XpandMonitor::server_removed(SERVER* pServer)
{
    // @see server_added(), no action is needed.
}


void XpandMonitor::pre_loop()
{
    read_journal();
    if (m_config.dynamic_node_detection())
    {
        // At startup we accept softfailed nodes in an attempt to be able to
        // connect at any cost. It'll be replaced once there is an alternative.
        check_cluster(xpand::Softfailed::ACCEPT);
    }
    else
    {
        populate_from_bootstrap_servers();
    }

    make_health_check();
}

void XpandMonitor::post_loop()
{
    write_journal();
    if (m_pHub_con)
    {
        mysql_close(m_pHub_con);
    }

    m_pHub_con = nullptr;
    m_pHub_server = nullptr;
}

void XpandMonitor::tick()
{
    check_maintenance_requests();
    if (m_config.dynamic_node_detection() && should_check_cluster())
    {
        check_cluster(xpand::Softfailed::REJECT);
    }

    switch (m_http.status())
    {
    case http::Async::PENDING:
        MXS_WARNING("%s: Health check round had not completed when next tick arrived.", name());
        break;

    case http::Async::ERROR:
        MXS_WARNING("%s: Health check round ended with general error.", name());
        make_health_check();
        break;

    case http::Async::READY:
        update_server_statuses();
        make_health_check();
        break;
    }

    flush_server_status();
    process_state_changes();
    hangup_failed_servers();
    write_journal_if_needed();
}

void XpandMonitor::choose_hub(xpand::Softfailed softfailed)
{
    mxb_assert(!m_pHub_con);

    set<string> ips;

    // First we check the dynamic servers, in case there are,
    if (!choose_dynamic_hub(softfailed, ips))
    {
        // then we check the bootstrap servers, and
        if (!choose_bootstrap_hub(softfailed, ips))
        {
            // finally, if all else fails - in practise we will only get here at
            // startup (no dynamic servers) if the bootstrap servers cannot be
            // contacted - we try to refresh the nodes using persisted information
            if (refresh_using_persisted_nodes(ips))
            {
                // and then select a hub from the dynamic ones.
                choose_dynamic_hub(softfailed, ips);
            }
        }
    }

    if (m_pHub_con)
    {
        MXS_NOTICE("%s: Monitoring Xpand cluster state using node %s:%d.",
                   name(), m_pHub_server->address(), m_pHub_server->port());
    }
    else
    {
        MXS_ERROR("%s: Could not connect to any server or no server that could "
                  "be connected to was part of the quorum.", name());
    }
}

bool XpandMonitor::choose_dynamic_hub(xpand::Softfailed softfailed, std::set<string>& ips_checked)
{
    for (auto& kv : m_nodes_by_id)
    {
        XpandNode& node = kv.second;

        if (node.can_be_used_as_hub(name(), conn_settings(), softfailed))
        {
            m_pHub_con = node.release_connection();
            m_pHub_server = node.server();
        }

        ips_checked.insert(node.ip());

        if (m_pHub_con)
        {
            break;
        }
    }

    return m_pHub_con != nullptr;
}

bool XpandMonitor::choose_bootstrap_hub(xpand::Softfailed softfailed, std::set<string>& ips_checked)
{
    for (auto* pMs : servers())
    {
        if (ips_checked.find(pMs->server->address()) == ips_checked.end())
        {
            if (xpand::ping_or_connect_to_hub(name(), conn_settings(), softfailed, *pMs))
            {
                m_pHub_con = pMs->con;
                m_pHub_server = pMs->server;
            }
            else if (pMs->con)
            {
                mysql_close(pMs->con);
            }

            pMs->con = nullptr;
        }

        if (m_pHub_con)
        {
            break;
        }
    }

    return m_pHub_con != nullptr;
}

bool XpandMonitor::refresh_using_persisted_nodes(std::set<string>& ips_checked)
{
    MXS_NOTICE("Attempting to find a Xpand bootstrap node from one of the nodes "
               "used during the previous run of MaxScale.");

    bool refreshed = false;

    HostPortPairs nodes;
    char* pError = nullptr;
    int rv = sqlite3_exec(m_pDb, SQL_DN_SELECT, select_cb, &nodes, &pError);

    if (rv == SQLITE_OK)
    {
        const std::string& username = conn_settings().username;
        const std::string& password = conn_settings().password;
        const std::string dec_password = mxs::decrypt_password(password);

        auto it = nodes.begin();

        while (!refreshed && (it != nodes.end()))
        {
            const auto& node = *it;

            const std::string& host = node.first;

            if (ips_checked.find(host) == ips_checked.end())
            {
                ips_checked.insert(host);
                int port = node.second;

                MXS_NOTICE("Trying to find out cluster nodes from %s:%d.", host.c_str(), port);

                MYSQL* pHub_con = mysql_init(NULL);

                if (mysql_real_connect(pHub_con, host.c_str(),
                                       username.c_str(), dec_password.c_str(),
                                       nullptr,
                                       port, nullptr, 0))
                {
                    if (xpand::is_part_of_the_quorum(name(), pHub_con))
                    {
                        if (refresh_nodes(pHub_con))
                        {
                            MXS_NOTICE("Cluster nodes refreshed.");
                            refreshed = true;
                        }
                    }
                    else
                    {
                        MXS_WARNING("%s:%d is not part of the quorum, ignoring.", host.c_str(), port);
                    }
                }
                else
                {
                    MXS_WARNING("Could not connect to %s:%d.", host.c_str(), port);
                }

                mysql_close(pHub_con);
            }

            ++it;
        }
    }
    else
    {
        MXS_ERROR("Could not look up persisted nodes: %s", pError ? pError : "Unknown error");
    }

    return refreshed;
}

bool XpandMonitor::refresh_nodes()
{
    mxb_assert(m_pHub_con);

    return refresh_nodes(m_pHub_con);
}

bool XpandMonitor::refresh_nodes(MYSQL* pHub_con)
{
    mxb_assert(pHub_con);

    map<int, XpandMembership> memberships;

    bool refreshed = check_cluster_membership(pHub_con, &memberships);

    if (refreshed)
    {
        const char ZQUERY[] =
            "SELECT ni.nodeid, ni.iface_ip, ni.mysql_port, ni.healthmon_port, sn.nodeid "
            "FROM system.nodeinfo AS ni "
            "LEFT JOIN system.softfailed_nodes AS sn ON ni.nodeid = sn.nodeid";

        if (mysql_query(pHub_con, ZQUERY) == 0)
        {
            MYSQL_RES* pResult = mysql_store_result(pHub_con);

            if (pResult)
            {
                mxb_assert(mysql_field_count(pHub_con) == 5);

                set<int> nids;
                for (const auto& kv : m_nodes_by_id)
                {
                    const XpandNode& node = kv.second;
                    nids.insert(node.id());
                }

                MYSQL_ROW row;
                while ((row = mysql_fetch_row(pResult)) != nullptr)
                {
                    if (row[0] && row[1])
                    {
                        int id = atoi(row[0]);
                        string ip = row[1];
                        int mysql_port = row[2] ? atoi(row[2]) : DEFAULT_MYSQL_PORT;
                        int health_port = row[3] ? atoi(row[3]) : DEFAULT_HEALTH_PORT;
                        bool softfailed = row[4] ? true : false;

                        // '@@' ensures no clash with user created servers.
                        // Monitor name ensures no clash with other Xpand monitor instances.
                        string server_name = string("@@") + m_name + ":node-" + std::to_string(id);

                        auto nit = m_nodes_by_id.find(id);
                        auto mit = memberships.find(id);

                        if (nit != m_nodes_by_id.end())
                        {
                            // Existing node.
                            mxb_assert(SERVER::find_by_unique_name(server_name));

                            XpandNode& node = nit->second;

                            node.update(ip, mysql_port, health_port);

                            bool is_draining = node.server()->is_draining();

                            if (softfailed && !is_draining)
                            {
                                MXS_NOTICE("%s: Node %d (%s) has been SOFTFAILed. "
                                           "Turning ON 'Being Drained'.",
                                           name(), node.id(), node.server()->address());

                                node.server()->set_status(SERVER_DRAINING);
                            }
                            else if (!softfailed && is_draining)
                            {
                                MXS_NOTICE("%s: Node %d (%s) is no longer being SOFTFAILed. "
                                           "Turning OFF 'Being Drained'.",
                                           name(), node.id(), node.server()->address());

                                node.server()->clear_status(SERVER_DRAINING);
                            }

                            nids.erase(id);
                        }
                        else if (mit != memberships.end())
                        {
                            // New node.
                            mxb_assert(!SERVER::find_by_unique_name(server_name));

                            if (runtime_create_volatile_server(server_name, ip, mysql_port))
                            {
                                SERVER* pServer = SERVER::find_by_unique_name(server_name);
                                mxb_assert(pServer);

                                if (pServer)
                                {
                                    if (softfailed)
                                    {
                                        pServer->set_status(SERVER_DRAINING);
                                    }

                                    const XpandMembership& membership = mit->second;
                                    int health_check_threshold = m_config.health_check_threshold();

                                    XpandNode node(this, membership, ip, mysql_port, health_port,
                                                   health_check_threshold, pServer);

                                    m_nodes_by_id.insert(make_pair(id, node));

                                    // New server, so it needs to be added to all services that
                                    // use this monitor for defining its cluster of servers.
                                    run_in_mainworker([this, pServer]() {
                                                          service_add_server(this, pServer);
                                                      });
                                }
                                else
                                {
                                    MXS_ERROR("%s: Created server %s (at %s:%d) could not be "
                                              "looked up using its name.",
                                              name(), server_name.c_str(), ip.c_str(), mysql_port);
                                }
                            }
                            else
                            {
                                MXS_ERROR("%s: Could not create server %s at %s:%d.",
                                          name(), server_name.c_str(), ip.c_str(), mysql_port);
                            }

                            memberships.erase(mit);
                        }
                        else
                        {
                            // Node found in system.node_info but not in system.membership
                            MXS_ERROR("%s: Node %d at %s:%d,%d found in system.node_info "
                                      "but not in system.membership.",
                                      name(), id, ip.c_str(), mysql_port, health_port);
                        }
                    }
                    else
                    {
                        MXS_WARNING("%s: Either nodeid and/or iface_ip is missing, ignoring node.",
                                    name());
                    }
                }

                mysql_free_result(pResult);

                // Any nodes that were not found are not available, so their
                // state must be set accordingly.
                for (const auto nid : nids)
                {
                    auto it = m_nodes_by_id.find(nid);
                    mxb_assert(it != m_nodes_by_id.end());

                    XpandNode& node = it->second;
                    node.set_running(false, XpandNode::APPROACH_OVERRIDE);
                }

                cluster_checked();
            }
            else
            {
                MXS_WARNING("%s: No result returned for '%s' on %s.",
                            name(), ZQUERY, mysql_get_host_info(pHub_con));
            }
        }
        else
        {
            MXS_ERROR("%s: Could not execute '%s' on %s: %s",
                      name(), ZQUERY, mysql_get_host_info(pHub_con), mysql_error(pHub_con));
        }

        // Since we are here, the call above to check_cluster_membership() succeeded. As that
        // function may change the content of m_nodes_by_ids, we must always update the urls,
        // irrespective of whether the SQL of this function succeeds or not.
        update_http_urls();
    }

    return refreshed;
}

void XpandMonitor::check_bootstrap_servers()
{
    HostPortPairs nodes;
    char* pError = nullptr;
    int rv = sqlite3_exec(m_pDb, SQL_BN_SELECT, select_cb, &nodes, &pError);

    if (rv == SQLITE_OK)
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
            SERVER* pServer = pMs->server;

            string s = string(pServer->address()) + ":" + std::to_string(pServer->port());
            current_bootstrap_servers.insert(s);
        }

        if (prev_bootstrap_servers == current_bootstrap_servers)
        {
            MXS_NOTICE("Current bootstrap servers are the same as the ones used on "
                       "previous run, using persisted connection information.");
        }
        else if (!prev_bootstrap_servers.empty())
        {
            MXS_NOTICE("Current bootstrap servers (%s) are different than the ones "
                       "used on the previous run (%s), NOT using persistent connection "
                       "information.",
                       mxb::join(current_bootstrap_servers, ", ").c_str(),
                       mxb::join(prev_bootstrap_servers, ", ").c_str());

            if (remove_persisted_information())
            {
                persist_bootstrap_servers();
            }
        }
    }
    else
    {
        MXS_WARNING("Could not lookup earlier bootstrap servers: %s", pError ? pError : "Unknown error");
    }
}

bool XpandMonitor::remove_persisted_information()
{
    char* pError = nullptr;
    int rv;

    int rv1 = sqlite3_exec(m_pDb, SQL_BN_DELETE, nullptr, nullptr, &pError);
    if (rv1 != SQLITE_OK)
    {
        MXS_ERROR("Could not delete persisted bootstrap nodes: %s", pError ? pError : "Unknown error");
    }

    int rv2 = sqlite3_exec(m_pDb, SQL_DN_DELETE, nullptr, nullptr, &pError);
    if (rv2 != SQLITE_OK)
    {
        MXS_ERROR("Could not delete persisted dynamic nodes: %s", pError ? pError : "Unknown error");
    }

    return rv1 == SQLITE_OK && rv2 == SQLITE_OK;
}

void XpandMonitor::persist_bootstrap_servers()
{
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
        int rv = sqlite3_exec(m_pDb, insert, nullptr, nullptr, &pError);

        if (rv != SQLITE_OK)
        {
            MXS_ERROR("Could not persist information about current bootstrap nodes: %s",
                      pError ? pError : "Unknown error");
        }
    }
}

void XpandMonitor::check_cluster(xpand::Softfailed softfailed)
{
    if (m_pHub_con)
    {
        check_hub(softfailed);
    }

    if (!m_pHub_con)
    {
        choose_hub(softfailed);
    }

    if (m_pHub_con)
    {
        refresh_nodes();
    }
}

void XpandMonitor::check_hub(xpand::Softfailed softfailed)
{
    mxb_assert(m_pHub_con);
    mxb_assert(m_pHub_server);

    if (!xpand::ping_or_connect_to_hub(name(), conn_settings(), softfailed, *m_pHub_server, &m_pHub_con))
    {
        mysql_close(m_pHub_con);
        m_pHub_con = nullptr;
    }
}

bool XpandMonitor::check_cluster_membership(MYSQL* pHub_con,
                                            std::map<int, XpandMembership>* pMemberships)
{
    mxb_assert(pHub_con);
    mxb_assert(pMemberships);

    bool rv = false;

    const char ZQUERY[] = "SELECT nid, status, instance, substate FROM system.membership";

    if (mysql_query(pHub_con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(pHub_con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(pHub_con) == 4);

            set<int> nids;
            for (const auto& kv : m_nodes_by_id)
            {
                const XpandNode& node = kv.second;
                nids.insert(node.id());
            }

            MYSQL_ROW row;
            while ((row = mysql_fetch_row(pResult)) != nullptr)
            {
                if (row[0])
                {
                    int nid = atoi(row[0]);
                    string status = row[1] ? row[1] : "unknown";
                    int instance = row[2] ? atoi(row[2]) : -1;
                    string substate = row[3] ? row[3] : "unknown";

                    auto it = m_nodes_by_id.find(nid);

                    if (it != m_nodes_by_id.end())
                    {
                        XpandNode& node = it->second;

                        node.update(xpand::status_from_string(status),
                                    xpand::substate_from_string(substate),
                                    instance);

                        nids.erase(node.id());
                    }
                    else
                    {
                        XpandMembership membership(nid,
                                                   xpand::status_from_string(status),
                                                   xpand::substate_from_string(substate),
                                                   instance);

                        pMemberships->insert(make_pair(nid, membership));
                    }
                }
                else
                {
                    MXS_WARNING("%s: No node id returned in row for '%s'.",
                                name(), ZQUERY);
                }
            }

            mysql_free_result(pResult);

            // Deactivate all servers that are no longer members.
            for (const auto nid : nids)
            {
                auto it = m_nodes_by_id.find(nid);
                mxb_assert(it != m_nodes_by_id.end());

                XpandNode& node = it->second;
                node.deactivate_server();
                m_nodes_by_id.erase(it);
            }

            rv = true;
        }
        else
        {
            MXS_WARNING("%s: No result returned for '%s'.", name(), ZQUERY);
        }
    }
    else
    {
        MXS_ERROR("%s: Could not execute '%s' on %s: %s",
                  name(), ZQUERY, mysql_get_host_info(pHub_con), mysql_error(pHub_con));
    }

    return rv;
}

void XpandMonitor::populate_from_bootstrap_servers()
{
    int id = 1;

    for (auto ms : servers())
    {
        SERVER* pServer = ms->server;

        xpand::Status status = xpand::Status::UNKNOWN;
        xpand::SubState substate = xpand::SubState::UNKNOWN;
        int instance = 1;
        XpandMembership membership(id, status, substate, instance);

        std::string ip = pServer->address();
        int mysql_port = pServer->port();
        int health_port = m_config.health_check_port();
        int health_check_threshold = m_config.health_check_threshold();

        XpandNode node(this, membership, ip, mysql_port, health_port, health_check_threshold, pServer);

        m_nodes_by_id.insert(make_pair(id, node));
        ++id;

        // New server, so it needs to be added to all services that
        // use this monitor for defining its cluster of servers.
        run_in_mainworker([this, pServer]() {
                              service_add_server(this, pServer);
                          });
    }

    update_http_urls();
}

void XpandMonitor::update_server_statuses()
{
    for (auto* pMs : servers())
    {
        pMs->stash_current_status();

        auto it = find_if(m_nodes_by_id.begin(), m_nodes_by_id.end(),
                          [pMs](const std::pair<int, XpandNode>& element) -> bool {
                              const XpandNode& info = element.second;
                              return pMs->server->address() == info.ip();
                          });

        if (it != m_nodes_by_id.end())
        {
            const XpandNode& info = it->second;

            if (info.is_running())
            {
                pMs->set_pending_status(SERVER_MASTER | SERVER_RUNNING);
            }
            else
            {
                pMs->clear_pending_status(SERVER_MASTER | SERVER_RUNNING);
            }
        }
        else
        {
            pMs->clear_pending_status(SERVER_MASTER | SERVER_RUNNING);
        }
    }
}

void XpandMonitor::make_health_check()
{
    mxb_assert(m_http.status() != http::Async::PENDING);

    m_http = mxb::http::get_async(m_health_urls);

    switch (m_http.status())
    {
    case http::Async::PENDING:
        initiate_delayed_http_check();
        break;

    case http::Async::ERROR:
        MXS_ERROR("%s: Could not initiate health check.", name());
        break;

    case http::Async::READY:
        MXS_INFO("%s: Health check available immediately.", name());
        break;
    }
}

void XpandMonitor::initiate_delayed_http_check()
{
    mxb_assert(m_delayed_http_check_id == 0);

    long max_delay_ms = settings().interval / 10;

    long ms = m_http.wait_no_more_than();

    if (ms > max_delay_ms)
    {
        ms = max_delay_ms;
    }

    m_delayed_http_check_id = delayed_call(ms, &XpandMonitor::check_http, this);
}

bool XpandMonitor::check_http(Call::action_t action)
{
    m_delayed_http_check_id = 0;

    if (action == Call::EXECUTE)
    {
        switch (m_http.perform())
        {
        case http::Async::PENDING:
            initiate_delayed_http_check();
            break;

        case http::Async::READY:
            {
                mxb_assert(m_health_urls == m_http.urls());
                // There are as many responses as there are nodes,
                // and the responses are in node order.
                const vector<http::Response>& responses = m_http.responses();
                mxb_assert(responses.size() == m_nodes_by_id.size());

                auto it = m_nodes_by_id.begin();

                for (const auto& response : responses)
                {
                    bool running = (response.code == 200);      // HTTP OK

                    XpandNode& node = it->second;

                    node.set_running(running);

                    if (!running)
                    {
                        // We have to explicitly check whether the node is to be
                        // considered down, as the value of `health_check_threshold`
                        // defines how quickly a node should be considered down.
                        if (!node.is_running())
                        {
                            // Ok, the node is down. Trigger a cluster check at next tick.
                            trigger_cluster_check();
                        }
                    }

                    ++it;
                }
            }
            break;

        case http::Async::ERROR:
            MXS_ERROR("%s: Health check waiting ended with general error.", name());
        }
    }

    return false;
}

void XpandMonitor::update_http_urls()
{
    vector<string> health_urls;
    for (const auto& kv : m_nodes_by_id)
    {
        const XpandNode& node = kv.second;
        string url = "http://" + node.ip() + ":" + std::to_string(node.health_port());

        health_urls.push_back(url);
    }

    if (m_health_urls != health_urls)
    {
        if (m_delayed_http_check_id != 0)
        {
            cancel_delayed_call(m_delayed_http_check_id);
            m_delayed_http_check_id = 0;
        }

        m_http.reset();

        m_health_urls.swap(health_urls);
    }
}

bool XpandMonitor::perform_softfail(SERVER* pServer, json_t** ppError)
{
    bool rv = perform_operation(Operation::SOFTFAIL, pServer, ppError);

    // Irrespective of whether the operation succeeded or not
    // a cluster check is triggered at next tick.
    trigger_cluster_check();

    return rv;
}

bool XpandMonitor::perform_unsoftfail(SERVER* pServer, json_t** ppError)
{
    return perform_operation(Operation::UNSOFTFAIL, pServer, ppError);
}

bool XpandMonitor::perform_operation(Operation operation,
                                     SERVER* pServer,
                                     json_t** ppError)
{
    bool performed = false;

    const char ZSOFTFAIL[] = "SOFTFAIL";
    const char ZUNSOFTFAIL[] = "UNSOFTFAIL";

    const char* zOperation = (operation == Operation::SOFTFAIL) ? ZSOFTFAIL : ZUNSOFTFAIL;

    if (!m_pHub_con)
    {
        check_cluster(xpand::Softfailed::ACCEPT);
    }

    if (m_pHub_con)
    {
        auto it = find_if(m_nodes_by_id.begin(), m_nodes_by_id.end(),
                          [pServer](const std::pair<int, XpandNode>& element) {
                              return element.second.server() == pServer;
                          });

        if (it != m_nodes_by_id.end())
        {
            XpandNode& node = it->second;

            const char ZQUERY_FORMAT[] = "ALTER CLUSTER %s %d";

            int id = node.id();
            // ZUNSOFTFAIL is longer
            char zQuery[sizeof(ZQUERY_FORMAT) + sizeof(ZUNSOFTFAIL) + UINTLEN(id)];

            sprintf(zQuery, ZQUERY_FORMAT, zOperation, id);

            if (mysql_query(m_pHub_con, zQuery) == 0)
            {
                MXS_NOTICE("%s: %s performed on node %d (%s).",
                           name(), zOperation, id, pServer->address());

                if (operation == Operation::SOFTFAIL)
                {
                    MXS_NOTICE("%s: Turning on 'Being Drained' on server %s.",
                               name(), pServer->address());
                    pServer->set_status(SERVER_DRAINING);
                }
                else
                {
                    mxb_assert(operation == Operation::UNSOFTFAIL);

                    MXS_NOTICE("%s: Turning off 'Being Drained' on server %s.",
                               name(), pServer->address());
                    pServer->clear_status(SERVER_DRAINING);
                }
            }
            else
            {
                LOG_JSON_ERROR(ppError,
                               "%s: The execution of '%s' failed: %s",
                               name(), zQuery, mysql_error(m_pHub_con));
            }
        }
        else
        {
            LOG_JSON_ERROR(ppError,
                           "%s: The server %s is not being monitored, "
                           "cannot perform %s.",
                           name(), pServer->address(), zOperation);
        }
    }
    else
    {
        LOG_JSON_ERROR(ppError,
                       "%s: Could not could not connect to any Xpand node, "
                       "cannot perform %s of %s.",
                       name(), zOperation, pServer->address());
    }

    return performed;
}

void XpandMonitor::persist(const XpandNode& node)
{
    if (!m_pDb)
    {
        return;
    }

    char sql_upsert[sizeof(SQL_DN_UPSERT_FORMAT) + 10 + node.ip().length() + 10 + 10];

    int id = node.id();
    const char* zIp = node.ip().c_str();
    int mysql_port = node.mysql_port();
    int health_port = node.health_port();

    sprintf(sql_upsert, SQL_DN_UPSERT_FORMAT, id, zIp, mysql_port, health_port);

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_upsert, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_INFO("Updated Xpand node in bookkeeping: %d, '%s', %d, %d.",
                 id, zIp, mysql_port, health_port);
    }
    else
    {
        MXS_ERROR("Could not update Ćlustrix node (%d, '%s', %d, %d) in bookkeeping: %s",
                  id, zIp, mysql_port, health_port, pError ? pError : "Unknown error");
    }
}

void XpandMonitor::unpersist(const XpandNode& node)
{
    if (!m_pDb)
    {
        return;
    }

    char sql_delete[sizeof(SQL_DN_UPSERT_FORMAT) + 10];

    int id = node.id();

    sprintf(sql_delete, SQL_DN_DELETE_FORMAT, id);

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_delete, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_INFO("Deleted Xpand node %d from bookkeeping.", id);
    }
    else
    {
        MXS_ERROR("Could not delete Ćlustrix node %d from bookkeeping: %s",
                  id, pError ? pError : "Unknown error");
    }
}
