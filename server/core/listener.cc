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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_set>

#include <maxscale/maxadmin.h>
#include <maxscale/paths.h>
#include <maxscale/ssl.hh>
#include <maxscale/protocol.hh>
#include <maxbase/alloc.h>
#include <maxscale/users.h>
#include <maxscale/service.hh>
#include <maxscale/poll.hh>
#include <maxscale/routingworker.hh>

#include "internal/modules.hh"
#include "internal/session.hh"
#include "internal/config.hh"

using Clock = std::chrono::steady_clock;
using std::chrono::seconds;

static std::list<SListener> all_listeners;
static std::mutex listener_lock;

constexpr int BLOCK_TIME = 60;

namespace
{
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

        if (int limit = config_get_global_options()->max_auth_errors_until_block)
        {
            auto& u = m_failures[remote];
            u.last_failure = Clock::now();
            rval = ++u.failures == limit;
        }

        return rval;
    }

    bool is_blocked(const std::string& remote)
    {
        bool rval = false;

        if (int limit = config_get_global_options()->max_auth_errors_until_block)
        {
            auto it = m_failures.find(remote);

            if (it != m_failures.end())
            {
                auto& u = it->second;

                if (Clock::now() - u.last_failure > seconds(BLOCK_TIME))
                {
                    u.last_failure = Clock::now();
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
        Clock::time_point last_failure = Clock::now();
        int               failures = 0;
    };

    std::unordered_map<std::string, Failure> m_failures;
};

thread_local RateLimit rate_limit;
}

Listener::Listener(SERVICE* service,
                   const std::string& name,
                   const std::string& address,
                   uint16_t port,
                   const std::string& protocol,
                   const std::string& authenticator,
                   const std::string& auth_opts,
                   void* auth_instance,
                   std::unique_ptr<mxs::SSLContext> ssl,
                   const MXS_CONFIG_PARAMETER& params)
    : MXB_POLL_DATA{Listener::poll_handler}
    , m_name(name)
    , m_state(CREATED)
    , m_protocol(protocol)
    , m_port(port)
    , m_address(address)
    , m_authenticator(authenticator)
    , m_auth_options(auth_opts)
    , m_auth_instance(auth_instance)
    , m_users(nullptr)
    , m_service(service)
    , m_proto_func(*(MXS_PROTOCOL*)load_module(protocol.c_str(), MODULE_PROTOCOL))
    , m_auth_func(*(MXS_AUTHENTICATOR*)load_module(authenticator.c_str(), MODULE_AUTHENTICATOR))
    , m_params(params)
    , m_ssl_provider(std::move(ssl))
{
    if (strcasecmp(service->router_name(), "cli") == 0 || strcasecmp(service->router_name(), "maxinfo") == 0)
    {
        m_type = Type::MAIN_WORKER;
    }
    else if (m_address[0] == '/')
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
    if (m_users)
    {
        users_free(m_users);
    }
}

SListener Listener::create(const std::string& name,
                           const std::string& protocol,
                           const MXS_CONFIG_PARAMETER& params)
{
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

    // Remove this once maxadmin is removed
    if (strcasecmp(protocol.c_str(), "maxscaled") == 0 && socket_defined
        && socket == MAXADMIN_CONFIG_DEFAULT_SOCKET_TAG)
    {
        socket_defined = true;
        address = MAXADMIN_DEFAULT_SOCKET;
        socket = address;
    }
    else if (port == 0 && socket[0] != '/')
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

    std::unique_ptr<mxs::SSLContext> ssl_info;

    if (!config_create_ssl(name.c_str(), params, true, &ssl_info))
    {
        return nullptr;
    }

    // These two values being NULL trigger the loading of the default
    // authenticators that are specific to each protocol module
    auto authenticator = params.get_string(CN_AUTHENTICATOR);
    auto authenticator_options = params.get_string(CN_AUTHENTICATOR_OPTIONS);
    int net_port = socket_defined ? 0 : port;


    const char* auth = !authenticator.empty() ? authenticator.c_str() :
        get_default_authenticator(protocol.c_str());

    if (!auth)
    {
        MXS_ERROR("No authenticator defined for listener '%s' and could not get "
                  "default authenticator for protocol '%s'.", name.c_str(), protocol.c_str());
        return nullptr;
    }

    void* auth_instance = NULL;

    if (!authenticator_init(&auth_instance, auth, authenticator_options.c_str()))
    {
        MXS_ERROR("Failed to initialize authenticator module '%s' for listener '%s'.",
                  auth, name.c_str());
        return nullptr;
    }

    // Add protocol and authenticator capabilities from the listener
    const MXS_MODULE* proto_mod = get_module(protocol.c_str(), MODULE_PROTOCOL);
    const MXS_MODULE* auth_mod = get_module(auth, MODULE_AUTHENTICATOR);
    mxb_assert(proto_mod && auth_mod);

    SListener listener(new(std::nothrow) Listener(service, name, address, port, protocol, auth,
                                                  authenticator_options, auth_instance,
                                                  std::move(ssl_info), params));

    if (listener)
    {
        // Storing a self-reference to the listener makes it possible to easily
        // increment the reference count when new connections are accepted.
        listener->m_self = listener;

        // Note: This isn't good: we modify the service from a listener and the service itself should do this.
        service->capabilities |= proto_mod->module_capabilities | auth_mod->module_capabilities;

        std::lock_guard<std::mutex> guard(listener_lock);
        all_listeners.push_back(listener);
    }

    return listener;
}

void Listener::close_all_fds()
{
    // Shared fds all have the same value. Unique fds each have a unique value. By sorting the values,
    // removing duplicates and skipping negative values, both cases work and use the same code.
    auto values = m_fd.values();
    std::sort(values.begin(), values.end());
    auto end = std::unique(values.begin(), values.end());
    auto start = std::upper_bound(values.begin(), end, -1);
    std::for_each(start, end, close);

    // Make sure we don't accidentally use a closed fd
    m_fd.assign(-1);
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
    bool rval = (m_state == STOPPED);

    if (m_state == STARTED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            if (execute_and_check([this]() {
                                      mxb_assert(*m_fd != -1);
                                      return mxs::RoutingWorker::get_current()->remove_fd(*m_fd);
                                  }))
            {
                m_state = STOPPED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::remove_shared_fd(m_fd))
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
    bool rval = (m_state == STARTED);

    if (m_state == STOPPED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            if (execute_and_check([this]() {
                                      mxb_assert(*m_fd != -1);
                                      return mxs::RoutingWorker::get_current()->add_fd(*m_fd, EPOLLIN, this);
                                  }))
            {
                m_state = STARTED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::add_shared_fd(*m_fd, EPOLLIN, this))
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

/**
 * Creates a listener configuration at the location pointed by @c filename
 *
 * @param listener Listener to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
bool Listener::create_listener_config(const char* filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing listener '%s': %d, %s",
                  filename,
                  m_name.c_str(),
                  errno,
                  mxs_strerror(errno));
        return false;
    }

    // TODO: Check for return values on all of the dprintf calls
    dprintf(file, "[%s]\n", m_name.c_str());
    dprintf(file, "type=listener\n");

    for (const auto& p : m_params)
    {
        dprintf(file, "%s=%s\n", p.first.c_str(), p.second.c_str());
    }

    ::close(file);

    return true;
}

bool listener_serialize(const SListener& listener)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename,
             sizeof(filename),
             "%s/%s.cnf.tmp",
             get_config_persistdir(),
             listener->name());

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary listener configuration at '%s': %d, %s",
                  filename,
                  errno,
                  mxs_strerror(errno));
    }
    else if (listener->create_listener_config(filename))
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
            MXS_ERROR("Failed to rename temporary listener configuration at '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
    }

    return rval;
}

json_t* Listener::to_json() const
{
    json_t* param = json_object();

    const MXS_MODULE* mod = get_module(m_protocol.c_str(), MODULE_PROTOCOL);
    config_add_module_params_json(&m_params,
                                  {CN_TYPE, CN_SERVICE},
                                  config_listener_params,
                                  mod->parameters,
                                  param);

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(state()));
    json_object_set_new(attr, CN_PARAMETERS, param);

    if (m_auth_func.diagnostic_json)
    {
        json_t* diag = m_auth_func.diagnostic_json(this);

        if (diag)
        {
            json_object_set_new(attr, CN_AUTHENTICATOR_DIAGNOSTICS, diag);
        }
    }

    json_t* rval = json_object();
    json_object_set_new(rval, CN_ID, json_string(m_name.c_str()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_LISTENERS));
    json_object_set_new(rval, CN_ATTRIBUTES, attr);

    return rval;
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

const char* Listener::authenticator() const
{
    return m_authenticator.c_str();
}

const char* Listener::protocol() const
{
    return m_protocol.c_str();
}

const MXS_PROTOCOL& Listener::protocol_func() const
{
    return m_proto_func;
}

const MXS_AUTHENTICATOR& Listener::auth_func() const
{
    return m_auth_func;
}

void* Listener::auth_instance() const
{
    return m_auth_instance;
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

void Listener::print_users(DCB* dcb)
{
    if (m_auth_func.diagnostic)
    {
        dcb_printf(dcb, "User names (%s): ", name());

        m_auth_func.diagnostic(dcb, this);

        dcb_printf(dcb, "\n");
    }
}

int Listener::load_users()
{
    int rval = MXS_AUTH_LOADUSERS_OK;

    if (m_auth_func.loadusers)
    {
        rval = m_auth_func.loadusers(this);
    }

    return rval;
}

struct users* Listener::users() const
{
    return m_users;
}


void Listener::set_users(struct users* u)
{
    m_users = u;
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

DCB* Listener::accept_one_dcb(int fd, const sockaddr_storage* addr, const char* host)
{
    mxs::Session* session = new(std::nothrow) mxs::Session(m_self);

    if (!session)
    {
        MXS_OOM();
        close(fd);
        return NULL;
    }

    DCB* client_dcb = dcb_alloc(DCB::Role::CLIENT, session);

    if (!client_dcb)
    {
        MXS_OOM();
        close(fd);
        delete session;
    }
    else
    {
        session->set_client_dcb(client_dcb);
        memcpy(&client_dcb->ip, addr, sizeof(*addr));
        client_dcb->fd = fd;
        client_dcb->remote = MXS_STRDUP_A(host);

        /** Allocate DCB specific authentication data */
        if (m_auth_func.create
            && (client_dcb->authenticator_data = m_auth_func.create(m_auth_instance)) == NULL)
        {
            MXS_ERROR("Failed to create authenticator for client DCB");
            dcb_close(client_dcb);
            return NULL;
        }

        if (m_service->max_connections && m_service->client_count > m_service->max_connections)
        {
            // TODO: If connections can be queued, this is the place to put the
            // TODO: connection on that queue.
            if (m_proto_func.connlimit)
            {
                m_proto_func.connlimit(client_dcb, m_service->max_connections);
            }

            // TODO: This is never used as the client connection is not up yet
            client_dcb->session->close_reason = SESSION_CLOSE_TOO_MANY_CONNECTIONS;

            dcb_close(client_dcb);
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
            m_fd.assign(fd);
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
        MXS_ERROR("[%s] Failed to listen on [%s]:%u", m_service->name(), m_address.c_str(), m_port);
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
                    *m_fd = fd;
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
        MXS_ERROR("[%s] One or more workers failed to listen on '[%s]:%u'.", m_service->name(),
                  m_address.c_str(), m_port);
    }

    return rval;
}

bool Listener::listen()
{
    m_state = FAILED;

    /** Load the authentication users before before starting the listener */
    if (m_auth_func.loadusers)
    {
        switch (m_auth_func.loadusers(this))
        {
        case MXS_AUTH_LOADUSERS_FATAL:
            MXS_ERROR("[%s] Fatal error when loading users for listener '%s', "
                      "service is not started.", m_service->name(), name());
            return false;

        case MXS_AUTH_LOADUSERS_ERROR:
            MXS_WARNING("[%s] Failed to load users for listener '%s', authentication"
                        " might not work.", m_service->name(), name());
            break;

        default:
            break;
        }
    }

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
    if (m_proto_func.reject)
    {
        if (GWBUF* buf = m_proto_func.reject(host))
        {
            for (auto b = buf; b; b = b->next)
            {
                write(fd, GWBUF_DATA(b), GWBUF_LENGTH(b));
            }
            gwbuf_free(buf);
        }
    }

    close(fd);
}

void Listener::accept_connections()
{
    for (ClientConn conn = accept_one_connection(fd()); conn.fd != -1; conn = accept_one_connection(fd()))
    {

        if (rate_limit.is_blocked(conn.host))
        {
            reject_connection(conn.fd, conn.host);
        }
        else if (type() == Type::UNIQUE_TCP)
        {
            if (DCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host))
            {
                m_proto_func.accept(dcb);
            }
        }
        else
        {
            auto worker = type() == Type::MAIN_WORKER ?
                mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN) :
                mxs::RoutingWorker::pick_worker();

            worker->execute([this, conn]() {
                                if (DCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host))
                                {
                                    m_proto_func.accept(dcb);
                                }
                            }, mxs::RoutingWorker::EXECUTE_AUTO);
        }
    }
}

void Listener::mark_auth_as_failed(const std::string& remote)
{
    if (rate_limit.mark_auth_as_failed(remote))
    {
        MXS_NOTICE("Host '%s' blocked for %d seconds due to too many authentication failures.",
                   remote.c_str(), BLOCK_TIME);
    }
}
