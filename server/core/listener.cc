/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/listener.hh>

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#include <chrono>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

#include <maxbase/log.hh>
#include <maxscale/ssl.hh>
#include <maxscale/protocol2.hh>
#include <maxbase/alloc.h>
#include <maxscale/service.hh>
#include <maxscale/poll.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/json_api.hh>
#include <maxscale/modutil.hh>

#include "internal/listener.hh"
#include "internal/modules.hh"
#include "internal/session.hh"
#include "internal/config.hh"

using std::chrono::seconds;
using std::unique_ptr;
using std::move;
using std::string;
using ListenerSessionData = mxs::ListenerSessionData;

using SListener = std::shared_ptr<Listener>;

static std::list<SListener> all_listeners;
static std::mutex listener_lock;

constexpr int BLOCK_TIME = 60;

namespace
{

const char CN_CONNECTION_INIT_SQL_FILE[] = "connection_init_sql_file";

class RateLimit
{
public:
    /**
     * Mark authentication from a host as failed
     *
     * @param remote The host from which the connection originated
     *
     * @return True if this was the failure that caused the host to be blocked
     */
    bool mark_auth_as_failed(const std::string& remote)
    {
        bool rval = false;

        if (int limit = mxs::Config::get().max_auth_errors_until_block.get())
        {
            auto& u = m_failures[remote];
            u.last_failure = maxbase::Clock::now(maxbase::NowType::EPollTick);
            rval = ++u.failures == limit;
        }

        return rval;
    }

    bool is_blocked(const std::string& remote)
    {
        bool rval = false;

        if (int limit = mxs::Config::get().max_auth_errors_until_block.get())
        {
            auto it = m_failures.find(remote);

            if (it != m_failures.end())
            {
                auto& u = it->second;

                if (maxbase::Clock::now() - u.last_failure > seconds(BLOCK_TIME))
                {
                    u.last_failure = maxbase::Clock::now(maxbase::NowType::EPollTick);
                    u.failures = 0;
                }

                rval = u.failures >= limit;
            }
        }

        return rval;
    }

private:
    struct Failure
    {
        maxbase::TimePoint last_failure = maxbase::Clock::now(maxbase::NowType::EPollTick);
        int                failures = 0;
    };

    std::unordered_map<std::string, Failure> m_failures;
};

thread_local RateLimit rate_limit;
}

Listener::Listener(Service* service,
                   const std::string& name,
                   const std::string& address,
                   uint16_t port,
                   const std::string& protocol,
                   const mxs::ConfigParameters& params,
                   unique_ptr<ListenerSessionData> shared_data)
    : MXB_POLL_DATA{Listener::poll_handler}
    , m_name(name)
    , m_state(CREATED)
    , m_protocol(protocol)
    , m_port(port)
    , m_address(address)
    , m_service(service)
    , m_params(params)
    , m_shared_data(std::move(shared_data))
{
    if (m_address[0] == '/')
    {
        m_type = Type::UNIX_SOCKET;
    }
    else if (mxs::have_so_reuseport())
    {
        m_type = Type::UNIQUE_TCP;
    }
    else
    {
        m_type = Type::SHARED_TCP;
    }
}

Listener::~Listener()
{
    MXS_INFO("Destroying '%s'", m_name.c_str());
}

SListener Listener::create(const std::string& name,
                           const std::string& protocol,
                           const mxs::ConfigParameters& params)
{
    mxb::LogScope scope(name.c_str());
    bool port_defined = params.contains(CN_PORT);
    bool socket_defined = params.contains(CN_SOCKET);
    Service* service = static_cast<Service*>(params.get_service(CN_SERVICE));

    if (port_defined && socket_defined)
    {
        MXS_ERROR("Creation of listener '%s' failed because both 'socket' and 'port' "
                  "are defined. Only one of them is allowed.",
                  name.c_str());
        return nullptr;
    }
    else if ((!port_defined && !socket_defined) || !service)
    {
        MXS_ERROR("Listener '%s' is missing a required parameter. A Listener "
                  "must have a service, protocol and port (or socket) defined.",
                  name.c_str());
        return nullptr;
    }

    // The conditionals just enforce defaults expected in the function.
    auto port = port_defined ? params.get_integer(CN_PORT) : 0;
    auto socket = socket_defined ? params.get_string(CN_SOCKET) : "";
    auto address = socket_defined ? params.get_string(CN_SOCKET) : params.get_string(CN_ADDRESS);

    if (port == 0 && socket[0] != '/')
    {
        MXS_ERROR("Invalid path given for listener '%s' for parameter '%s': %s",
                  name.c_str(), CN_SOCKET, socket.c_str());
        return nullptr;
    }

    mxb_assert(!address.empty());

    if (socket_defined)
    {
        if (auto l = listener_find_by_socket(socket))
        {
            MXS_ERROR("Creation of listener '%s' for service '%s' failed, because "
                      "listener '%s' already listens on socket %s.",
                      name.c_str(), service->name(), l->name(), socket.c_str());
            return nullptr;
        }
    }
    else if (auto l = listener_find_by_address(address, port))
    {
        MXS_ERROR("Creation of listener '%s' for service '%s' failed, because "
                  "listener '%s' already listens on port %s.",
                  name.c_str(), service->name(), l->name(),
                  params.get_string(CN_PORT).c_str());
        return nullptr;
    }

    SListener listener;
    auto shared_data = create_shared_data(params, name);
    if (shared_data)
    {
        listener.reset(new(std::nothrow) Listener(service, name, address, port, protocol, params,
                                                  std::move(shared_data)));
    }

    if (listener)
    {
        bool user_manager_ok = true;
        auto proto_module = listener->m_shared_data->m_proto_module.get();
        if (proto_module->capabilities() & mxs::ProtocolModule::CAP_AUTHDATA)
        {
            if (!service->check_update_user_account_manager(proto_module, listener->name()))
            {
                user_manager_ok = false;
            }
        }

        if (user_manager_ok)
        {
            std::lock_guard<std::mutex> guard(listener_lock);
            all_listeners.push_back(listener);
            return listener;
        }
    }
    return nullptr;
}

void listener_destroy_instances()
{
    std::lock_guard<std::mutex> guard(listener_lock);
    all_listeners.clear();
}

void Listener::close_all_fds()
{
    if (m_type == Type::UNIQUE_TCP)
    {
        mxs::RoutingWorker::execute_concurrently(
            [this]() {
                close(*m_local_fd);
                *m_local_fd = -1;
            });
    }
    else
    {
        close(m_shared_fd);
        m_shared_fd = -1;
    }
}

void Listener::destroy(const SListener& listener)
{
    // Remove the listener from all workers. This makes sure that there's no concurrent access while we're
    // closing things up.
    listener->stop();

    listener->close_all_fds();
    listener->m_state = DESTROYED;

    std::lock_guard<std::mutex> guard(listener_lock);
    all_listeners.remove(listener);
}

// static
void Listener::stop_all()
{
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& a : all_listeners)
    {
        a->stop();
    }
}

// Helper function that executes a function on all workers and checks the result
static bool execute_and_check(const std::function<bool ()>& func)
{
    std::atomic<size_t> n_ok {0};
    auto wrapper = [func, &n_ok]() {
            if (func())
            {
                ++n_ok;
            }
        };

    size_t n_executed = mxs::RoutingWorker::execute_concurrently(wrapper);
    return n_executed == n_ok;
}

bool Listener::stop()
{
    mxb::LogScope scope(name());
    bool rval = (m_state == STOPPED);

    if (m_state == STARTED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            if (execute_and_check([this]() {
                                      mxb_assert(*m_local_fd != -1);
                                      auto worker = mxs::RoutingWorker::get_current();
                                      return worker->remove_fd(*m_local_fd);
                                  }))
            {
                m_state = STOPPED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::remove_shared_fd(m_shared_fd))
            {
                m_state = STOPPED;
                rval = true;
            }
        }
    }

    return rval;
}

bool Listener::start()
{
    mxb::LogScope scope(name());
    bool rval = (m_state == STARTED);

    if (m_state == STOPPED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            if (execute_and_check([this]() {
                                      mxb_assert(*m_local_fd != -1);
                                      auto worker = mxs::RoutingWorker::get_current();
                                      return worker->add_fd(*m_local_fd, EPOLLIN, this);
                                  }))
            {
                m_state = STARTED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::add_shared_fd(m_shared_fd, EPOLLIN, this))
            {
                m_state = STARTED;
                rval = true;
            }
        }
    }

    return rval;
}

SListener listener_find(const std::string& name)
{
    SListener rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& a : all_listeners)
    {
        if (a->name() == name)
        {
            rval = a;
            break;
        }
    }

    return rval;
}

std::vector<SListener> listener_find_by_service(const SERVICE* service)
{
    std::vector<SListener> rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& a : all_listeners)
    {
        if (a->service() == service)
        {
            rval.push_back(a);
        }
    }

    return rval;
}

static bool is_all_iface(const std::string& a, const std::string& b)
{
    std::unordered_set<std::string> addresses {"::", "0.0.0.0"};
    return addresses.count(a) || addresses.count(b);
}

SListener listener_find_by_socket(const std::string& socket)
{
    SListener rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& listener : all_listeners)
    {
        if (listener->address() == socket)
        {
            rval = listener;
            break;
        }
    }

    return rval;
}

SListener listener_find_by_address(const std::string& address, unsigned short port)
{
    SListener rval;
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& listener : all_listeners)
    {
        if (port == listener->port()
            && (listener->address() == address || is_all_iface(listener->address(), address)))
        {
            rval = listener;
            break;
        }
    }

    return rval;
}

std::ostream& Listener::persist(std::ostream& os) const
{
    os << "[" << m_name << "]\n"
       << "type=listener\n";

    for (const auto& p : m_params)
    {
        os << p.first << "=" << p.second << "\n";
    }

    return os;
}

json_t* Listener::to_json(const char* host) const
{
    const char CN_AUTHENTICATOR_DIAGNOSTICS[] = "authenticator_diagnostics";
    json_t* param = json_object();

    const MXS_MODULE* mod = get_module(m_protocol.c_str(), MODULE_PROTOCOL);
    config_add_module_params_json(&m_params,
                                  {CN_TYPE, CN_SERVICE},
                                  common_listener_params(),
                                  mod->parameters,
                                  param);

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(state()));
    json_object_set_new(attr, CN_PARAMETERS, param);

    json_t* diag = m_shared_data->m_proto_module->print_auth_users_json();
    if (diag)
    {
        json_object_set_new(attr, CN_AUTHENTICATOR_DIAGNOSTICS, diag);
    }

    json_t* rval = json_object();
    json_object_set_new(rval, CN_ID, json_string(m_name.c_str()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_LISTENERS));
    json_object_set_new(rval, CN_ATTRIBUTES, attr);

    json_t* rel = json_object();
    std::string self = std::string(MXS_JSON_API_LISTENERS) + name() + "/relationships/services/";
    json_t* service = mxs_json_relationship(host, self.c_str(), MXS_JSON_API_SERVICES);
    mxs_json_add_relation(service, m_service->name(), CN_SERVICES);
    json_object_set_new(rel, CN_SERVICES, service);
    json_object_set_new(rval, CN_RELATIONSHIPS, rel);

    return rval;
}

// static
json_t* Listener::to_json_collection(const char* host)
{
    json_t* arr = json_array();
    std::lock_guard<std::mutex> guard(listener_lock);

    for (const auto& listener : all_listeners)
    {
        json_array_append_new(arr, listener->to_json(host));
    }

    return mxs_json_resource(host, MXS_JSON_API_LISTENERS, arr);
}

json_t* Listener::to_json_resource(const char* host) const
{
    std::string self = MXS_JSON_API_LISTENERS + m_name;
    return mxs_json_resource(host, self.c_str(), to_json(host));
}

const char* Listener::name() const
{
    return m_name.c_str();
}

const char* Listener::address() const
{
    return m_address.c_str();
}

uint16_t Listener::port() const
{
    return m_port;
}

SERVICE* Listener::service() const
{
    return m_service;
}

const char* Listener::protocol() const
{
    return m_protocol.c_str();
}

const char* Listener::state() const
{
    switch (m_state)
    {
    case CREATED:
        return "Created";

    case STARTED:
        return "Running";

    case STOPPED:
        return "Stopped";

    case FAILED:
        return "Failed";

    case DESTROYED:
        return "Destroyed";

    default:
        mxb_assert(!true);
        return "Unknown";
    }
}

namespace
{

/**
 * @brief Create a Unix domain socket
 *
 * @param path The socket path
 * @return     The opened socket or -1 on error
 */
static int create_unix_socket(const char* path)
{
    if (unlink(path) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to unlink Unix Socket %s: %d %s", path, errno, mxs_strerror(errno));
    }

    struct sockaddr_un local_addr;
    int listener_socket = open_unix_socket(MXS_SOCKET_LISTENER, &local_addr, path);

    if (listener_socket >= 0 && chmod(path, 0777) < 0)
    {
        MXS_ERROR("Failed to change permissions on UNIX Domain socket '%s': %d, %s",
                  path, errno, mxs_strerror(errno));
    }

    return listener_socket;
}

/**
 * @brief Create a listener, add new information to the given DCB
 *
 * First creates and opens a socket, either TCP or Unix according to the
 * configuration data provided.  Then try to listen on the socket and
 * record the socket in the given DCB.  Add the given DCB into the poll
 * list.  The protocol name does not affect the logic, but is used in
 * log messages.
 *
 * @param config Configuration for port to listen on
 *
 * @return New socket or -1 on error
 */
int start_listening(const std::string& host, uint16_t port)
{
    mxb_assert(host[0] == '/' || port != 0);

    int listener_socket = -1;

    if (host[0] == '/')
    {
        listener_socket = create_unix_socket(host.c_str());
    }
    else if (port > 0)
    {
        struct sockaddr_storage server_address = {};
        listener_socket = open_network_socket(MXS_SOCKET_LISTENER, &server_address, host.c_str(), port);

        if (listener_socket == -1 && host == "::")
        {
            /** Attempt to bind to the IPv4 if the default IPv6 one is used */
            MXS_WARNING("Failed to bind on default IPv6 host '::', attempting "
                        "to bind on IPv4 version '0.0.0.0'");
            listener_socket = open_network_socket(MXS_SOCKET_LISTENER, &server_address, "0.0.0.0", port);
        }
    }

    if (listener_socket != -1)
    {
        /**
         * The use of INT_MAX for backlog length in listen() allows the end-user to
         * control the backlog length with the net.ipv4.tcp_max_syn_backlog kernel
         * option since the parameter is silently truncated to the configured value.
         *
         * @see man 2 listen
         */
        if (listen(listener_socket, INT_MAX) != 0)
        {
            MXS_ERROR("Failed to start listening on [%s]:%u: %d, %s", host.c_str(), port, errno,
                      mxs_strerror(errno));
            close(listener_socket);
            return -1;
        }
    }

    return listener_socket;
}

// Helper struct that contains the network information of an accepted connection
struct ClientConn
{
    int              fd;
    sockaddr_storage addr;
    char             host[INET6_ADDRSTRLEN + 1];
};

/**
 * @brief Accept a new client connection
 *
 * @param fd File descriptor to accept from
 *
 * @return ClientConn with fd set to -1 on failure
 */
static ClientConn accept_one_connection(int fd)
{
    ClientConn conn = {};
    socklen_t client_len = sizeof(conn.addr);
    conn.fd = accept(fd, (sockaddr*)&conn.addr, &client_len);

    if (conn.fd != -1)
    {
        void* ptr = nullptr;

        if (conn.addr.ss_family == AF_INET)
        {
            ptr = &((struct sockaddr_in*)&conn.addr)->sin_addr;
        }
        else if (conn.addr.ss_family == AF_INET6)
        {
            ptr = &((struct sockaddr_in6*)&conn.addr)->sin6_addr;
        }

        if (ptr)
        {
            inet_ntop(conn.addr.ss_family, ptr, conn.host, sizeof(conn.host) - 1);
        }
        else
        {
            strcpy(conn.host, "localhost");
        }

        configure_network_socket(conn.fd, conn.addr.ss_family);
    }
    else if (errno != EAGAIN && errno != EWOULDBLOCK)
    {
        MXS_ERROR("Failed to accept new client connection: %d, %s", errno, mxs_strerror(errno));
    }

    return conn;
}
}

ClientDCB* Listener::accept_one_dcb(int fd, const sockaddr_storage* addr, const char* host)
{
    auto* session = new(std::nothrow) Session(m_shared_data, host);
    if (!session)
    {
        MXS_OOM();
        close(fd);
        return NULL;
    }

    auto client_protocol = m_shared_data->m_proto_module->create_client_protocol(session, session);
    if (!client_protocol)
    {
        delete session;
        return nullptr;
    }

    mxs::RoutingWorker* worker = mxs::RoutingWorker::get_current();
    mxb_assert(worker);

    auto pProtocol = client_protocol.get();
    ClientDCB* client_dcb = ClientDCB::create(fd, host, *addr, session, std::move(client_protocol), worker);
    if (!client_dcb)
    {
        MXS_OOM();
        delete session;
    }
    else
    {
        session->set_client_dcb(client_dcb);
        session->set_client_connection(pProtocol);
        pProtocol->set_dcb(client_dcb);

        if (m_service->has_too_many_connections())
        {
            // TODO: If connections can be queued, this is the place to put the
            // TODO: connection on that queue.
            pProtocol->connlimit(m_service->config()->max_connections);

            // TODO: This is never used as the client connection is not up yet
            client_dcb->session()->close_reason = SESSION_CLOSE_TOO_MANY_CONNECTIONS;

            DCB::close(client_dcb);
            client_dcb = NULL;
        }
        else if (!client_dcb->enable_events())
        {
            MXS_ERROR("Failed to add dcb %p for fd %d to epoll set.", client_dcb, fd);
            DCB::close(client_dcb);
            client_dcb = NULL;
        }
    }

    return client_dcb;
}

bool Listener::listen_shared()
{
    bool rval = false;
    int fd = start_listening(m_address.c_str(), m_port);

    if (fd != -1)
    {
        if (mxs::RoutingWorker::add_shared_fd(fd, EPOLLIN, this))
        {
            // All workers share the same fd, assign it here
            m_shared_fd = fd;
            rval = true;
            m_state = STARTED;
        }
        else
        {
            close(fd);
        }
    }
    else
    {
        MXS_ERROR("Failed to listen on [%s]:%u", m_address.c_str(), m_port);
    }

    return rval;
}

bool Listener::listen_unique()
{
    auto open_socket = [this]() {
            bool rval = false;
            int fd = start_listening(m_address.c_str(), m_port);

            if (fd != -1)
            {
                if (mxs::RoutingWorker::get_current()->add_fd(fd, EPOLLIN, this))
                {
                    // Set the worker-local fd to the unique value
                    *m_local_fd = fd;
                    rval = true;
                }
                else
                {
                    close(fd);
                }
            }

            return rval;
        };

    bool rval = execute_and_check(open_socket);

    if (!rval)
    {
        close_all_fds();
        MXS_ERROR("One or more workers failed to listen on '[%s]:%u'.", m_address.c_str(), m_port);
    }

    return rval;
}

bool Listener::listen()
{
    mxb_assert(mxs::MainWorker::is_main_worker());

    mxb::LogScope scope(name());
    m_state = FAILED;

    // TODO: Here would could load all users (using some functionality equivalent with
    // TODO: m_proto_module->load_auth_users(m_service)), return false if there is a
    // TODO: fatal error, and prepopulate the databases of all routing workers if there is not.


    bool rval = false;
    if (m_type == Type::UNIQUE_TCP)
    {
        rval = listen_unique();
    }
    else
    {
        rval = listen_shared();
    }

    if (rval)
    {
        m_state = STARTED;
        MXS_NOTICE("Listening for connections at [%s]:%u", m_address.c_str(), m_port);
    }

    return rval;
}

uint32_t Listener::poll_handler(MXB_POLL_DATA* data, MXB_WORKER* worker, uint32_t events)
{
    Listener* listener = static_cast<Listener*>(data);
    listener->accept_connections();
    return MXB_POLL_ACCEPT;
}

void Listener::reject_connection(int fd, const char* host)
{
    if (GWBUF* buf = m_shared_data->m_proto_module->reject(host))
    {
        for (auto b = buf; b; b = b->next)
        {
            write(fd, GWBUF_DATA(b), GWBUF_LENGTH(b));
        }
        gwbuf_free(buf);
    }

    close(fd);
}

void Listener::accept_connections()
{
    mxb::LogScope scope(name());

    for (ClientConn conn = accept_one_connection(fd()); conn.fd != -1; conn = accept_one_connection(fd()))
    {
        if (rate_limit.is_blocked(conn.host))
        {
            reject_connection(conn.fd, conn.host);
        }
        else if (type() == Type::UNIQUE_TCP)
        {
            if (ClientDCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host))
            {
                if (!dcb->protocol()->init_connection())
                {
                    DCB::close(dcb);
                }
            }
        }
        else
        {
            auto worker = mxs::RoutingWorker::pick_worker();
            worker->execute([this, conn]() {
                                if (ClientDCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host))
                                {
                                    if (!dcb->protocol()->init_connection())
                                    {
                                        DCB::close(dcb);
                                    }
                                }
                            }, mxs::RoutingWorker::EXECUTE_AUTO);
        }
    }
}

/**
 * Listener creation helper function. Creates the shared data object.
 *
 * @param params Listener config params
 * @param listener_name Listener name, used for log messages
 * @return Shared data on success
 */
unique_ptr<mxs::ListenerSessionData>
Listener::create_shared_data(const mxs::ConfigParameters& params, const std::string& listener_name)
{
    auto protocol_name = params.get_string(CN_PROTOCOL);
    auto protocol_namez = protocol_name.c_str();
    auto listener_namez = listener_name.c_str();

    // If no authenticator is set, the default authenticator will be loaded.
    auto authenticator = params.get_string(CN_AUTHENTICATOR);
    auto authenticator_options = params.get_string(CN_AUTHENTICATOR_OPTIONS);

    // Add protocol and authenticator capabilities from the listener
    std::unique_ptr<mxs::ProtocolModule> protocol_module;
    auto protocol_api = (MXS_PROTOCOL_API*)load_module(protocol_namez, MODULE_PROTOCOL);
    if (protocol_api)
    {
        protocol_module.reset(protocol_api->create_protocol_module());
    }
    if (!protocol_module)
    {
        MXS_ERROR("Failed to initialize protocol module '%s' for listener '%s'.",
                  protocol_namez, listener_namez);
        return nullptr;
    }

    qc_sql_mode_t sql_mode;
    if (params.contains(CN_SQL_MODE))
    {
        std::string sql_mode_str = params.get_string(CN_SQL_MODE);
        if (strcasecmp(sql_mode_str.c_str(), "default") == 0)
        {
            sql_mode = QC_SQL_MODE_DEFAULT;
        }
        else if (strcasecmp(sql_mode_str.c_str(), "oracle") == 0)
        {
            sql_mode = QC_SQL_MODE_ORACLE;
        }
        else
        {
            MXS_ERROR("'%s' is not a valid value for '%s'. Allowed values are 'DEFAULT' and 'ORACLE'.",
                      sql_mode_str.c_str(), CN_SQL_MODE);
            return nullptr;
        }
    }
    else
    {
        // If listener doesn't configure sql_mode use the sql mode of query classifier.
        // This is the global configuration of sql_mode or "default" if it is not configured at all.
        sql_mode = qc_get_sql_mode();
    }

    mxs::SSLContext ssl;
    if (!ssl.read_configuration(listener_name, params, true))
    {
        return nullptr;
    }

    string init_sql_file = params.get_string(CN_CONNECTION_INIT_SQL_FILE);
    ListenerSessionData::ConnectionInitSql init_sql;
    if (!read_connection_init_sql(init_sql_file, &init_sql))
    {
        return nullptr;
    }

    bool auth_ok = true;
    std::vector<mxs::SAuthenticatorModule> authenticators;
    if (protocol_module->capabilities() & mxs::ProtocolModule::CAP_AUTH_MODULES)
    {
        // If the protocol uses separate authenticator modules, assume that at least one must be created.
        authenticators = protocol_module->create_authenticators(params);
        if (authenticators.empty())
        {
            auth_ok = false;
        }
    }

    if (auth_ok)
    {
        auto service = static_cast<Service*>(params.get_service(CN_SERVICE));
        return std::make_unique<ListenerSessionData>(move(ssl), sql_mode, service, move(protocol_module),
                                                     listener_name, move(authenticators), move(init_sql));
    }
    else
    {
        MXB_ERROR("Authenticator creation for listener '%s' failed.", listener_namez);
        return nullptr;
    }
}

/**
 * Read in connection init sql file.
 *
 * @param filepath Path to text file
 * @param output Output object
 * @return True on success, or if setting was not set.
 */
bool
Listener::read_connection_init_sql(const string& filepath, ListenerSessionData::ConnectionInitSql* output)
{
    bool file_ok = true;
    if (!filepath.empty())
    {
        auto& queries = output->queries;

        std::ifstream inputfile(filepath);
        if (inputfile.is_open())
        {
            string line;
            while (std::getline(inputfile, line))
            {
                if (!line.empty())
                {
                    queries.push_back(line);
                }
            }
            MXB_NOTICE("Read %zu queries from connection init file '%s'.",
                       queries.size(), filepath.c_str());
        }
        else
        {
            MXB_ERROR("Could not open connection init file '%s'.", filepath.c_str());
            file_ok = false;
        }

        if (file_ok)
        {
            // Construct a buffer with all the queries. The protocol can send the entire buffer as is.
            mxs::Buffer total_buf;
            for (const auto& query : queries)
            {
                auto querybuf = modutil_create_query(query.c_str());
                total_buf.append(querybuf);
            }
            auto total_len = total_buf.length();
            output->buffer_contents.resize(total_len);
            gwbuf_copy_data(total_buf.get(), 0, total_len, output->buffer_contents.data());
        }
    }
    return file_ok;
}

namespace maxscale
{
void mark_auth_as_failed(const std::string& remote)
{
    if (rate_limit.mark_auth_as_failed(remote))
    {
        MXS_NOTICE("Host '%s' blocked for %d seconds due to too many authentication failures.",
                   remote.c_str(), BLOCK_TIME);
    }
}

ListenerSessionData::ListenerSessionData(SSLContext ssl, qc_sql_mode_t default_sql_mode, SERVICE* service,
                                         std::unique_ptr<mxs::ProtocolModule> protocol_module,
                                         const std::string& listener_name,
                                         std::vector<SAuthenticator>&& authenticators,
                                         ListenerSessionData::ConnectionInitSql&& init_sql)
    : m_ssl(move(ssl))
    , m_default_sql_mode(default_sql_mode)
    , m_service(*service)
    , m_proto_module(move(protocol_module))
    , m_listener_name(listener_name)
    , m_authenticators(move(authenticators))
    , m_conn_init_sql(init_sql)
{
}

std::shared_ptr<mxs::ListenerSessionData>
ListenerSessionData::create_test_data(const mxs::ConfigParameters& params)
{
    auto data = Listener::create_shared_data(params, "test_listener");
    return std::shared_ptr<mxs::ListenerSessionData>(std::move(data));
}
}


const MXS_MODULE_PARAM* common_listener_params()
{
    static const MXS_MODULE_PARAM config_listener_params[] =
    {
        {CN_TYPE,          MXS_MODULE_PARAM_STRING,  CN_LISTENER, MXS_MODULE_OPT_REQUIRED },
        {CN_SERVICE,       MXS_MODULE_PARAM_SERVICE, NULL,        MXS_MODULE_OPT_REQUIRED },
        {CN_PROTOCOL,      MXS_MODULE_PARAM_STRING,  NULL,        MXS_MODULE_OPT_REQUIRED },
        // Either port or socket, checked when created
        {CN_PORT,          MXS_MODULE_PARAM_COUNT},
        {CN_SOCKET,        MXS_MODULE_PARAM_STRING},
        {
            CN_AUTHENTICATOR_OPTIONS, MXS_MODULE_PARAM_STRING, ""
        },
        {CN_ADDRESS,       MXS_MODULE_PARAM_STRING,  "::"},
        {CN_AUTHENTICATOR, MXS_MODULE_PARAM_STRING},
        {
            CN_SSL, MXS_MODULE_PARAM_ENUM, "false", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_setting_values()
        },
        {CN_SSL_CERT,      MXS_MODULE_PARAM_PATH,    NULL,        MXS_MODULE_OPT_PATH_R_OK},
        {CN_SSL_KEY,       MXS_MODULE_PARAM_PATH,    NULL,        MXS_MODULE_OPT_PATH_R_OK},
        {CN_SSL_CA_CERT,   MXS_MODULE_PARAM_PATH,    NULL,        MXS_MODULE_OPT_PATH_R_OK},
        {CN_SSL_CRL,       MXS_MODULE_PARAM_PATH,    NULL,        MXS_MODULE_OPT_PATH_R_OK},
        {
            CN_SSL_VERSION, MXS_MODULE_PARAM_ENUM, "MAX", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_version_values
        },
        {
            CN_SSL_CERT_VERIFY_DEPTH, MXS_MODULE_PARAM_COUNT, "9"
        },
        {
            CN_SSL_VERIFY_PEER_CERTIFICATE, MXS_MODULE_PARAM_BOOL, "false"
        },
        {
            CN_SSL_VERIFY_PEER_HOST, MXS_MODULE_PARAM_BOOL, "false"
        },
        {
            CN_SSL_CIPHER,
            MXS_MODULE_PARAM_STRING
        },
        {
            CN_SQL_MODE, MXS_MODULE_PARAM_STRING, NULL
        },
        {
            CN_CONNECTION_INIT_SQL_FILE, MXS_MODULE_PARAM_PATH, nullptr, MXS_MODULE_OPT_PATH_R_OK
        },
        {NULL}
    };
    return config_listener_params;
}
