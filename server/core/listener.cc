/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
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
using std::map;
using std::move;
using std::string;
using std::unique_ptr;
using ListenerSessionData = mxs::ListenerSessionData;
using SListener = std::shared_ptr<Listener>;

constexpr int BLOCK_TIME = 60;

namespace
{

const char CN_CONNECTION_INIT_SQL_FILE[] = "connection_init_sql_file";

namespace cfg = mxs::config;

const auto RUNTIME = cfg::Param::Modifiable::AT_RUNTIME;

class ListenerSpecification : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const override;
    bool post_validate(json_t* json) const override;
};

ListenerSpecification s_spec("listener", cfg::Specification::LISTENER);

cfg::ParamString s_type(&s_spec, CN_TYPE, "Object type", "listener");
cfg::ParamModule s_protocol(&s_spec, CN_PROTOCOL, "Listener protocol to use",
                            mxs::ModuleType::PROTOCOL, "mariadb");
cfg::ParamString s_authenticator(&s_spec, CN_AUTHENTICATOR, "Listener authenticator", "");
cfg::ParamString s_authenticator_options(&s_spec, CN_AUTHENTICATOR_OPTIONS, "Authenticator options", "");
cfg::ParamService s_service(&s_spec, CN_SERVICE, "Service to which the listener connects to");
cfg::ParamString s_address(&s_spec, CN_ADDRESS, "Listener address", "::");
cfg::ParamString s_socket(&s_spec, CN_SOCKET, "Listener UNIX socket", "");
cfg::ParamCount s_port(&s_spec, CN_PORT, "Listener port", 0);
cfg::ParamBool s_ssl(&s_spec, CN_SSL, "Enable TLS for server", false, RUNTIME);
cfg::ParamPath s_ssl_key(&s_spec, CN_SSL_KEY, "TLS private key", cfg::ParamPath::R, "", RUNTIME);
cfg::ParamPath s_ssl_cert(&s_spec, CN_SSL_CERT, "TLS public certificate", cfg::ParamPath::R, "", RUNTIME);
cfg::ParamPath s_ssl_ca(&s_spec, CN_SSL_CA_CERT, "TLS certificate authority", cfg::ParamPath::R, "", RUNTIME);

cfg::ParamEnum<mxb::ssl_version::Version> s_ssl_version(
    &s_spec, CN_SSL_VERSION, "Minimum TLS protocol version",
    {
        {mxb::ssl_version::SSL_TLS_MAX, "MAX"},
        {mxb::ssl_version::TLS10, "TLSv10"},
        {mxb::ssl_version::TLS11, "TLSv11"},
        {mxb::ssl_version::TLS12, "TLSv12"},
        {mxb::ssl_version::TLS13, "TLSv13"}
    }, mxb::ssl_version::SSL_TLS_MAX, RUNTIME);

cfg::ParamString s_ssl_cipher(&s_spec, CN_SSL_CIPHER, "TLS cipher list", "", RUNTIME);
cfg::ParamString s_ssl_crl(&s_spec, CN_SSL_CRL, "TLS certificate revocation list", "", RUNTIME);

cfg::ParamCount s_ssl_cert_verify_depth(
    &s_spec, CN_SSL_CERT_VERIFY_DEPTH, "TLS certificate verification depth", 9, RUNTIME);

cfg::ParamBool s_ssl_verify_peer_certificate(
    &s_spec, CN_SSL_VERIFY_PEER_CERTIFICATE, "Verify TLS peer certificate", false, RUNTIME);

cfg::ParamBool s_ssl_verify_peer_host(
    &s_spec, CN_SSL_VERIFY_PEER_HOST, "Verify TLS peer host", false, RUNTIME);

cfg::ParamEnum<qc_sql_mode_t> s_sql_mode(&s_spec, CN_SQL_MODE, "SQL parsing mode",
    {
        {QC_SQL_MODE_DEFAULT, "default"},
        {QC_SQL_MODE_ORACLE, "oracle"}
    }, QC_SQL_MODE_DEFAULT, RUNTIME);

cfg::ParamPath s_connection_init_sql_file(
    &s_spec, CN_CONNECTION_INIT_SQL_FILE, "Path to connection initialization SQL", cfg::ParamPath::R, "",
    RUNTIME);

template<class Params>
bool ListenerSpecification::do_post_validate(Params params) const
{
    bool ok = true;

    if (s_ssl.get(params))
    {
        if (s_ssl_key.get(params).empty())
        {
            MXS_ERROR("The 'ssl_key' parameter must be defined when a listener is configured with SSL.");
            ok = false;
        }

        if (s_ssl_cert.get(params).empty())
        {
            MXS_ERROR("The 'ssl_cert' parameter must be defined when a listener is configured with SSL.");
            ok = false;
        }
    }

    return ok;
}

bool ListenerSpecification::post_validate(const mxs::ConfigParameters& params) const
{
    return do_post_validate(params);
}

bool ListenerSpecification::post_validate(json_t* json) const
{
    return do_post_validate(json);
}

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
static ListenerManager this_unit;
}

bool is_all_iface(const std::string& iface)
{
    return iface == "::" || iface == "0.0.0.0";
}

bool is_all_iface(const std::string& a, const std::string& b)
{
    return is_all_iface(a) || is_all_iface(b);
}

bool ListenerManager::listener_is_duplicate(const SListener& listener)
{
    std::string name = listener->name();
    std::string address = listener->address();
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& other : m_listeners)
    {
        if (name == other->name())
        {
            MXS_ERROR("Listener '%s' already exists", name.c_str());
            return true;
        }
        else if (listener->type() == Listener::Type::UNIX_SOCKET && address == other->address())
        {
            MXS_ERROR("Listener '%s' already listens on '%s'", other->name(), address.c_str());
            return true;
        }
        else if (other->port() == listener->port()
                 && (address == other->address() || is_all_iface(listener->address(), other->address())))
        {
            MXS_ERROR("Listener '%s' already listens at [%s]:%d",
                      other->name(), address.c_str(), listener->port());
            return true;
        }
    }

    return false;
}

template<class Params, class Unknown>
SListener ListenerManager::create(const std::string& name, Params params, Unknown unknown)
{
    SListener listener;

    if (s_spec.validate(params, &unknown))
    {
        listener.reset(new Listener(name));

        if (listener->m_config.configure(params))
        {
            listener->set_type();

            if (!listener_is_duplicate(listener))
            {
                std::lock_guard<std::mutex> guard(m_lock);
                m_listeners.push_back(listener);
            }
            else
            {
                listener.reset();
            }
        }
    }
    return listener;
}

void ListenerManager::destroy_instances()
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_listeners.clear();
}

void ListenerManager::remove(const SListener& listener)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_listeners.remove(listener);
}

void ListenerManager::stop_all()
{
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        a->stop();
    }
}

SListener ListenerManager::find(const std::string& name)
{
    SListener rval;
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        if (a->name() == name)
        {
            rval = a;
            break;
        }
    }

    return rval;
}

std::vector<SListener> ListenerManager::find_by_service(const SERVICE* service)
{
    std::vector<SListener> rval;
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        if (a->service() == service)
        {
            rval.push_back(a);
        }
    }

    return rval;
}

json_t* ListenerManager::to_json_collection(const char* host)
{
    json_t* arr = json_array();
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& listener : m_listeners)
    {
        json_array_append_new(arr, listener->to_json(host));
    }

    return mxs_json_resource(host, MXS_JSON_API_LISTENERS, arr);
}

Listener::Config::Config(const std::string& name, Listener* listener)
    : mxs::config::Configuration(name, &s_spec)
    , m_listener(listener)
{
    add_native(&Listener::Config::type, &s_type);
    add_native(&Listener::Config::protocol, &s_protocol);
    add_native(&Listener::Config::authenticator, &s_authenticator);
    add_native(&Listener::Config::authenticator_options, &s_authenticator_options);
    add_native(&Listener::Config::service, &s_service);
    add_native(&Listener::Config::address, &s_address);
    add_native(&Listener::Config::socket, &s_socket);
    add_native(&Listener::Config::port, &s_port);
    add_native(&Listener::Config::ssl, &s_ssl);
    add_native(&Listener::Config::ssl_key, &s_ssl_key);
    add_native(&Listener::Config::ssl_cert, &s_ssl_cert);
    add_native(&Listener::Config::ssl_ca, &s_ssl_ca);
    add_native(&Listener::Config::ssl_version, &s_ssl_version);
    add_native(&Listener::Config::ssl_cipher, &s_ssl_cipher);
    add_native(&Listener::Config::ssl_crl, &s_ssl_crl);
    add_native(&Listener::Config::ssl_cert_verify_depth, &s_ssl_cert_verify_depth);
    add_native(&Listener::Config::ssl_verify_peer_certificate, &s_ssl_verify_peer_certificate);
    add_native(&Listener::Config::ssl_verify_peer_host, &s_ssl_verify_peer_host);
    add_native(&Listener::Config::sql_mode, &s_sql_mode);
    add_native(&Listener::Config::connection_init_sql_file, &s_connection_init_sql_file);
}

bool Listener::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    mxb_assert(nested_params.size() <=1);
    mxb_assert(nested_params.size() == 0 ||
               (nested_params.size() == 1 && nested_params.find(protocol->name) != nested_params.end()));

    if (port > 0 && !socket.empty())
    {
        MXS_ERROR("Creation of listener '%s' failed because both 'socket' and 'port' "
                  "are defined. Only one of them is allowed.",
                  name().c_str());
        return false;
    }
    else if (port == 0 && socket.empty())
    {
        MXS_ERROR("Listener '%s' is missing the port or socket parameter.", name().c_str());
        return false;
    }
    else if (!socket.empty() && socket[0] != '/')
    {
        MXS_ERROR("Invalid path given for listener '%s' for parameter '%s': %s",
                  name().c_str(), CN_SOCKET, socket.c_str());
        return false;
    }

    mxs::ConfigParameters params;

    if (nested_params.size() == 1)
    {
        params = nested_params.at(protocol->name);
    }

    return m_listener->post_configure(params);
}

bool Listener::Config::configure(const mxs::ConfigParameters& params,
                                 mxs::ConfigParameters* pUnrecognized)
{
    m_listener->m_params = params;
    return mxs::config::Configuration::configure(params, pUnrecognized);
}

bool Listener::Config::configure(json_t* json, std::set<std::string>* pUnrecognized)
{
    m_listener->m_params = mxs::ConfigParameters::from_json(json);
    return mxs::config::Configuration::configure(json, pUnrecognized);
}

// static
mxs::config::Specification* Listener::specification()
{
    return &s_spec;
}

Listener::Listener(const std::string& name)
    : MXB_POLL_DATA{Listener::poll_handler}
    , m_config(name, this)
    , m_name(name)
    , m_state(CREATED)
{
}

Listener::~Listener()
{
    MXS_INFO("Destroying '%s'", m_name.c_str());
}

SListener Listener::create(const std::string& name, const mxs::ConfigParameters& params)
{
    mxb::LogScope scope(name.c_str());
    mxs::ConfigParameters unknown;
    return this_unit.create(name, params, unknown);
}

SListener Listener::create(const std::string& name, json_t* params)
{
    mxb::LogScope scope(name.c_str());
    std::set<std::string> unknown;
    return this_unit.create(name, params, unknown);
}

void Listener::set_type()
{
    // Setting the type only once avoids it being repeatedly being set in the post_configure method.

    if (!m_config.socket.empty())
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

void listener_destroy_instances()
{
    this_unit.destroy_instances();
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

    this_unit.remove(listener);
}

// static
void Listener::stop_all()
{
    this_unit.stop_all();
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
    return this_unit.find(name);
}

std::vector<SListener> listener_find_by_service(const SERVICE* service)
{
    return this_unit.find_by_service(service);
}

std::ostream& Listener::persist(std::ostream& os) const
{
    return m_config.persist(os);
}

json_t* Listener::to_json(const char* host) const
{
    const char CN_AUTHENTICATOR_DIAGNOSTICS[] = "authenticator_diagnostics";

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(state()));
    json_object_set_new(attr, CN_PARAMETERS, m_config.to_json());

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
    mxs_json_add_relation(service, m_config.service->name(), CN_SERVICES);
    json_object_set_new(rel, CN_SERVICES, service);
    json_object_set_new(rval, CN_RELATIONSHIPS, rel);

    return rval;
}

// static
json_t* Listener::to_json_collection(const char* host)
{
    return this_unit.to_json_collection(host);
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
    return m_type == Type::UNIX_SOCKET ? m_config.socket.c_str() : m_config.address.c_str();
}

uint16_t Listener::port() const
{
    return m_config.port;
}

SERVICE* Listener::service() const
{
    return m_config.service;
}

const char* Listener::protocol() const
{
    mxb_assert(m_config.protocol);
    return m_config.protocol->name;
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

        if (service()->has_too_many_connections())
        {
            // TODO: If connections can be queued, this is the place to put the
            // TODO: connection on that queue.
            pProtocol->connlimit(service()->config()->max_connections);

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
    int fd = start_listening(address(), port());

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
        MXS_ERROR("Failed to listen on [%s]:%u", address(), port());
    }

    return rval;
}

bool Listener::listen_unique()
{
    auto open_socket = [this]() {
            bool rval = false;
            int fd = start_listening(address(), port());

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
        MXS_ERROR("One or more workers failed to listen on '[%s]:%u'.", address(), port());
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
        MXS_NOTICE("Listening for connections at [%s]:%u", address(), port());
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
            write(fd, GWBUF_DATA(b), gwbuf_link_length(b));
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

Listener::SData Listener::create_shared_data(const mxs::ConfigParameters& protocol_params)
{
    SData rval;

    auto protocol_api = reinterpret_cast<MXS_PROTOCOL_API*>(m_config.protocol->module_object);
    std::unique_ptr<mxs::ProtocolModule> protocol_module {protocol_api->create_protocol_module(protocol_params)};

    if (protocol_module)
    {
        // TODO: The old behavior where the global sql_mode was used if the listener one isn't configured
        mxs::SSLContext ssl;

        if (ssl.configure(create_ssl_config()))
        {
            ListenerSessionData::ConnectionInitSql init_sql;
            if (read_connection_init_sql(m_config.connection_init_sql_file, &init_sql))
            {
                std::vector<mxs::SAuthenticatorModule> authenticators;

                if (protocol_module->capabilities() & mxs::ProtocolModule::CAP_AUTH_MODULES)
                {
                    // If the protocol uses separate authenticator modules, assume that at least
                    // one must be created.
                    authenticators = protocol_module->create_authenticators(m_params);

                    if (authenticators.empty())
                    {
                        return {};
                    }
                }

                if (protocol_module->capabilities() & mxs::ProtocolModule::CAP_AUTHDATA)
                {
                    auto svc = static_cast<Service*>(m_config.service);

                    if (!svc->check_update_user_account_manager(protocol_module.get(), m_name))
                    {
                        return {};
                    }
                }

                rval = std::make_shared<ListenerSessionData>(
                    move(ssl), m_config.sql_mode, m_config.service, move(protocol_module),
                    m_name, move(authenticators), move(init_sql));
            }
        }
    }
    else
    {
        MXS_ERROR("Failed to initialize protocol module '%s' for listener '%s'.",
                  m_config.protocol->name, m_name.c_str());
    }

    return rval;
}

mxb::SSLConfig Listener::create_ssl_config()
{
    mxb::SSLConfig cfg;

    cfg.enabled = m_config.ssl;
    cfg.key = m_config.ssl_key;
    cfg.cert = m_config.ssl_cert;
    cfg.ca = m_config.ssl_ca;
    cfg.version = m_config.ssl_version;
    cfg.verify_peer = m_config.ssl_verify_peer_certificate;
    cfg.verify_host = m_config.ssl_verify_peer_host;
    cfg.crl = m_config.ssl_crl;
    cfg.verify_depth = m_config.ssl_cert_verify_depth;
    cfg.cipher = m_config.ssl_cipher;

    return cfg;
}

bool Listener::post_configure(const mxs::ConfigParameters& protocol_params)
{
    bool rval = false;

    if (auto data = create_shared_data(protocol_params))
    {
        auto start_state = m_state;

        if (start_state == STARTED)
        {
            stop();
        }

        m_shared_data = data;
        rval = true;

        if (start_state == STARTED)
        {
            start();
        }
    }

    return rval;
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
}

std::shared_ptr<mxs::ListenerSessionData>
Listener::create_test_data(const mxs::ConfigParameters& params)
{
    SListener listener {new Listener("test_listener")};
    listener->m_config.configure(params);
    mxs::ConfigParameters protocol_params;
    return listener->create_shared_data(protocol_params);
}
