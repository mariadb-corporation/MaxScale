/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
                auto server_ptr = server.release();
                Guard guard(m_all_servers_lock);
                m_all_servers.push_back(server_ptr);
                rval = server_ptr;
            }
            else
            {
                MXB_ERROR("Cannot create server '%s' at '[%s]:%d', server '%s' exists there already.",
                          server->name(), other->address(), other->port(), other->name());
            }
        }

        return rval;
    }

    Server* add_volatile_server(std::unique_ptr<Server> server)
    {
        // Only check that server name does not clash, volatile servers can have same host-ports.
        // Callers should check for name clashes before getting here so a clash is only possible if
        // someone managed to create the same server just now.
        Server* rval = nullptr;
        bool conflict = false;

        {
            Guard guard(m_all_servers_lock);
            for (auto* srv : m_all_servers)
            {
                if (srv->active() && strcmp(srv->name(), server->name()) == 0)
                {
                    conflict = true;
                    break;
                }
            }

            if (!conflict)
            {
                auto server_ptr = server.release();
                m_all_servers.push_back(server_ptr);
                rval = server_ptr;
            }
        }

        if (conflict)
        {
            MXB_ERROR("Cannot create volatile server '%s' at '[%s]:%d', server '%s' exists already.",
                      server->name(), server->address(), server->port(), server->name());
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

Server* ServerManager::create_volatile_server(const string& name, const mxs::ConfigParameters& params)
{
    mxb::LogScope scope(name.c_str());
    return this_unit.add_volatile_server(Server::create(name.c_str(), params));
}

void ServerManager::server_free(Server* server)
{
    mxb_assert(server);
    this_unit.erase(server);

    auto pool_close_per_thread = [server] () {
        mxs::RoutingWorker::get_current()->pool_close_all_conns_by_server(server);
    };
    mxs::RoutingWorker::execute_concurrently(pool_close_per_thread);
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
    return json_data;
}

// static
bool ServerManager::reload_tls()
{
    bool ok = true;

    this_unit.foreach_server(
        [&](Server* server) {
            mxb::LogScope scope(server->name());
            mxb::Json js(server->json_parameters(), mxb::Json::RefType::STEAL);
            js.remove_nulls();

            auto& config = server->configuration();

            if (!config.validate(js.get_json()) || !config.configure(js.get_json()))
            {
                MXB_ERROR("Failed to reload TLS certificates for '%s'", server->name());
                ok = false;
            }

            return ok;
        });

    return ok;
}

