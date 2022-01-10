/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
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

    Server* add_server(std::unique_ptr<Server> server)
    {
        Server* rval = nullptr;

        if (server)
        {
            auto other = ServerManager::find_by_address(server->address(), server->port());

            // The strncmp is for volatile servers. They may host/port-wise clash with
            // servers used only as bootstrap servers. But that's ok.
            if (!other || m_allow_duplicates || strncmp(server->name(), "@@", 2) == 0)
            {
                Guard guard(m_all_servers_lock);
                // This keeps the order of the servers the same as in 2.2
                rval = *m_all_servers.insert(m_all_servers.begin(), server.release());
            }
            else
            {
                MXS_ERROR("Cannot create server '%s' at '[%s]:%d', server '%s' exists there already.",
                          server->name(), other->address(), other->port(), other->name());
            }
        }

        return rval;
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

    void set_allow_duplicates(bool value)
    {
        m_allow_duplicates = value;
    }

private:
    std::mutex           m_all_servers_lock;/**< Protects access to array */
    std::vector<Server*> m_all_servers;     /**< Global list of servers, in configuration file order */
    bool                 m_allow_duplicates = false;
};

ThisUnit this_unit;
}

Server* ServerManager::create_server(const char* name, const mxs::ConfigParameters& params)
{
    mxb::LogScope scope(name);
    return this_unit.add_server(Server::create(name, params));
}

Server* ServerManager::create_server(const char* name, json_t* json)
{
    mxb::LogScope scope(name);
    return this_unit.add_server(Server::create(name, json));
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
            if (server->active() && server->name() == name)
            {
                rval = server;
                return false;
            }
            return true;
        }
        );
    return rval;
}

Server* ServerManager::find_by_address(const string& address, uint16_t port)
{
    Server* rval = nullptr;
    this_unit.foreach_server(
        [&rval, address, port](Server* server) {
            if (server->active() && server->address() == address && server->port() == port)
            {
                rval = server;
                return false;
            }
            return true;
        }
        );
    return rval;
}

json_t* ServerManager::server_list_to_json(const char* host)
{
    json_t* data = json_array();
    this_unit.foreach_server(
        [data, host](Server* server) {
            if (server->active())
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

void ServerManager::set_allow_duplicates(bool value)
{
    this_unit.set_allow_duplicates(value);
}

json_t* ServerManager::server_to_json_data_relations(const Server* server, const char* host)
{
    // Add monitor and service info to server json representation.
    json_t* rel = json_object();
    std::string self = std::string(MXS_JSON_API_SERVERS) + server->name() + "/relationships/";
    json_t* service_rel = service_relations_to_server(server, host, self + "services");
    if (service_rel)
    {
        json_object_set_new(rel, CN_SERVICES, service_rel);
    }

    json_t* monitor_rel = MonitorManager::monitor_relations_to_server(server, host, self + "monitors");
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

    // Retrieve additional server-specific attributes from monitor and combine it with the base data.
    if (auto extra = MonitorManager::monitored_server_attributes_json(server))
    {
        json_object_update(attr, extra);
        json_decref(extra);
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

bool Server::is_mxs_service() const
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
