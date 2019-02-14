/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include "internal/server.hh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <maxbase/atomic.hh>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>

#include <maxscale/config.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>
#include <maxscale/dcb.hh>
#include <maxscale/poll.hh>
#include <maxscale/ssl.hh>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>
#include <maxscale/json_api.h>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/maxscale.h>
#include <maxscale/routingworker.hh>

#include "internal/monitor.hh"
#include "internal/poll.hh"
#include "internal/config.hh"
#include "internal/service.hh"
#include "internal/modules.hh"


using maxbase::Worker;
using maxscale::RoutingWorker;

using std::string;
using Guard = std::lock_guard<std::mutex>;

const char CN_MONITORPW[] = "monitorpw";
const char CN_MONITORUSER[] = "monitoruser";
const char CN_PERSISTMAXTIME[] = "persistmaxtime";
const char CN_PERSISTPOOLMAX[] = "persistpoolmax";
const char CN_PROXY_PROTOCOL[] = "proxy_protocol";

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

private:
    std::mutex           m_all_servers_lock;    /**< Protects access to array */
    std::vector<Server*> m_all_servers;         /**< Global list of servers, in configuration file order */
};

ThisUnit this_unit;

const char ERR_TOO_LONG_CONFIG_VALUE[] = "The new value for %s is too long. Maximum length is %i characters.";

// Converts Server::ConfigParam to MXS_CONFIG_PARAM and keeps them in the same order. Required for some
// functions taking MXS_CONFIG_PARAMs as arguments.
class ParamAdaptor
{
public:
    ParamAdaptor(const std::vector<Server::ConfigParameter>& parameters)
    {
        for (const auto& elem : parameters)
        {
            // Allocate and add new head element.
            MXS_CONFIG_PARAMETER* new_elem =
                    static_cast<MXS_CONFIG_PARAMETER*>(MXS_MALLOC(sizeof(MXS_CONFIG_PARAMETER)));
            new_elem->name = MXS_STRDUP(elem.name.c_str());
            new_elem->value = MXS_STRDUP(elem.value.c_str());
            new_elem->next = m_params;
            m_params = new_elem;
        }
    }

    ~ParamAdaptor()
    {
        while (m_params)
        {
            auto elem = m_params;
            m_params = elem->next;
            MXS_FREE(elem->name);
            MXS_FREE(elem->value);
            MXS_FREE(elem);
        }
    }

    operator MXS_CONFIG_PARAMETER*()
    {
        return m_params;
    }

private:
    MXS_CONFIG_PARAMETER* m_params = nullptr;
};

/**
 * Write to char array by first zeroing any extra space. This reduces effects of concurrent reading.
 * Concurrent writing should be prevented by the caller.
 *
 * @param dest Destination buffer. The buffer is assumed to contains at least a \0 at the end.
 * @param max_len Size of destination buffer - 1. The last element (max_len) is never written to.
 * @param source Source string. A maximum of @c max_len characters are copied.
 */
void careful_strcpy(char* dest, size_t max_len, const std::string& source)
{
    // The string may be accessed while we are updating it.
    // Take some precautions to ensure that the string cannot be completely garbled at any point.
    // Strictly speaking, this is not fool-proof as writes may not appear in order to the reader.
    size_t new_len = source.length();
    if (new_len > max_len)
    {
        new_len = max_len;
    }

    size_t old_len = strlen(dest);
    if (new_len < old_len)
    {
        // If the new string is shorter, zero out the excess data.
        memset(dest + new_len, 0, old_len - new_len);
    }

    // No null-byte needs to be set. The array starts out as all zeros and the above memset adds
    // the necessary null, should the new string be shorter than the old.
    strncpy(dest, source.c_str(), new_len);
}

}

Server* Server::server_alloc(const char* name, MXS_CONFIG_PARAMETER* params)
{
    auto monuser = params->get_string(CN_MONITORUSER);
    auto monpw = params->get_string(CN_MONITORPW);

    const char one_defined_err[] = "'%s is defined for server '%s', '%s' must also be defined.";
    if (!monuser.empty() && monpw.empty())
    {
        MXS_ERROR(one_defined_err, CN_MONITORUSER, name, CN_MONITORPW);
        return NULL;
    }
    else if (monuser.empty() && !monpw.empty())
    {
        MXS_ERROR(one_defined_err, CN_MONITORPW, name, CN_MONITORUSER);
        return NULL;
    }

    auto protocol = params->get_string(CN_PROTOCOL);
    auto authenticator = params->get_string(CN_AUTHENTICATOR);

    if (authenticator.empty())
    {
        authenticator = get_default_authenticator(protocol.c_str());
        if (authenticator.empty())
        {
            MXS_ERROR("No authenticator defined for server '%s' and no default "
                      "authenticator for protocol '%s'.",
                      name, protocol.c_str());
            return NULL;
        }
    }

    void* auth_instance = NULL;
    // Backend authenticators do not have options.
    if (!authenticator_init(&auth_instance, authenticator.c_str(), NULL))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for server '%s' ",
                  authenticator.c_str(), name);
        return NULL;
    }

    SSL_LISTENER* ssl = NULL;

    if (!config_create_ssl(name, params, false, &ssl))
    {
        MXS_ERROR("Unable to initialize SSL for server '%s'", name);
        return NULL;
    }

    Server* server = new(std::nothrow) Server(name, protocol, authenticator);
    DCB** persistent = (DCB**)MXS_CALLOC(config_threadcount(), sizeof(*persistent));

    if (!server || !persistent)
    {
        delete server;
        MXS_FREE(persistent);
        SSL_LISTENER_free(ssl);
        return NULL;
    }

    auto address = params->get_string(CN_ADDRESS);
    careful_strcpy(server->address, MAX_ADDRESS_LEN, address.c_str());
    if (address.length() > MAX_ADDRESS_LEN)
    {
        MXS_WARNING("Truncated server address '%s' to the maximum size of %i characters.",
                    address.c_str(), MAX_ADDRESS_LEN);
    }

    server->port = params->get_integer(CN_PORT);
    server->extra_port = params->get_integer(CN_EXTRA_PORT);
    server->m_settings.persistpoolmax = params->get_integer(CN_PERSISTPOOLMAX);
    server->m_settings.persistmaxtime = params->get_integer(CN_PERSISTMAXTIME);
    server->proxy_protocol = params->get_bool(CN_PROXY_PROTOCOL);
    server->is_active = true;
    server->m_auth_instance = auth_instance;
    server->server_ssl = ssl;
    server->persistent = persistent;
    server->last_event = SERVER_UP_EVENT;
    server->status = SERVER_RUNNING;

    if (!monuser.empty())
    {
        mxb_assert(!monpw.empty());
        server->set_monitor_user(monuser);
        server->set_monitor_password(monpw);
    }

    for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
    {
        server->m_settings.all_parameters.push_back({p->name, p->value});
        if (server->is_custom_parameter(p->name))
        {
            server->set_parameter(p->name, p->value);
        }
    }

    // This keeps the order of the servers the same as in 2.2
    this_unit.insert_front(server);
    return server;
}

Server* Server::create_test_server()
{
    static int next_id = 1;
    string name = "TestServer" + std::to_string(next_id++);
    return new Server(name);
}

void Server::server_free(Server* server)
{
    mxb_assert(server);
    this_unit.erase(server);

    /* Clean up session and free the memory */
    if (server->persistent)
    {
        int nthr = config_threadcount();

        for (int i = 0; i < nthr; i++)
        {
            dcb_persistent_clean_count(server->persistent[i], i, true);
        }
        MXS_FREE(server->persistent);
    }

    delete server;
}

DCB* Server::get_persistent_dcb(const string& user, const string& ip, const string& protocol, int id)
{
    DCB* dcb, * previous = NULL;
    Server* server = this;
    if (server->persistent[id]
        && dcb_persistent_clean_count(server->persistent[id], id, false)
        && server->persistent[id]   // Check after cleaning
        && (server->status & SERVER_RUNNING))
    {
        mxb_assert(dcb->server);

        dcb = server->persistent[id];
        while (dcb)
        {
            if (dcb->user
                && dcb->remote
                && !ip.empty()
                && !dcb->dcb_errhandle_called
                && user == dcb->user
                && ip == dcb->remote
                && protocol == dcb->server->protocol())
            {
                if (NULL == previous)
                {
                    server->persistent[id] = dcb->nextpersistent;
                }
                else
                {
                    previous->nextpersistent = dcb->nextpersistent;
                }
                MXS_FREE(dcb->user);
                dcb->user = NULL;
                mxb::atomic::add(&server->stats.n_persistent, -1);
                mxb::atomic::add(&server->stats.n_current, 1, mxb::atomic::RELAXED);
                return dcb;
            }

            previous = dcb;
            dcb = dcb->nextpersistent;
        }
    }
    return NULL;
}

SERVER* SERVER::find_by_unique_name(const string& name)
{
    return Server::find_by_unique_name(name);
}

Server* Server::find_by_unique_name(const string& name)
{
    Server* rval = nullptr;
    this_unit.foreach_server([&rval, name](Server* server) {
        if (server->is_active && server->m_name == name)
        {
            rval = server;
            return false;
        }
        return true;
    }
    );
    return rval;
}

std::vector<SERVER*> SERVER::server_find_by_unique_names(const std::vector<string>& server_names)
{
    std::vector<SERVER*> rval;
    rval.reserve(server_names.size());
    for (auto elem : server_names)
    {
        rval.push_back(Server::find_by_unique_name(elem));
    }
    return rval;
}

void Server::printServer()
{
    printf("Server %p\n", this);
    printf("\tServer:                       %s\n", address);
    printf("\tProtocol:                     %s\n", m_settings.protocol.c_str());
    printf("\tPort:                         %d\n", port);
    printf("\tTotal connections:            %d\n", stats.n_connections);
    printf("\tCurrent connections:          %d\n", stats.n_current);
    printf("\tPersistent connections:       %d\n", stats.n_persistent);
    printf("\tPersistent actual max:        %d\n", persistmax);
}

void Server::printAllServers()
{
    this_unit.foreach_server([](Server* server) {
        if (server->server_is_active())
        {
            server->printServer();
        }
        return true;
    });
}

void Server::dprintAllServers(DCB* dcb)
{
    this_unit.foreach_server([dcb](Server* server) {
        if (server->is_active)
        {
            Server::dprintServer(dcb, server);
        }
        return true;
    });
}

void Server::dprintAllServersJson(DCB* dcb)
{
    json_t* all_servers_json = Server::server_list_to_json("");
    char* dump = json_dumps(all_servers_json, JSON_INDENT(4));
    dcb_printf(dcb, "%s", dump);
    MXS_FREE(dump);
    json_decref(all_servers_json);
}

/**
 * A class for cleaning up persistent connections
 */
class CleanupTask : public Worker::Task
{
public:
    CleanupTask(const Server* server)
        : m_server(server)
    {
    }

    void execute(Worker& worker)
    {
        RoutingWorker& rworker = static_cast<RoutingWorker&>(worker);
        mxb_assert(&rworker == RoutingWorker::get_current());

        int thread_id = rworker.id();
        dcb_persistent_clean_count(m_server->persistent[thread_id], thread_id, false);
    }

private:
    const Server* m_server;     /**< Server to clean up */
};

/**
 * @brief Clean up any stale persistent connections
 *
 * This function purges any stale persistent connections from @c server.
 *
 * @param server Server to clean up
 */
static void cleanup_persistent_connections(const Server* server)
{
    CleanupTask task(server);
    RoutingWorker::execute_concurrently(task);
}

void Server::dprintServer(DCB* dcb, const Server* srv)
{
    srv->print_to_dcb(dcb);
}

void Server::print_to_dcb(DCB* dcb) const
{
    const Server* server = this;
    if (!server->server_is_active())
    {
        return;
    }

    dcb_printf(dcb, "Server %p (%s)\n", server, server->name());
    dcb_printf(dcb, "\tServer:                              %s\n", server->address);
    string stat = status_string();
    dcb_printf(dcb, "\tStatus:                              %s\n", stat.c_str());
    dcb_printf(dcb, "\tProtocol:                            %s\n", server->m_settings.protocol.c_str());
    dcb_printf(dcb, "\tPort:                                %d\n", server->port);
    dcb_printf(dcb, "\tServer Version:                      %s\n", server->version_string().c_str());
    dcb_printf(dcb, "\tNode Id:                             %ld\n", server->node_id);
    dcb_printf(dcb, "\tMaster Id:                           %ld\n", server->master_id);
    dcb_printf(dcb,
               "\tLast event:                          %s\n",
               mon_get_event_name((mxs_monitor_event_t)server->last_event));
    time_t t = maxscale_started() + MXS_CLOCK_TO_SEC(server->triggered_at);
    dcb_printf(dcb, "\tTriggered at:                        %s\n", http_to_date(t).c_str());
    if (server->is_slave() || server->is_relay())
    {
        if (server->rlag >= 0)
        {
            dcb_printf(dcb, "\tSlave delay:                         %d\n", server->rlag);
        }
    }
    if (server->node_ts > 0)
    {
        struct tm result;
        char buf[40];
        dcb_printf(dcb,
                   "\tLast Repl Heartbeat:                 %s",
                   asctime_r(localtime_r((time_t*)(&server->node_ts), &result), buf));
    }

    if (!server->m_settings.all_parameters.empty())
    {
        dcb_printf(dcb, "\tServer Parameters:\n");
        for (const auto& elem : server->m_settings.all_parameters)
        {
            dcb_printf(dcb, "\t                                       %s\t%s\n",
                       elem.name.c_str(), elem.value.c_str());
        }
    }
    dcb_printf(dcb, "\tNumber of connections:               %d\n", server->stats.n_connections);
    dcb_printf(dcb, "\tCurrent no. of conns:                %d\n", server->stats.n_current);
    dcb_printf(dcb, "\tCurrent no. of operations:           %d\n", server->stats.n_current_ops);
    dcb_printf(dcb, "\tNumber of routed packets:            %lu\n", server->stats.packets);
    std::ostringstream ave_os;
    if (response_time_num_samples())
    {
        maxbase::Duration dur(response_time_average());
        ave_os << dur;
    }
    else
    {
        ave_os << "not available";
    }
    dcb_printf(dcb, "\tAdaptive avg. select time:           %s\n", ave_os.str().c_str());

    if (server->m_settings.persistpoolmax)
    {
        dcb_printf(dcb, "\tPersistent pool size:                %d\n", server->stats.n_persistent);
        cleanup_persistent_connections(server);
        dcb_printf(dcb, "\tPersistent measured pool size:       %d\n", server->stats.n_persistent);
        dcb_printf(dcb, "\tPersistent actual size max:          %d\n", server->persistmax);
        dcb_printf(dcb, "\tPersistent pool size limit:          %ld\n", server->m_settings.persistpoolmax);
        dcb_printf(dcb, "\tPersistent max time (secs):          %ld\n", server->m_settings.persistmaxtime);
        dcb_printf(dcb, "\tConnections taken from pool:         %lu\n", server->stats.n_from_pool);
        double d = (double)server->stats.n_from_pool / (double)(server->stats.n_connections
                                                                + server->stats.n_from_pool + 1);
        dcb_printf(dcb, "\tPool availability:                   %0.2lf%%\n", d * 100.0);
    }
    if (server->server_ssl)
    {
        SSL_LISTENER* l = server->server_ssl;
        dcb_printf(dcb,
                   "\tSSL initialized:                     %s\n",
                   l->ssl_init_done ? "yes" : "no");
        dcb_printf(dcb,
                   "\tSSL method type:                     %s\n",
                   ssl_method_type_to_string(l->ssl_method_type));
        dcb_printf(dcb, "\tSSL certificate verification depth:  %d\n", l->ssl_cert_verify_depth);
        dcb_printf(dcb, "\tSSL peer verification :  %s\n", l->ssl_verify_peer_certificate ? "true" : "false");
        dcb_printf(dcb,
                   "\tSSL certificate:                     %s\n",
                   l->ssl_cert ? l->ssl_cert : "null");
        dcb_printf(dcb,
                   "\tSSL key:                             %s\n",
                   l->ssl_key ? l->ssl_key : "null");
        dcb_printf(dcb,
                   "\tSSL CA certificate:                  %s\n",
                   l->ssl_ca_cert ? l->ssl_ca_cert : "null");
    }
    if (server->proxy_protocol)
    {
        dcb_printf(dcb, "\tPROXY protocol:                      on.\n");
    }
}

void Server::dprintPersistentDCBs(DCB* pdcb, const Server* server)
{
    dcb_printf(pdcb, "Number of persistent DCBs: %d\n", server->stats.n_persistent);
}

void Server::dListServers(DCB* dcb)
{
    const string horizontalLine =
    "-------------------+-----------------+-------+-------------+--------------------\n";
    string message;
    // Estimate the likely size of the string. Should be enough for 5 servers.
    message.reserve((4 + 5) * horizontalLine.length());
    message += "Servers.\n" + horizontalLine;
    message += mxb::string_printf("%-18s | %-15s | Port  | Connections | %-20s\n",
                                  "Server", "Address", "Status");
    message += horizontalLine;

    bool have_servers = false;
    this_unit.foreach_server([&message, &have_servers](Server* server) {
        if (server->server_is_active())
        {
            have_servers = true;
            string stat = server->status_string();
            message += mxb::string_printf("%-18s | %-15s | %5d | %11d | %s\n",
                                          server->name(), server->address, server->port,
                                          server->stats.n_current, stat.c_str());
        }
        return true;
    });

    if (have_servers)
    {
        message += horizontalLine;
        dcb_printf(dcb, "%s", message.c_str());
    }
}

string SERVER::status_to_string(uint64_t flags)
{
    string result;
    string separator;

    // Helper function.
    auto concatenate_if = [&result, &separator](bool condition, const string& desc) {
            if (condition)
            {
                result += separator + desc;
                separator = ", ";
            }
        };

    // TODO: The following values should be revisited at some point, but since they are printed by
    // the REST API they should not be changed suddenly. Strictly speaking, even the combinations
    // should not change, but this is more dependant on the monitors and have already changed.
    // Also, system tests compare to these strings so the output must stay constant for now.
    const string maintenance = "Maintenance";
    const string being_drained = "Being Drained";
    const string master = "Master";
    const string relay = "Relay Master";
    const string slave = "Slave";
    const string synced = "Synced";
    const string ndb = "NDB";
    const string slave_ext = "Slave of External Server";
    const string sticky = "Master Stickiness";
    const string auth_err = "Auth Error";
    const string running = "Running";
    const string down = "Down";

    // Maintenance/Being Drained is usually set by user so is printed first.
    // Being Drained in the presence of Maintenance has no effect, so we only
    // print either one of those, with Maintenance taking precedence.
    if (status_is_in_maint(flags))
    {
        concatenate_if(true, maintenance);
    }
    else if (status_is_being_drained(flags))
    {
        concatenate_if(true, being_drained);
    }

    // Master cannot be a relay or a slave.
    if (status_is_master(flags))
    {
        concatenate_if(true, master);
    }
    else
    {
        // Relays are typically slaves as well. The binlog server may be an exception.
        concatenate_if(status_is_relay(flags), relay);
        concatenate_if(status_is_slave(flags), slave);
    }

    // The following Galera and Cluster bits may be combined with master/slave.
    concatenate_if(status_is_joined(flags), synced);
    concatenate_if(status_is_ndb(flags), ndb);
    // May be combined with other MariaDB monitor flags.
    concatenate_if(flags & SERVER_SLAVE_OF_EXT_MASTER, slave_ext);

    // Should this be printed only if server is master?
    concatenate_if(flags & SERVER_MASTER_STICKINESS, sticky);

    concatenate_if(flags & SERVER_AUTH_ERROR, auth_err);
    concatenate_if(status_is_running(flags), running);
    concatenate_if(status_is_down(flags), down);

    return result;
}

string SERVER::status_string() const
{
    return status_to_string(status);
}

void SERVER::set_status(uint64_t bit)
{
    status |= bit;

    /** clear error logged flag before the next failure */
    if (is_master())
    {
        master_err_is_logged = false;
    }
}

void SERVER::clear_status(uint64_t bit)
{
    status &= ~bit;
}

bool Server::set_monitor_user(const string& username)
{
    bool rval = false;
    if (username.length() <= MAX_MONUSER_LEN)
    {
        careful_strcpy(m_settings.monuser, MAX_MONUSER_LEN, username);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORUSER, MAX_MONUSER_LEN);
    }
    return rval;
}

bool Server::set_monitor_password(const string& password)
{
    bool rval = false;
    if (password.length() <= MAX_MONPW_LEN)
    {
        careful_strcpy(m_settings.monpw, MAX_MONPW_LEN, password);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORPW, MAX_MONPW_LEN);
    }
    return rval;
}

string Server::monitor_user() const
{
    return m_settings.monuser;
}

string Server::monitor_password() const
{
    return m_settings.monpw;
}

void Server::set_parameter(const string& name, const string& value)
{
    // Set/add the parameter in both containers
    bool found = false;
    for (auto& elem : m_settings.all_parameters)
    {
        if (name == elem.name)
        {
            found = true;
            elem.value = value; // Update
            break;
        }
    }
    if (!found)
    {
        m_settings.all_parameters.push_back({name, value});
    }
    std::lock_guard<std::mutex> guard(m_settings.lock);
    m_settings.custom_parameters[name] = value;
}

string Server::get_custom_parameter(const string& name) const
{
    string rval;
    std::lock_guard<std::mutex> guard(m_settings.lock);
    auto iter = m_settings.custom_parameters.find(name);
    if (iter != m_settings.custom_parameters.end())
    {
        rval = iter->second;
    }
    return rval;
}

/**
 * Return a resultset that has the current set of servers in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> Server::getList()
{
    std::unique_ptr<ResultSet> set =
            ResultSet::create({"Server", "Address", "Port", "Connections", "Status"});

    this_unit.foreach_server([&set](Server* server) {
        if (server->server_is_active())
        {
            string stat = server->status_string();
            set->add_row({server->name(), server->address, std::to_string(server->port),
                          std::to_string(server->stats.n_current), stat});
        }
        return true;
    });

    return set;
}

bool SERVER::server_update_address(const string& new_address)
{
    bool rval = false;
    if (new_address.length() <= MAX_ADDRESS_LEN)
    {
        careful_strcpy(address, MAX_ADDRESS_LEN, new_address);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_ADDRESS, MAX_ADDRESS_LEN);
    }
    return rval;
}

void SERVER::update_port(int new_port)
{
    mxb::atomic::store(&port, new_port, mxb::atomic::RELAXED);
}

void SERVER::update_extra_port(int new_port)
{
    mxb::atomic::store(&extra_port, new_port, mxb::atomic::RELAXED);
}

uint64_t SERVER::status_from_string(const char* str)
{
    static struct
    {
        const char* str;
        uint64_t    bit;
    } ServerBits[] =
    {
        {"running",     SERVER_RUNNING      },
        {"master",      SERVER_MASTER       },
        {"slave",       SERVER_SLAVE        },
        {"synced",      SERVER_JOINED       },
        {"ndb",         SERVER_NDB          },
        {"maintenance", SERVER_MAINT        },
        {"maint",       SERVER_MAINT        },
        {"stale",       SERVER_WAS_MASTER   },
        {"drain",       SERVER_BEING_DRAINED},
        {NULL,          0                   }
    };

    for (int i = 0; ServerBits[i].str; i++)
    {
        if (!strcasecmp(str, ServerBits[i].str))
        {
            return ServerBits[i].bit;
        }
    }
    return 0;
}

void Server::set_version(uint64_t version_num, const std::string& version_str)
{
    m_info.set(version_num, version_str);
}

/**
 * Creates a server configuration at the location pointed by @c filename
 * TODO: Move to member
 *
 * @param server Server to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
bool Server::create_server_config(const Server* server, const char* filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing server '%s': %d, %s",
                  filename,
                  server->name(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", server->name());
    dprintf(file, "%s=server\n", CN_TYPE);

    const MXS_MODULE* mod = get_module(server->m_settings.protocol.c_str(), MODULE_PROTOCOL);
    dump_param_list(file,
                    ParamAdaptor(server->m_settings.all_parameters),
                    {CN_TYPE},
                    config_server_params,
                    mod->parameters);

    // Print custom parameters
    for (const auto& elem : server->m_settings.custom_parameters)
    {
        dprintf(file, "%s=%s\n", elem.first.c_str(), elem.second.c_str());
    }

    close(file);
    return true;
}

bool Server::serialize() const
{
    const Server* server = this;
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             server->name());

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary server configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (create_server_config(server, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char* dot = strrchr(final_filename, '.');
        mxb_assert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

bool mxs::server_set_status(SERVER* srv, int bit, string* errmsg_out)
{
    bool written = false;
    /* First check if the server is monitored. This isn't done under a lock
     * but the race condition cannot cause significant harm. Monitors are never
     * freed so the pointer stays valid. */
    Monitor* mon = monitor_server_in_use(srv);
    if (mon)
    {
        written = mon->set_server_status(srv, bit, errmsg_out);
    }
    else
    {
        /* Set the bit directly */
        srv->set_status(bit);
        written = true;
    }

    return written;
}

bool mxs::server_clear_status(SERVER* srv, int bit, string* errmsg_out)
{
    // See server_set_status().
    bool written = false;
    Monitor* mon = monitor_server_in_use(srv);
    if (mon)
    {
        written = mon->clear_server_status(srv, bit, errmsg_out);
    }
    else
    {
        /* Clear bit directly */
        srv->clear_status(bit);
        written = true;
    }

    return written;
}

bool SERVER::is_mxs_service()
{
    bool rval = false;

    /** Do a coarse check for local server pointing to a MaxScale service */
    if (strcmp(address, "127.0.0.1") == 0
        || strcmp(address, "::1") == 0
        || strcmp(address, "localhost") == 0
        || strcmp(address, "localhost.localdomain") == 0)
    {
        if (service_port_is_used(port))
        {
            rval = true;
        }
    }

    return rval;
}

// TODO: member function
json_t* Server::server_json_attributes(const Server* server)
{
    /** Resource attributes */
    json_t* attr = json_object();

    /** Store server parameters in attributes */
    json_t* params = json_object();

    const MXS_MODULE* mod = get_module(server->m_settings.protocol.c_str(), MODULE_PROTOCOL);
    config_add_module_params_json(ParamAdaptor(server->m_settings.all_parameters),
                                  {CN_TYPE},
                                  config_server_params,
                                  mod->parameters,
                                  params);

    // Add weighting parameters that weren't added by config_add_module_params_json
    for (const auto& elem : server->m_settings.custom_parameters)
    {
        if (!json_object_get(params, elem.first.c_str()))
        {
            json_object_set_new(params, elem.first.c_str(), json_string(elem.second.c_str()));
        }
    }

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    string stat = server->status_string();
    json_object_set_new(attr, CN_STATE, json_string(stat.c_str()));

    json_object_set_new(attr, CN_VERSION_STRING, json_string(server->version_string().c_str()));

    json_object_set_new(attr, "node_id", json_integer(server->node_id));
    json_object_set_new(attr, "master_id", json_integer(server->master_id));

    const char* event_name = mon_get_event_name((mxs_monitor_event_t)server->last_event);
    time_t t = maxscale_started() + MXS_CLOCK_TO_SEC(server->triggered_at);
    json_object_set_new(attr, "last_event", json_string(event_name));
    json_object_set_new(attr, "triggered_at", json_string(http_to_date(t).c_str()));

    if (server->rlag >= 0)
    {
        json_object_set_new(attr, "replication_lag", json_integer(server->rlag));
    }

    if (server->node_ts > 0)
    {
        struct tm result;
        char timebuf[30];
        time_t tim = server->node_ts;
        asctime_r(localtime_r(&tim, &result), timebuf);
        mxb::trim(timebuf);

        json_object_set_new(attr, "last_heartbeat", json_string(timebuf));
    }

    /** Store statistics */
    json_t* stats = json_object();

    json_object_set_new(stats, "connections", json_integer(server->stats.n_current));
    json_object_set_new(stats, "total_connections", json_integer(server->stats.n_connections));
    json_object_set_new(stats, "persistent_connections", json_integer(server->stats.n_persistent));
    json_object_set_new(stats, "active_operations", json_integer(server->stats.n_current_ops));
    json_object_set_new(stats, "routed_packets", json_integer(server->stats.packets));

    maxbase::Duration response_ave(server->response_time_average());
    json_object_set_new(stats, "adaptive_avg_select_time", json_string(to_string(response_ave).c_str()));

    json_object_set_new(attr, "statistics", stats);

    return attr;
}

static json_t* server_to_json_data(const Server* server, const char* host)
{
    json_t* rval = json_object();

    /** Add resource identifiers */
    json_object_set_new(rval, CN_ID, json_string(server->name()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVERS));

    /** Relationships */
    json_t* rel = json_object();
    json_t* service_rel = service_relations_to_server(server, host);
    json_t* monitor_rel = monitor_relations_to_server(server, host);

    if (service_rel)
    {
        json_object_set_new(rel, CN_SERVICES, service_rel);
    }

    if (monitor_rel)
    {
        json_object_set_new(rel, CN_MONITORS, monitor_rel);
    }

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    /** Attributes */
    json_object_set_new(rval, CN_ATTRIBUTES, Server::server_json_attributes(server));
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVERS, server->name()));

    return rval;
}

json_t* Server::to_json(const char* host)
{
    string self = MXS_JSON_API_SERVERS;
    self += name();
    return mxs_json_resource(host, self.c_str(), server_to_json_data(this, host));
}

json_t* Server::server_list_to_json(const char* host)
{
    json_t* data = json_array();
    this_unit.foreach_server([data, host](Server* server) {
        if (server->server_is_active())
        {
            json_array_append_new(data, server_to_json_data(server, host));
        }
        return true;
    });
    return mxs_json_resource(host, MXS_JSON_API_SERVERS, data);
}

bool Server::set_disk_space_threshold(const string& disk_space_threshold)
{
    DiskSpaceLimits dst;
    bool rv = config_parse_disk_space_threshold(&dst, disk_space_threshold.c_str());
    if (rv)
    {
        set_disk_space_limits(dst);
    }
    return rv;
}

void SERVER::response_time_add(double ave, int num_samples)
{
    /**
     * Apply backend average and adjust sample_max, which determines the weight of a new average
     * applied to EMAverage.
     *
     * Sample max is raised if the server is fast, aggressively lowered if the incoming average is clearly
     * lower than the EMA, else just lowered a bit. The normal increase and decrease, drifting, of the max
     * is done to follow the speed of a server. The important part is the lowering of max, to allow for a
     * server that is speeding up to be adjusted and used.
     *
     * Three new magic numbers to replace the sample max magic number... */
    constexpr double drift {1.1};
    Guard guard(m_average_write_mutex);
    int current_max = m_response_time.sample_max();
    int new_max {0};

    // This server handles more samples than EMA max.
    // Increasing max allows all servers to be fairly compared.
    if (num_samples >= current_max)
    {
        new_max = num_samples * drift;
    }
    // This server is experiencing high load of some kind,
    // lower max to give more weight to the samples.
    else if (m_response_time.average() / ave > 2)
    {
        new_max = current_max * 0.5;
    }
    // Let the max slowly trickle down to keep
    // the sample max close to reality.
    else
    {
        new_max = current_max / drift;
    }

    m_response_time.set_sample_max(new_max);
    m_response_time.add(ave, num_samples);
}

bool Server::is_custom_parameter(const string& name) const
{
    for (int i = 0; config_server_params[i].name; i++)
    {
        if (name == config_server_params[i].name)
        {
            return false;
        }
    }
    auto module_params = get_module(m_settings.protocol.c_str(), MODULE_PROTOCOL)->parameters;
    for (int i = 0; module_params[i].name; i++)
    {
        if (name == module_params[i].name)
        {
            return false;
        }
    }
    return true;
}

void Server::VersionInfo::set(uint64_t version, const std::string& version_str)
{
    /* This only protects against concurrent writing which could result in garbled values. Reads are not
     * synchronized. Since writing is rare, this is an unlikely issue. Readers should be prepared to
     * sometimes get inconsistent values. */
    Guard lock(m_lock);

    mxb::atomic::store(&m_version_num.total, version, mxb::atomic::RELAXED);
    uint32_t major = version / 10000;
    uint32_t minor = (version - major * 10000) / 100;
    uint32_t patch = version - major * 10000 - minor * 100;
    m_version_num.major = major;
    m_version_num.minor = minor;
    m_version_num.patch = patch;

    careful_strcpy(m_version_str, MAX_VERSION_LEN, version_str);
    if (strcasestr(version_str.c_str(), "clustrix") != NULL)
    {
        m_type = Type::CLUSTRIX;
    }
    else if (strcasestr(version_str.c_str(), "mariadb") != NULL)
    {
        m_type = Type::MARIADB;
    }
    else
    {
        m_type = Type::MYSQL;
    }
}

Server::Version Server::VersionInfo::version_num() const
{
    return m_version_num;
}

Server::Type Server::VersionInfo::type() const
{
    return m_type;
}

std::string Server::VersionInfo::version_string() const
{
    return m_version_str;
}
