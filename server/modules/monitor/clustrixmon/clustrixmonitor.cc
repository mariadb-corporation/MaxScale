/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "clustrixmonitor.hh"
#include <algorithm>
#include <set>
#include <maxscale/json_api.hh>
#include <maxscale/paths.h>
#include "../../../core/internal/config_runtime.hh"
#include "../../../core/internal/service.hh"

namespace http = mxb::http;
using namespace std;
using maxscale::MonitorServer;

#define LOG_JSON_ERROR(ppJson, format, ...) \
    do { \
        MXS_ERROR(format, ##__VA_ARGS__); \
        if (ppJson) \
        { \
            *ppJson = mxs_json_error_append(*ppJson, format, ##__VA_ARGS__); \
        } \
    } while (false)

namespace
{

const int DEFAULT_MYSQL_PORT = 3306;
const int DEFAULT_HEALTH_PORT = 3581;

// Change this, if the schema is changed.
const int SCHEMA_VERSION = 1;

static const char SQL_CREATE[] =
    "CREATE TABLE IF NOT EXISTS clustrix_nodes "
    "(id INT PRIMARY KEY, ip VARCHAR(255), mysql_port INT, health_port INT)";

static const char SQL_UPSERT_FORMAT[] =
    "INSERT OR REPLACE INTO clustrix_nodes (id, ip, mysql_port, health_port) "
    "VALUES (%d, '%s', %d, %d)";

static const char SQL_DELETE_FORMAT[] =
    "DELETE FROM clustrix_nodes WHERE id = %d";
}

namespace
{

bool create_schema(sqlite3* pDb)
{
    char* pError = nullptr;
    int rv = sqlite3_exec(pDb, SQL_CREATE, nullptr, nullptr, &pError);

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
                          "initialized. You should delete the database manually.", path.c_str());
                sqlite3_close_v2(pDb);
                pDb = nullptr;
            }
        }
    }
    else
    {
        MXS_ERROR("Opening/creating the sqlite3 database %s failed: %s",
                  path.c_str(), sqlite3_errstr(rv));
    }

    return pDb;
}

}

ClustrixMonitor::ClustrixMonitor(const string& name, const string& module, sqlite3* pDb)
    : MonitorWorker(name, module)
    , m_pDb(pDb)
{
}

ClustrixMonitor::~ClustrixMonitor()
{
    sqlite3_close_v2(m_pDb);
}

//static
ClustrixMonitor* ClustrixMonitor::create(const string& name, const string& module)
{
    string path = get_datadir();

    path += "/";
    path += name;
    path += "/clustrix_nodes-";
    path += std::to_string(SCHEMA_VERSION);
    path += ".db";

    sqlite3* pDb = open_or_create_db(path);

    if (!pDb)
    {
        MXS_WARNING("Could not open sqlite3 database for storing information "
                    "about dynamically detected Clustrix nodes. The Clustrix "
                    "monitor will remain dependant upon statically defined "
                    "bootstrap nodes.");
    }

    return new ClustrixMonitor(name, module, pDb);
}

bool ClustrixMonitor::configure(const MXS_CONFIG_PARAMETER* pParams)
{
    if (!MonitorWorker::configure(pParams))
    {
        return false;
    }

    m_health_urls.clear();
    m_nodes.clear();

    m_config.set_cluster_monitor_interval(pParams->get_integer(CLUSTER_MONITOR_INTERVAL_NAME));
    m_config.set_health_check_threshold(pParams->get_integer(HEALTH_CHECK_THRESHOLD_NAME));

    return true;
}

void ClustrixMonitor::populate_services()
{
    mxb_assert(state() == MONITOR_STATE_STOPPED);

    // The servers that the Clustrix monitor has been configured with are
    // only used for bootstrapping and services will not be populated
    // with them.
}

bool ClustrixMonitor::softfail(SERVER* pServer, json_t** ppError)
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
                       name(), pServer->address);
    }

    return true;
}

bool ClustrixMonitor::unsoftfail(SERVER* pServer, json_t** ppError)
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
                       name(), pServer->address);
    }

    return true;
}

void ClustrixMonitor::server_added(SERVER* pServer)
{
    // The servers explicitly added to the Cluster monitor are only used
    // as bootstrap servers, so they are not added to any services.
}

void ClustrixMonitor::server_removed(SERVER* pServer)
{
    // @see server_added(), no action is needed.
}


void ClustrixMonitor::pre_loop()
{
    // At startup we accept softfailed nodes in an attempt to be able to
    // connect at any cost. It'll be replaced once there is an alternative.
    check_cluster(Clustrix::Softfailed::ACCEPT);

    make_health_check();
}

void ClustrixMonitor::post_loop()
{
    if (m_pHub_con)
    {
        mysql_close(m_pHub_con);
    }

    m_pHub_con = nullptr;
    m_pHub_server = nullptr;
}

void ClustrixMonitor::tick()
{
    if (should_check_cluster())
    {
        check_cluster(Clustrix::Softfailed::REJECT);
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

        if (!m_health_urls.empty())
        {
	    make_health_check();
	}
	break;
    }
}

void ClustrixMonitor::choose_hub(Clustrix::Softfailed softfailed)
{
    mxb_assert(!m_pHub_con);

    SERVER* pHub_server = nullptr;
    MYSQL* pHub_con = nullptr;

    set<string> ips;

    // First we check the dynamic servers, in case there are.
    for (auto it = m_nodes.begin(); !pHub_con && (it != m_nodes.end()); ++it)
    {
        auto& element = *it;
        ClustrixNode& node = element.second;

        if (node.can_be_used_as_hub(name(), m_settings.conn_settings))
        {
            pHub_con = node.release_connection();
            pHub_server = node.server();
        }

        ips.insert(node.ip());
    }

    if (!pHub_con)
    {
        // If that fails, then we check the bootstrap servers, but only if
        // it was not checked above.

        for (auto it = m_servers.begin(); !pHub_con && (it != m_servers.end()); ++it)
        {
            MonitorServer& ms = **it;

            if (ips.find(ms.server->address) == ips.end())
            {
                if (Clustrix::ping_or_connect_to_hub(name(), m_settings.conn_settings, softfailed, ms))
                {
                    pHub_con = ms.con;
                    pHub_server = ms.server;
                }
                else if (ms.con)
                {
                    mysql_close(ms.con);
                }

                ms.con = nullptr;
            }
        }
    }

    if (pHub_con)
    {
        MXS_NOTICE("%s: Monitoring Clustrix cluster state using node %s:%d.",
                   name(), pHub_server->address, pHub_server->port);

        m_pHub_con = pHub_con;
        m_pHub_server = pHub_server;

        mxb_assert(m_pHub_con);
        mxb_assert(m_pHub_con);
    }
    else
    {
        MXS_ERROR("%s: Could not connect to any server or no server that could "
                  "be connected to was part of the quorum.", name());
    }
}

void ClustrixMonitor::refresh_nodes()
{
    mxb_assert(m_pHub_con);

    map<int, ClustrixMembership> memberships;

    if (check_cluster_membership(&memberships))
    {
        const char ZQUERY[] =
            "SELECT ni.nodeid, ni.iface_ip, ni.mysql_port, ni.healthmon_port, sn.nodeid "
            "FROM system.nodeinfo AS ni "
            "LEFT JOIN system.softfailed_nodes AS sn ON ni.nodeid = sn.nodeid";

        if (mysql_query(m_pHub_con, ZQUERY) == 0)
        {
            MYSQL_RES* pResult = mysql_store_result(m_pHub_con);

            if (pResult)
            {
                mxb_assert(mysql_field_count(m_pHub_con) == 5);

                set<int> nids;
                for (const auto& element : m_nodes)
                {
                    const ClustrixNode& node = element.second;
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
                        // Monitor name ensures no clash with other Clustrix monitor instances.
                        string server_name = string("@@") + m_name + ":node-" + std::to_string(id);

                        auto nit = m_nodes.find(id);
                        auto mit = memberships.find(id);

                        if (nit != m_nodes.end())
                        {
                            // Existing node.
                            mxb_assert(SERVER::find_by_unique_name(server_name));

                            ClustrixNode& node = nit->second;

                            bool changed = false;

                            if (node.ip() != ip)
                            {
                                node.set_ip(ip);
                                changed = true;
                            }

                            if (node.mysql_port() != mysql_port)
                            {
                                node.set_mysql_port(mysql_port);
                                changed = true;
                            }

                            if (node.health_port() != health_port)
                            {
                                node.set_health_port(health_port);
                                changed = true;
                            }

                            bool is_draining = node.server()->is_draining();

                            if (softfailed && !is_draining)
                            {
                                MXS_NOTICE("%s: Node %d (%s) has been SOFTFAILed. "
                                           "Turning ON 'Being Drained'.",
                                           name(), node.id(), node.server()->address);

                                node.server()->set_status(SERVER_DRAINING);
                            }
                            else if (!softfailed && is_draining)
                            {
                                MXS_NOTICE("%s: Node %d (%s) is no longer being SOFTFAILed. "
                                           "Turning OFF 'Being Drained'.",
                                           name(), node.id(), node.server()->address);

                                node.server()->clear_status(SERVER_DRAINING);
                            }

                            if (changed)
                            {
                                persist_node(node);
                            }

                            nids.erase(id);
                        }
                        else if (mit != memberships.end())
                        {
                            // New node.
                            mxb_assert(!SERVER::find_by_unique_name(server_name));

                            if (runtime_create_server(server_name.c_str(),
                                                      ip.c_str(),
                                                      std::to_string(mysql_port).c_str(),
                                                      "mariadbbackend",
                                                      "mysqlbackendauth",
                                                      false))
                            {
                                SERVER* pServer = SERVER::find_by_unique_name(server_name);
                                mxb_assert(pServer);

                                if (softfailed)
                                {
                                    pServer->set_status(SERVER_DRAINING);
                                }

                                const ClustrixMembership& membership = mit->second;
                                int health_check_threshold = m_config.health_check_threshold();

                                ClustrixNode node(membership, ip, mysql_port, health_port,
                                                  health_check_threshold, pServer);

                                m_nodes.insert(make_pair(id, node));
                                persist_node(node);

                                // New server, so it needs to be added to all services that
                                // use this monitor for defining its cluster of servers.
                                service_add_server(this, pServer);
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
                    auto it = m_nodes.find(nid);
                    mxb_assert(it != m_nodes.end());

                    ClustrixNode& node = it->second;
                    node.set_running(false, ClustrixNode::APPROACH_OVERRIDE);
                    unpersist_node(node);
                }

                vector<string> health_urls;
                for (const auto& element : m_nodes)
                {
                    const ClustrixNode& node = element.second;
                    string url = "http://" + node.ip() + ":" + std::to_string(node.health_port());

                    health_urls.push_back(url);
                }

                m_health_urls.swap(health_urls);

                cluster_checked();
            }
            else
            {
                MXS_WARNING("%s: No result returned for '%s' on %s.",
                            name(), ZQUERY, m_pHub_server->address);
            }
        }
        else
        {
            MXS_ERROR("%s: Could not execute '%s' on %s: %s",
                      name(), ZQUERY, m_pHub_server->address, mysql_error(m_pHub_con));
        }
    }
}

void ClustrixMonitor::check_cluster(Clustrix::Softfailed softfailed)
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

void ClustrixMonitor::check_hub(Clustrix::Softfailed softfailed)
{
    mxb_assert(m_pHub_con);
    mxb_assert(m_pHub_server);

    if (!Clustrix::ping_or_connect_to_hub(name(), m_settings.conn_settings, softfailed,
                                          *m_pHub_server, &m_pHub_con))
    {
        mysql_close(m_pHub_con);
        m_pHub_con = nullptr;
    }
}

bool ClustrixMonitor::check_cluster_membership(std::map<int, ClustrixMembership>* pMemberships)
{
    mxb_assert(pMemberships);

    mxb_assert(m_pHub_con);
    mxb_assert(m_pHub_server);

    bool rv = false;

    const char ZQUERY[] = "SELECT nid, status, instance, substate FROM system.membership";

    if (mysql_query(m_pHub_con, ZQUERY) == 0)
    {
        MYSQL_RES* pResult = mysql_store_result(m_pHub_con);

        if (pResult)
        {
            mxb_assert(mysql_field_count(m_pHub_con) == 4);

            set<int> nids;
            for (const auto& element : m_nodes)
            {
                const ClustrixNode& node = element.second;
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

                    auto it = m_nodes.find(nid);

                    if (it != m_nodes.end())
                    {
                        ClustrixNode& node = it->second;

                        node.update(Clustrix::status_from_string(status),
                                    Clustrix::substate_from_string(substate),
                                    instance);

                        nids.erase(node.id());
                    }
                    else
                    {
                        ClustrixMembership membership(nid,
                                                      Clustrix::status_from_string(status),
                                                      Clustrix::substate_from_string(substate),
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
                auto it = m_nodes.find(nid);
                mxb_assert(it != m_nodes.end());

                ClustrixNode& node = it->second;
                node.deactivate_server();
                m_nodes.erase(it);
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
		  name(), ZQUERY, m_pHub_server->address, mysql_error(m_pHub_con));
    }

    return rv;
}

void ClustrixMonitor::update_server_statuses()
{
    mxb_assert(!m_servers.empty());

    for (auto ms : m_servers)
    {
        ms->stash_current_status();

        auto it = find_if(m_nodes.begin(), m_nodes.end(),
                          [ms](const std::pair<int,ClustrixNode>& element) -> bool {
                              const ClustrixNode& info = element.second;
                              return ms->server->address == info.ip();
                          });

        if (it != m_nodes.end())
        {
            const ClustrixNode& info = it->second;

            if (info.is_running())
            {
                ms->set_pending_status(SERVER_MASTER | SERVER_RUNNING);
            }
            else
            {
                ms->clear_pending_status(SERVER_MASTER | SERVER_RUNNING);
            }
        }
        else
        {
            ms->clear_pending_status(SERVER_MASTER | SERVER_RUNNING);
        }
    }
}

void ClustrixMonitor::make_health_check()
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

void ClustrixMonitor::initiate_delayed_http_check()
{
    mxb_assert(m_delayed_http_check_id == 0);

    long max_delay_ms = m_settings.interval / 10;

    long ms = m_http.wait_no_more_than();

    if (ms > max_delay_ms)
    {
        ms = max_delay_ms;
    }

    m_delayed_http_check_id = delayed_call(ms, &ClustrixMonitor::check_http, this);
}

bool ClustrixMonitor::check_http(Call::action_t action)
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
                // There are as many results as there are nodes,
                // and the results are in node order.
                const vector<http::Result>& results = m_http.results();
                mxb_assert(results.size() == m_nodes.size());

                auto it = m_nodes.begin();

                for (const auto& result : results)
                {
                    bool running = (result.code == 200); // HTTP OK

                    ClustrixNode& node = it->second;

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

bool ClustrixMonitor::perform_softfail(SERVER* pServer, json_t** ppError)
{
    bool rv = perform_operation(Operation::SOFTFAIL, pServer, ppError);

    // Irrespective of whether the operation succeeded or not
    // a cluster check is triggered at next tick.
    trigger_cluster_check();

    return rv;
}

bool ClustrixMonitor::perform_unsoftfail(SERVER* pServer, json_t** ppError)
{
    return perform_operation(Operation::UNSOFTFAIL, pServer, ppError);
}

bool ClustrixMonitor::perform_operation(Operation operation,
                                        SERVER* pServer,
                                        json_t** ppError)
{
    bool performed = false;

    const char ZSOFTFAIL[] = "SOFTFAIL";
    const char ZUNSOFTFAIL[] = "UNSOFTFAIL";

    const char* zOperation = (operation == Operation::SOFTFAIL) ? ZSOFTFAIL : ZUNSOFTFAIL;

    if (!m_pHub_con)
    {
        check_cluster(Clustrix::Softfailed::ACCEPT);
    }

    if (m_pHub_con)
    {
        auto it = find_if(m_nodes.begin(), m_nodes.end(),
                          [pServer] (const std::pair<int, ClustrixNode>& element) {
                              return element.second.server() == pServer;
                          });

        if (it != m_nodes.end())
        {
            ClustrixNode& node = it->second;

            const char ZQUERY_FORMAT[] = "ALTER CLUSTER %s %d";

            int id = node.id();
            char zQuery[sizeof(ZQUERY_FORMAT) + sizeof(ZUNSOFTFAIL) + UINTLEN(id)]; // ZUNSOFTFAIL is longer

            sprintf(zQuery, ZQUERY_FORMAT, zOperation, id);

            if (mysql_query(m_pHub_con, zQuery) == 0)
            {
                MXS_NOTICE("%s: %s performed on node %d (%s).",
                           name(), zOperation, id, pServer->address);

                if (operation == Operation::SOFTFAIL)
                {
                    MXS_NOTICE("%s: Turning on 'Being Drained' on server %s.",
                               name(), pServer->address);
                    pServer->set_status(SERVER_DRAINING);
                }
                else
                {
                    mxb_assert(operation == Operation::UNSOFTFAIL);

                    MXS_NOTICE("%s: Turning off 'Being Drained' on server %s.",
                               name(), pServer->address);
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
                           name(), pServer->address, zOperation);
        }
    }
    else
    {
        LOG_JSON_ERROR(ppError,
                       "%s: Could not could not connect to any Clustrix node, "
                       "cannot perform %s of %s.",
                       name(), zOperation, pServer->address);
    }

    return performed;
}

void ClustrixMonitor::persist_node(const ClustrixNode& node)
{
    if (!m_pDb)
    {
        return;
    }

    char sql_upsert[sizeof(SQL_UPSERT_FORMAT) + 10 + node.ip().length() + 10 + 10];

    int id = node.id();
    const char* zIp = node.ip().c_str();
    int mysql_port = node.mysql_port();
    int health_port = node.health_port();

    sprintf(sql_upsert, SQL_UPSERT_FORMAT, id, zIp, mysql_port, health_port);

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_upsert, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_INFO("Updated Clustrix node in bookkeeping: %d, '%s', %d, %d.",
                 id, zIp, mysql_port, health_port);
    }
    else
    {
        MXS_ERROR("Could not update Ćlustrix node (%d, '%s', %d, %d) in bookkeeping: %s",
                  id, zIp, mysql_port, health_port, pError ? pError : "Unknown error");
    }
}

void ClustrixMonitor::unpersist_node(const ClustrixNode& node)
{
    if (!m_pDb)
    {
        return;
    }

    char sql_delete[sizeof(SQL_UPSERT_FORMAT) + 10];

    int id = node.id();

    sprintf(sql_delete, SQL_DELETE_FORMAT, id);

    char* pError = nullptr;
    if (sqlite3_exec(m_pDb, sql_delete, nullptr, nullptr, &pError) == SQLITE_OK)
    {
        MXS_INFO("Deleted Clustrix node %d from bookkeeping.", id);
    }
    else
    {
        MXS_ERROR("Could not delete Ćlustrix node %d from bookkeeping: %s",
                  id, pError ? pError : "Unknown error");
    }
}
