/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/servermanager.hh"

#include <mutex>
#include <string>
#include <vector>
#include <maxbase/format.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/json_api.hh>
#include <maxscale/routingworker.hh>

#include "internal/monitormanager.hh"
#include "internal/service.hh"

using std::string;
using Guard = std::lock_guard<std::mutex>;

namespace
{

class ThisUnit
{
public:

    /**
     * Call a function on every server in the global server list.
     *
     * @param apply The function to apply. If the function returns false, iteration is discontinued.
     */
    void foreach_server(std::function<bool(Server*)> apply)
    {
        Guard guard(m_all_servers_lock);
        for (Server* server : m_all_servers)
        {
            if (!apply(server))
            {
                break;
            }
        }
    }

    void insert_front(Server* server)
    {
        Guard guard(m_all_servers_lock);
        m_all_servers.insert(m_all_servers.begin(), server);
    }

    void erase(Server* server)
    {
        Guard guard(m_all_servers_lock);
        auto it = std::find(m_all_servers.begin(), m_all_servers.end(), server);
        mxb_assert(it != m_all_servers.end());
        m_all_servers.erase(it);
    }

    void clear()
    {
        Guard guard(m_all_servers_lock);

        for (auto s : m_all_servers)
        {
            delete s;
        }

        m_all_servers.clear();
    }

private:
    std::mutex           m_all_servers_lock;/**< Protects access to array */
    std::vector<Server*> m_all_servers;     /**< Global list of servers, in configuration file order */
};

ThisUnit this_unit;
}

Server* ServerManager::create_server(const char* name, const mxs::ConfigParameters& params)
{
    Server* server = Server::server_alloc(name, params);
    if (server)
    {
        // This keeps the order of the servers the same as in 2.2
        this_unit.insert_front(server);
    }
    return server;
}

void ServerManager::server_free(Server* server)
{
    mxb_assert(server);
    this_unit.erase(server);

    mxs::RoutingWorker::execute_concurrently(
        [server]() {
            mxs::RoutingWorker* worker = mxs::RoutingWorker::get_current();
            mxb_assert(worker);

            worker->evict_dcbs(server, mxs::RoutingWorker::Evict::ALL);
        });

    delete server;
}

void ServerManager::destroy_all()
{
    this_unit.clear();
}

Server* ServerManager::find_by_unique_name(const string& name)
{
    Server* rval = nullptr;
    this_unit.foreach_server(
        [&rval, name](Server* server) {
            if (server->is_active && server->name() == name)
            {
                rval = server;
                return false;
            }
            return true;
        }
        );
    return rval;
}

/**
 * Return a resultset that has the current set of servers in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> ServerManager::getList()
{
    std::unique_ptr<ResultSet> set =
        ResultSet::create({"Server", "Address", "Port", "Connections", "Status"});

    this_unit.foreach_server(
        [&set](Server* server) {
            if (server->server_is_active())
            {
                string stat = server->status_string();
                set->add_row({server->name(), server->address(),
                              std::to_string(server->port()),
                              std::to_string(server->stats().n_current), stat});
            }
            return true;
        });

    return set;
}

json_t* ServerManager::server_list_to_json(const char* host)
{
    json_t* data = json_array();
    this_unit.foreach_server(
        [data, host](Server* server) {
            if (server->server_is_active())
            {
                json_array_append_new(data, server_to_json_data_relations(server, host));
            }
            return true;
        });
    return mxs_json_resource(host, MXS_JSON_API_SERVERS, data);
}

json_t* ServerManager::server_to_json_resource(const Server* server, const char* host)
{
    string self = MXS_JSON_API_SERVERS;
    self += server->name();
    return mxs_json_resource(host, self.c_str(), server_to_json_data_relations(server, host));
}

json_t* ServerManager::server_to_json_data_relations(const Server* server, const char* host)
{
    // Add monitor and service info to server json representation.
    json_t* rel = json_object();
    json_t* service_rel = service_relations_to_server(server, host);
    if (service_rel)
    {
        json_object_set_new(rel, CN_SERVICES, service_rel);
    }

    json_t* monitor_rel = MonitorManager::monitor_relations_to_server(server, host);
    if (monitor_rel)
    {
        json_object_set_new(rel, CN_MONITORS, monitor_rel);
    }

    auto json_data = server->to_json_data(host);
    json_object_set_new(json_data, CN_RELATIONSHIPS, rel);
    json_object_set_new(json_data, CN_ATTRIBUTES, server_to_json_attributes(server));
    return json_data;
}

json_t* ServerManager::server_to_json_attributes(const Server* server)
{
    json_t* attr = server->json_attributes();
    // Retrieve additional server-specific attributes from monitor.
    json_t* monitor_attr = MonitorManager::monitored_server_attributes_json(server);
    // Append the data.
    if (monitor_attr)
    {
        // Non-monitored servers will not display these.
        const char* key = nullptr;
        json_t* iter = nullptr;
        json_object_foreach(monitor_attr, key, iter)
        {
            json_object_set(attr, key, iter);
        }
        json_decref(monitor_attr);      // No longer used.
    }
    return attr;
}

SERVER* SERVER::find_by_unique_name(const string& name)
{
    return ServerManager::find_by_unique_name(name);
}

std::vector<SERVER*> SERVER::server_find_by_unique_names(const std::vector<string>& server_names)
{
    std::vector<SERVER*> rval;
    rval.reserve(server_names.size());
    for (auto elem : server_names)
    {
        rval.push_back(ServerManager::find_by_unique_name(elem));
    }
    return rval;
}

bool SERVER::is_mxs_service()
{
    bool rval = false;

    /** Do a coarse check for local server pointing to a MaxScale service */
    if (address()[0] == '/')
    {
        if (service_socket_is_used(address()))
        {
            rval = true;
        }
    }
    else if (strcmp(address(), "127.0.0.1") == 0
             || strcmp(address(), "::1") == 0
             || strcmp(address(), "localhost") == 0
             || strcmp(address(), "localhost.localdomain") == 0)
    {
        if (service_port_is_used(port()))
        {
            rval = true;
        }
    }

    return rval;
}
