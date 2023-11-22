/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/listener.hh>

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

#include <maxbase/log.hh>
#include <maxscale/json_api.hh>
#include <maxscale/protocol2.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/secrets.hh>
#include <maxscale/service.hh>
#include <maxscale/ssl.hh>
#include <maxscale/utils.hh>

#include "internal/config.hh"
#include "internal/modules.hh"
#include "internal/session.hh"

using std::chrono::seconds;
using std::map;
using std::move;
using std::string;
using std::unique_ptr;
using std::vector;
using mxs::ListenerData;

constexpr int BLOCK_TIME = 60;

namespace
{

const char CN_CONNECTION_INIT_SQL_FILE[] = "connection_init_sql_file";
const char CN_PROXY_PROTOCOL_NETWORKS[] = "proxy_protocol_networks";

constexpr std::string_view TX_ISOLATION = "tx_isolation";
constexpr std::string_view TRANSACTION_ISOLATION = "transaction_isolation";

namespace cfg = mxs::config;

const auto RUNTIME = cfg::Param::Modifiable::AT_RUNTIME;

class ListenerSpecification : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:
    template<class Params>
    bool do_post_validate(Params& params) const;

    bool post_validate(const cfg::Configuration* config,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override
    {
        return do_post_validate(params);
    }

    bool post_validate(const cfg::Configuration* config,
                       json_t* params,
                       const std::map<std::string, json_t*>& nested_params) const override
    {
        return do_post_validate(params);
    }
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
cfg::ParamPath s_ssl_ca(&s_spec, CN_SSL_CA, "TLS certificate authority", cfg::ParamPath::R, "", RUNTIME);
cfg::ParamDeprecated<cfg::ParamAlias> s_ssl_ca_cert(&s_spec, CN_SSL_CA_CERT, &s_ssl_ca);

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

cfg::ParamEnum<mxs::Parser::SqlMode> s_sql_mode(&s_spec, CN_SQL_MODE, "SQL parsing mode",
    {
        {mxs::Parser::SqlMode::DEFAULT, "default"},
        {mxs::Parser::SqlMode::ORACLE, "oracle"}
    }, mxs::Parser::SqlMode::DEFAULT, RUNTIME);

cfg::ParamPath s_connection_init_sql_file(
    &s_spec, CN_CONNECTION_INIT_SQL_FILE, "Path to connection initialization SQL", cfg::ParamPath::R, "",
    RUNTIME);

cfg::ParamPath s_user_mapping_file(
    &s_spec, "user_mapping_file", "Path to user and group mapping file", cfg::ParamPath::R, "",
    RUNTIME);

cfg::ParamString s_proxy_networks(
    &s_spec, CN_PROXY_PROTOCOL_NETWORKS, "Allowed (sub)networks for proxy protocol connections. Should be "
                                         "a comma-separated list of IPv4 or IPv6 addresses.", "", RUNTIME);

cfg::ParamStringList s_connection_metadata(
    &s_spec, "connection_metadata",
    "Metadata that's sent to all connecting clients.",
    ",",
    {
        "character_set_client=auto",
        "character_set_connection=auto",
        "character_set_results=auto",
        "max_allowed_packet=auto",
        "system_time_zone=auto",
        "time_zone=auto",
        "tx_isolation=auto",
    },
    RUNTIME);

template<class Params>
bool ListenerSpecification::do_post_validate(Params& params) const
{
    bool ok = true;

    if (s_ssl.get(params))
    {
        if (s_ssl_key.get(params).empty())
        {
            MXB_ERROR("The 'ssl_key' parameter must be defined when a listener is configured with SSL.");
            ok = false;
        }

        if (s_ssl_cert.get(params).empty())
        {
            MXB_ERROR("The 'ssl_cert' parameter must be defined when a listener is configured with SSL.");
            ok = false;
        }
    }

    if (auto values = s_connection_metadata.get(params); !values.empty())
    {
        for (const auto& val : values)
        {
            if (val.find("=") == std::string::npos)
            {
                MXB_ERROR("Invalid key-value list for '%s': %s",
                          s_connection_metadata.name().c_str(), val.c_str());
                ok = false;
            }
        }
    }

    auto pn_parse_res = mxb::proxy_protocol::parse_networks_from_string(s_proxy_networks.get(params));
    if (!pn_parse_res.errmsg.empty())
    {
        MXB_ERROR("Failed to parse %s. %s", CN_PROXY_PROTOCOL_NETWORKS, pn_parse_res.errmsg.c_str());
        ok = false;
    }
    return ok;
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
thread_local std::vector<std::string> listen_errors;

static bool redirect_listener_errors(int level, std::string_view msg)
{
    // Lower is more severe. Include warnings as they bring context to the automatic re-bind to IPv4 that is
    // done if the IPv6 binding fails.
    if (level < LOG_NOTICE)
    {
        // The suppression message should not be included in the actual message. This would look really odd if
        // shown to the client. If it's not found, the substr() call ends up just copying the whole string.
        auto pos = msg.find(" (subsequent similar messages");
        listen_errors.emplace_back(msg.substr(0, pos));
        return true;
    }

    return false;
}

// Helper function for extracting the best candidate server from a set of servers based on a set of status
// bits. The status bits given as template parameters are in increasing priority, that is, the worst candidate
// type is first and the best one is the last.
template<uint64_t ... Bits>
SERVER* best_server(const std::vector<SERVER*>& container)
{
    constexpr std::array<int, sizeof...(Bits)> bit_array{Bits ...};
    SERVER* rval = nullptr;
    int best = -1;

    for (SERVER* t : container)
    {
        int status = t->status();
        int rank = -1;

        for (int i = 0; i < (int)bit_array.size(); i++)
        {
            if (status & bit_array[i])
            {
                rank = i;
            }
        }

        if (rank > best)
        {
            rval = t;
            best = rank;
        }
    }

    return rval;
}
}

bool is_all_iface(const std::string& iface)
{
    return iface == "::" || iface == "0.0.0.0";
}

bool is_all_iface(const std::string& a, const std::string& b)
{
    return is_all_iface(a) || is_all_iface(b);
}

namespace maxscale
{

/**
 * ListenerData
 */
ListenerData::ListenerData(SSLContext ssl, mxs::Parser::SqlMode default_sql_mode,
                           std::unique_ptr<mxs::ProtocolModule> protocol_module,
                           const std::string& listener_name,
                           std::vector<SAuthenticator>&& authenticators,
                           ListenerData::ConnectionInitSql&& init_sql, SMappingInfo mapping,
                           mxb::proxy_protocol::SubnetArray&& proxy_networks)
    : m_ssl(move(ssl))
    , m_default_sql_mode(default_sql_mode)
    , m_proto_module(move(protocol_module))
    , m_listener_name(listener_name)
    , m_authenticators(move(authenticators))
    , m_conn_init_sql(std::move(init_sql))
    , m_mapping_info(move(mapping))
    , m_proxy_networks(std::move(proxy_networks))
{
}

/**
 * Listener::Manager
 */

class Listener::Manager
{
public:
    template<class Params>
    SListener create(const std::string& name, Params params);

    void                   clear();
    void                   remove(const SListener& listener);
    json_t*                to_json_collection(const char* host);
    SListener              find(const std::string& name);
    std::vector<SListener> find_by_service(const SERVICE* service);
    void                   stop_all();
    bool                   reload_tls();
    std::vector<SListener> get_started_listeners();
    void                   server_variables_changed(SERVER* server);

private:
    std::list<SListener> m_listeners;
    std::mutex           m_lock;

    bool listener_is_duplicate(const SListener& listener);
};

bool Listener::Manager::listener_is_duplicate(const SListener& listener)
{
    std::string name = listener->name();
    std::string address = listener->address();
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& other : m_listeners)
    {
        if (name == other->name())
        {
            MXB_ERROR("Listener '%s' already exists", name.c_str());
            return true;
        }
        else if (listener->type() == Listener::Type::UNIX_SOCKET && address == other->address())
        {
            MXB_ERROR("Listener '%s' already listens on '%s'", other->name(), address.c_str());
            return true;
        }
        else if (other->port() == listener->port()
                 && (address == other->address() || is_all_iface(listener->address(), other->address())))
        {
            MXB_ERROR("Listener '%s' already listens at [%s]:%d",
                      other->name(), address.c_str(), listener->port());
            return true;
        }
    }

    return false;
}

template<class Params>
SListener Listener::Manager::create(const std::string& name, Params params)
{
    SListener rval;

    if (s_spec.validate(params))
    {
        SListener listener(new Listener(name));

        if (listener->m_config.configure(params))
        {
            listener->set_type();

            if (!listener_is_duplicate(listener))
            {
                std::lock_guard<std::mutex> guard(m_lock);
                m_listeners.push_back(listener);
                rval = std::move(listener);
            }
        }
    }

    return rval;
}

void Listener::Manager::clear()
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_listeners.clear();
}

void Listener::Manager::remove(const SListener& listener)
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_listeners.remove(listener);
}

void Listener::Manager::stop_all()
{
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        a->stop();
    }
}

bool Listener::Manager::reload_tls()
{
    bool ok = true;
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        if (!a->force_config_reload())
        {
            ok = false;
            break;
        }
    }

    return ok;
}

vector<SListener> Listener::Manager::get_started_listeners()
{
    // Not all unit tests have a MainWorker.
    mxb_assert(!mxb::Worker::get_current() || mxs::MainWorker::is_current());

    vector<SListener> started_listeners;

    for (auto listener : m_listeners)
    {
        if (listener->m_state == Listener::STARTED)
        {
            started_listeners.push_back(listener);
        }
    }

    return started_listeners;
}

void Listener::Manager::server_variables_changed(SERVER* server)
{
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& a : m_listeners)
    {
        auto servers = a->service()->reachable_servers();

        if (std::find(servers.begin(), servers.end(), server) != servers.end())
        {
            auto listener_data = a->m_shared_data->listener_data;
            a->m_shared_data.assign(SharedData {std::move(listener_data), a->create_connection_metadata()});
        }
    }
}

SListener Listener::Manager::find(const std::string& name)
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

std::vector<SListener> Listener::Manager::find_by_service(const SERVICE* service)
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

json_t* Listener::Manager::to_json_collection(const char* host)
{
    json_t* arr = json_array();
    std::lock_guard<std::mutex> guard(m_lock);

    for (const auto& listener : m_listeners)
    {
        json_array_append_new(arr, listener->to_json(host));
    }

    return mxs_json_resource(host, MXS_JSON_API_LISTENERS, arr);
}

/**
 * Listener::Config
 */
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
    add_native(&Listener::Config::user_mapping_file, &s_user_mapping_file);
    add_native(&Listener::Config::proxy_networks, &s_proxy_networks);
    add_native(&Listener::Config::connection_metadata, &s_connection_metadata);
}

bool Listener::Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    std::string protocol_name = mxb::lower_case_copy(protocol->name);
    mxb_assert(nested_params.size() <= 1);
    mxb_assert(nested_params.size() == 0
               || (nested_params.size() == 1
                   && nested_params.find(protocol_name) != nested_params.end()));

    if (port > 0 && !socket.empty())
    {
        MXB_ERROR("Creation of listener '%s' failed because both 'socket' and 'port' "
                  "are defined. Only one of them is allowed.",
                  name().c_str());
        return false;
    }
    else if (port == 0 && socket.empty())
    {
        MXB_ERROR("Listener '%s' is missing the port or socket parameter.", name().c_str());
        return false;
    }
    else if (!socket.empty() && socket[0] != '/')
    {
        MXB_ERROR("Invalid path given for listener '%s' for parameter '%s': %s",
                  name().c_str(), CN_SOCKET, socket.c_str());
        return false;
    }

    mxs::ConfigParameters params;
    auto it = nested_params.find(protocol_name);

    if (it != nested_params.end())
    {
        params = it->second;
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

/**
 * Listener
 */

// static
Listener::Manager Listener::s_manager;

// static
mxs::config::Specification* Listener::specification()
{
    return &s_spec;
}

Listener::Listener(const std::string& name)
    : mxb::Pollable(mxb::Pollable::SHARED)
    , m_config(name, this)
    , m_name(name)
    , m_state(CREATED)
{
}

Listener::~Listener()
{
    MXB_INFO("Destroying '%s'", m_name.c_str());
}

SListener Listener::create(const std::string& name, const mxs::ConfigParameters& params)
{
    mxb::LogScope scope(name.c_str());
    return s_manager.create(name, params);
}

SListener Listener::create(const std::string& name, json_t* params)
{
    mxb::LogScope scope(name.c_str());
    return s_manager.create(name, params);
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

bool Listener::force_config_reload()
{
    mxb::LogScope scope(name());
    mxb::Json js(json_parameters(), mxb::Json::RefType::STEAL);
    js.remove_nulls();

    return m_config.specification().validate(js.get_json()) && m_config.configure(js.get_json());
}

void Listener::clear()
{
    s_manager.clear();
}

std::vector<std::shared_ptr<Listener>> Listener::get_started_listeners()
{
    return s_manager.get_started_listeners();
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

    s_manager.remove(listener);
}

// static
void Listener::stop_all()
{
    s_manager.stop_all();
}

// static
bool Listener::reload_tls()
{
    return s_manager.reload_tls();
}

// static
void Listener::mark_auth_as_failed(const std::string& remote)
{
    if (rate_limit.mark_auth_as_failed(remote))
    {
        MXB_NOTICE("Host '%s' blocked for %d seconds due to too many authentication failures.",
                   remote.c_str(), BLOCK_TIME);
    }
}

// static
void Listener::server_variables_changed(SERVER* server)
{
    mxs::MainWorker::get()->execute([server](){
        s_manager.server_variables_changed(server);
    }, mxb::Worker::EXECUTE_AUTO);
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
                bool rv = true;
                if (*m_local_fd != -1)
                {
                    auto worker = mxs::RoutingWorker::get_current();
                    rv = worker->remove_pollable(this);
                }
                return rv;
            }))
            {
                m_state = STOPPED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::remove_listener(this))
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
                return worker->add_pollable(EPOLLIN, this);
            }))
            {
                m_state = STARTED;
                rval = true;
            }
        }
        else
        {
            if (mxs::RoutingWorker::add_listener(this))
            {
                m_state = STARTED;
                rval = true;
            }
        }
    }

    return rval;
}

// static
SListener Listener::find(const std::string& name)
{
    return s_manager.find(name);
}

// static
std::vector<SListener> Listener::find_by_service(const SERVICE* service)
{
    return s_manager.find_by_service(service);
}

std::ostream& Listener::persist(std::ostream& os) const
{
    m_config.persist(os, {s_type.name()});
    m_shared_data->listener_data->m_proto_module->getConfiguration().persist_append(os);
    return os;
}

json_t* Listener::json_parameters() const
{
    json_t* params = m_config.to_json();
    json_t* tmp = m_shared_data->listener_data->m_proto_module->getConfiguration().to_json();
    json_object_update(params, tmp);
    json_decref(tmp);

    return params;
}

json_t* Listener::to_json(const char* host) const
{
    const char CN_AUTHENTICATOR_DIAGNOSTICS[] = "authenticator_diagnostics";

    json_t* attr = json_object();
    json_object_set_new(attr, CN_STATE, json_string(state()));
    json_object_set_new(attr, CN_SOURCE, mxs::Config::object_source_to_json(name()));

    auto& protocol_module = m_shared_data->listener_data->m_proto_module;

    json_object_set_new(attr, CN_PARAMETERS, json_parameters());

    json_t* diag = protocol_module->print_auth_users_json();
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
    return s_manager.to_json_collection(host);
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
        MXB_ERROR("Failed to unlink Unix Socket %s: %d %s", path, errno, mxb_strerror(errno));
    }

    struct sockaddr_un local_addr;
    int listener_socket = open_unix_socket(MxsSocketType::LISTEN, &local_addr, path);

    if (listener_socket >= 0 && chmod(path, 0777) < 0)
    {
        MXB_ERROR("Failed to change permissions on UNIX Domain socket '%s': %d, %s",
                  path, errno, mxb_strerror(errno));
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
        listener_socket = open_listener_network_socket(host.c_str(), port);

        if (listener_socket == -1 && host == "::")
        {
            /** Attempt to bind to the IPv4 if the default IPv6 one is used */
            MXB_WARNING("Failed to bind on default IPv6 host '::', attempting "
                        "to bind on IPv4 version '0.0.0.0'");
            listener_socket = open_listener_network_socket("0.0.0.0", port);
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
            MXB_ERROR("Failed to start listening on [%s]:%u: %d, %s", host.c_str(), port, errno,
                      mxb_strerror(errno));
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
    sockaddr_storage addr {};
    socklen_t client_len = sizeof(addr);
    conn.fd = accept4(fd, (sockaddr*)&addr, &client_len, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (conn.fd != -1)
    {
        mxb::get_normalized_ip(addr, &conn.addr);
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
        MXB_ERROR("Failed to accept new client connection: %d, %s", errno, mxb_strerror(errno));
    }

    return conn;
}
}

ClientDCB* Listener::accept_one_dcb(int fd, const sockaddr_storage* addr, const char* host,
                                    const SharedData& shared_data)
{
    const auto& sdata = shared_data.listener_data;
    auto* session = new(std::nothrow) Session(sdata, shared_data.metadata, m_config.service, host);
    if (!session)
    {
        MXB_OOM();
        close(fd);
        return NULL;
    }

    auto client_protocol = sdata->m_proto_module->create_client_protocol(session, session);
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
        MXB_OOM();
        delete session;
    }
    else
    {
        // Order is significant, since the session will extract the
        // client dcb from the client connection.
        pProtocol->set_dcb(client_dcb);
        session->set_client_connection(pProtocol);

        if (service()->has_too_many_connections())
        {
            // TODO: If connections can be queued, this is the place to put the
            // TODO: connection on that queue.
            pProtocol->connlimit(service()->config()->max_connections);

            // TODO: This is never used as the client connection is not up yet
            client_dcb->session()->close_reason = SESSION_CLOSE_TOO_MANY_CONNECTIONS;

            ClientDCB::close(client_dcb);
            client_dcb = NULL;
        }
        else if (session->is_enabled())
        {
            // TODO: Not quite alright that the listener enables the events
            // TODO: behind the session's back.
            if (!client_dcb->enable_events())
            {
                MXB_ERROR("Failed to add dcb %p for fd %d to epoll set.", client_dcb, fd);
                ClientDCB::close(client_dcb);
                client_dcb = NULL;
            }
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
        // All workers share the same fd, assign it here
        m_shared_fd = fd;
        if (mxs::RoutingWorker::add_listener(this))
        {
            rval = true;
            m_state = STARTED;
        }
        else
        {
            m_shared_fd = -1;
            close(fd);
        }
    }
    else
    {
        MXB_ERROR("Failed to listen on [%s]:%u", address(), port());
    }

    return rval;
}

bool Listener::listen_shared(mxs::RoutingWorker& worker)
{
    // Nothing can to be done; whether or not the worker reacts on
    // events on the listener fd, depends upon whether the worker
    // listens on events on the shared routing worker fd.
    return false;
}

bool Listener::unlisten_shared(mxs::RoutingWorker& worker)
{
    // Nothing can to be done; whether or not the worker reacts on
    // events on the listener fd, depends upon whether the worker
    // listens on events on the shared routing worker fd.
    return false;
}

bool Listener::open_unique_listener(mxs::RoutingWorker& worker, std::mutex& lock,
                                    std::vector<std::string>& errors)
{
    mxb::LogRedirect redirect(redirect_listener_errors);
    mxb::LogScope scope(name());
    bool rval = false;
    int fd = start_listening(address(), port());

    if (fd != -1)
    {
        // Set the worker-local fd to the unique value
        *m_local_fd = fd;
        rval = worker.add_pollable(EPOLLIN, this);

        if (!rval)
        {
            *m_local_fd = -1;
            close(fd);
        }
    }

    if (!rval)
    {
        std::lock_guard<std::mutex> guard(lock);

        for (auto&& msg : listen_errors)
        {
            if (std::find(errors.begin(), errors.end(), msg) == errors.end())
            {
                errors.emplace_back(std::move(msg));
            }
        }

        listen_errors.clear();
    }

    return rval;
}

bool Listener::listen_unique()
{
    std::mutex lock;
    std::vector<std::string> errors;
    auto open_socket = [&]() {
        mxb::LogScope scope(name());
        return open_unique_listener(*mxs::RoutingWorker::get_current(), lock, errors);
    };

    bool rval = execute_and_check(open_socket);

    if (!rval)
    {
        close_all_fds();
        std::lock_guard<std::mutex> guard(lock);
        mxb_assert_message(!errors.empty(), "Failure to listen should cause an error to be logged");

        for (const auto& msg : errors)
        {
            MXB_ERROR("%s", msg.c_str());
        }
    }

    return rval;
}

bool Listener::listen_unique(mxs::RoutingWorker& worker)
{
    bool rval = true;

    if (m_state == STARTED)
    {
        std::mutex lock;
        std::vector<std::string> errors;
        rval = false;

        auto open_socket = [&]() {
            mxb_assert(*m_local_fd == -1);
            mxb::LogScope scope(name());
            rval = open_unique_listener(worker, lock, errors);
        };

        if (!worker.call(open_socket))
        {
            MXB_ERROR("Could not call worker thread; it will not start listening "
                      "on listener socket.");
        }

        if (!rval)
        {
            std::lock_guard<std::mutex> guard(lock);
            mxb_assert_message(!errors.empty(), "Failure to listen should cause an error to be logged");

            for (const auto& msg : errors)
            {
                MXB_ERROR("%s", msg.c_str());
            }
        }
    }

    return rval;
}

bool Listener::unlisten_unique(mxs::RoutingWorker& worker)
{
    bool rval = true;

    if (m_state == STARTED)
    {
        rval = false;

        auto close_socket = [this, &worker, &rval]() {
            mxb_assert(*m_local_fd != -1);

            mxb::LogScope scope(name());

            rval = worker.remove_pollable(this);

            close(*m_local_fd);
            *m_local_fd = -1;
        };

        if (!worker.call(close_socket))
        {
            MXB_ERROR("Could not call worker thread; it will not stop listening "
                      "on listener socket.");
        }
    }

    return rval;
}

bool Listener::listen()
{
    mxb_assert(mxs::MainWorker::is_current());

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
        MXB_NOTICE("Listening for connections at [%s]:%u", address(), port());
    }

    return rval;
}

bool Listener::listen(mxs::RoutingWorker& worker)
{
    mxb_assert(mxs::MainWorker::is_current() || &worker == mxs::RoutingWorker::get_current());

    mxb::LogScope scope(name());

    bool rval = true;
    if (m_state == STARTED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            rval = listen_unique(worker);
        }
        else
        {
            rval = listen_shared(worker);
        }
    }

    return rval;
}

bool Listener::unlisten(mxs::RoutingWorker& worker)
{
    mxb_assert(mxs::MainWorker::is_current() || &worker == mxs::RoutingWorker::get_current());

    mxb::LogScope scope(name());

    bool rval = true;
    if (m_state == STARTED)
    {
        if (m_type == Type::UNIQUE_TCP)
        {
            rval = unlisten_unique(worker);
        }
        else
        {
            rval = unlisten_shared(worker);
        }
    }

    return rval;
}

int Listener::poll_fd() const
{
    return fd();
}

uint32_t Listener::handle_poll_events(mxb::Worker* worker, uint32_t events, Pollable::Context)
{
    accept_connections();
    return mxb::poll_action::ACCEPT;
}

void Listener::reject_connection(int fd, const char* host)
{
    std::string message = mxb::cat("Host '", host, "' is temporarily blocked due ",
                                   "to too many authentication failures.");
    int errnum = 1129;      // This is ER_HOST_IS_BLOCKED
    const auto& sdata = m_shared_data->listener_data;

    if (GWBUF buf = sdata->m_proto_module->make_error(errnum, "HY000", message); !buf.empty())
    {
        write(fd, buf.data(), buf.length());
    }

    close(fd);
}

void Listener::accept_connections()
{
    mxb::LogScope scope(name());
    const auto& shared_data = *m_shared_data;

    for (ClientConn conn = accept_one_connection(fd()); conn.fd != -1; conn = accept_one_connection(fd()))
    {
        if (rate_limit.is_blocked(conn.host))
        {
            reject_connection(conn.fd, conn.host);
        }
        else if (type() == Type::UNIQUE_TCP)
        {
            if (ClientDCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host, shared_data))
            {
                if (!dcb->protocol()->init_connection())
                {
                    ClientDCB::close(dcb);
                }
            }
        }
        else
        {
            auto worker = mxs::RoutingWorker::pick_worker();
            worker->execute([this, conn]() {
                if (ClientDCB* dcb = accept_one_dcb(conn.fd, &conn.addr, conn.host, *m_shared_data))
                {
                    if (!dcb->protocol()->init_connection())
                    {
                        ClientDCB::close(dcb);
                    }
                }
            }, mxs::RoutingWorker::EXECUTE_AUTO);
        }
    }
}

Listener::SData Listener::create_shared_data(const mxs::ConfigParameters& protocol_params)
{
    using SProtocolModule = std::unique_ptr<mxs::ProtocolModule>;

    SData rval;

    auto protocol_api = reinterpret_cast<MXS_PROTOCOL_API*>(m_config.protocol->module_object);
    SProtocolModule protocol_module(protocol_api->create_protocol_module(m_name, this));
    auto svc = static_cast<Service*>(m_config.service);

    if (protocol_module && svc->protocol_is_compatible(*protocol_module)
        && protocol_module->getConfiguration().configure(protocol_params))
    {
        // TODO: The old behavior where the global sql_mode was used if the listener one isn't configured
        mxs::SSLContext ssl;
        ssl.set_usage(mxb::KeyUsage::SERVER);
        ListenerData::ConnectionInitSql init_sql;
        ListenerData::SMappingInfo mapping_info;
        mxb::proxy_protocol::SubnetArray proxy_networks;

        if (ssl.configure(create_ssl_config()) && read_connection_init_sql(*protocol_module, init_sql)
            && read_user_mapping(mapping_info) && read_proxy_networks(proxy_networks))
        {
            bool auth_modules_ok = true;
            std::vector<mxs::SAuthenticatorModule> authenticators;

            if (protocol_module->capabilities() & mxs::ProtocolModule::CAP_AUTH_MODULES)
            {
                // If the protocol uses separate authenticator modules, assume that at least
                // one must be created.
                authenticators = protocol_module->create_authenticators(m_params);
                if (authenticators.empty())
                {
                    auth_modules_ok = false;
                }
            }

            if (auth_modules_ok)
            {
                rval = std::make_shared<const mxs::ListenerData>(
                    move(ssl), m_config.sql_mode, move(protocol_module),
                    m_name, move(authenticators), move(init_sql), move(mapping_info),
                    std::move(proxy_networks));
            }
        }
    }
    else
    {
        MXB_ERROR("Failed to initialize protocol module '%s' for listener '%s'.",
                  m_config.protocol->name, m_name.c_str());
    }

    return rval;
}

Listener::SMetadata Listener::create_connection_metadata()
{
    std::map<std::string, std::string> metadata;
    SERVER* srv = best_server<SERVER_RUNNING, SERVER_SLAVE, SERVER_MASTER>(
        m_config.service->reachable_servers());

    for (const auto& val : m_config.connection_metadata)
    {
        if (auto [key, value] = mxb::split(val, "="); value == "auto")
        {
            if (srv)
            {
                if (std::string var = srv->get_variable_value(key); !var.empty())
                {
                    if (key == TX_ISOLATION && srv->info().version_num().major > 10)
                    {
                        key = TRANSACTION_ISOLATION;
                    }

                    metadata.emplace(key, var);
                }
            }
        }
        else
        {
            metadata.emplace(key, value);
        }
    }

    return std::make_shared<MXS_SESSION::ConnectionMetadata>(std::move(metadata));
}

mxb::SSLConfig Listener::create_ssl_config() const
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

    auto servers = m_config.service->reachable_servers();

    for (const auto& val : m_config.connection_metadata)
    {
        // Track all variables that have the value set to "auto", this way they'll get updated automatically
        // whenever they're changed on the source server.
        if (auto [key, value] = mxb::split(val, "="); value == "auto")
        {
            for (auto srv : servers)
            {
                // TODO: Currently the set of variables is append-only. The superset of trackable variables
                // should be recalculated after every reconfiguration.
                srv->track_variable(key);

                if (key == TX_ISOLATION && srv->info().version_num().major > 10)
                {
                    srv->track_variable(TRANSACTION_ISOLATION);
                }
            }
        }
    }

    if (auto data = create_shared_data(protocol_params))
    {
        bool uam_ok = true;
        auto* prot_module = data->m_proto_module.get();
        if (prot_module->capabilities() & mxs::ProtocolModule::CAP_AUTHDATA)
        {
            auto svc = static_cast<Service*>(m_config.service);
            if (!svc->check_update_user_account_manager(prot_module, m_name))
            {
                uam_ok = false;
            }
        }

        if (uam_ok)
        {
            auto start_state = m_state;

            if (start_state == STARTED)
            {
                stop();
            }

            m_shared_data.assign(SharedData {std::move(data), create_connection_metadata()});
            rval = true;

            if (start_state == STARTED)
            {
                start();
            }
        }
    }

    return rval;
}

/**
 * Read in connection init sql file.
 *
 * @param output Output object
 * @return True on success, or if setting was not set.
 */
bool Listener::read_connection_init_sql(const mxs::ProtocolModule& protocol,
                                        ListenerData::ConnectionInitSql& output) const
{
    const string& filepath = m_config.connection_init_sql_file;
    bool file_ok = true;
    if (!filepath.empty())
    {
        auto& queries = output.queries;

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
            GWBUF total_buf;
            for (const auto& query : queries)
            {
                total_buf.append(protocol.make_query(query));
            }
            output.buffer_contents = std::move(total_buf);
        }
    }
    return file_ok;
}

Listener::SData Listener::create_test_data(const mxs::ConfigParameters& params)
{
    SListener listener {new Listener("test_listener")};
    listener->m_config.configure(params);
    mxs::ConfigParameters protocol_params;
    return listener->create_shared_data(protocol_params);
}

bool Listener::read_user_mapping(mxs::ListenerData::SMappingInfo& output) const
{
    using mxb::Json;
    auto& filepath = m_config.user_mapping_file;
    auto filepathc = filepath.c_str();
    bool rval = false;

    if (!filepath.empty())
    {
        Json all;
        if (all.load(filepath))
        {
            rval = true;
            auto result = std::make_unique<mxs::ListenerData::MappingInfo>();
            const char wrong_type[] = "Wrong object type in '%s'. %s";
            const char malformed_entry[] = "Malformed entry %i in '%s'-array in file '%s': %s";
            const char duplicate_key[] = "Read duplicate key '%s' from '%s'-array in file '%s'.";

            // User and group mappings are very similar, define helper function.
            using StringMap = std::unordered_map<std::string, std::string>;

            Json::ElemFailHandler elem_fail = [&](int ind, const char* arr_name, const char* msg) {
                MXB_ERROR(malformed_entry, ind + 1, arr_name, filepathc, msg);
            };

            auto parse_struct_arr = [&](const char* arr_key, const char* key1, const char* key2,
                                        StringMap& out) {
                bool success = true;
                if (all.contains(arr_key))
                {
                    const char strings_fmt[] = "{s:s, s:s}";
                    const char* val1 = nullptr;
                    const char* val2 = nullptr;
                    Json::ElemOkHandler elem_ok = [&](int ind, const char* arr_name) {
                        auto ret = out.emplace(val1, val2);
                        if (!ret.second)
                        {
                            MXB_WARNING(duplicate_key, val1, arr_name, filepathc);
                        }
                    };

                    if (!all.unpack_arr(arr_key, elem_ok, elem_fail, strings_fmt, key1, &val1, key2,
                                        &val2))
                    {
                        MXB_ERROR(wrong_type, arr_key, all.error_msg().c_str());
                        success = false;
                    }
                }
                return success;
            };

            if (!parse_struct_arr("user_map", "original_user", "mapped_user", result->user_map)
                || !parse_struct_arr("group_map", "original_group", "mapped_user", result->group_map))
            {
                rval = false;
            }

            // The credentials-array has three strings, with plugin being optional.
            const char arr_creds[] = "server_credentials";
            if (all.contains(arr_creds))
            {
                const char fmt[] = "{s:s, s:s, s?:s}";
                const char* val_mapped = nullptr;
                const char* val_pw = nullptr;
                const char* val_plugin = nullptr;
                Json::ElemOkHandler elem_ok = [&](int ind, const char* arr_name) {
                    ListenerData::UserCreds dest;
                    dest.password = mxs::decrypt_password(val_pw);
                    // "plugin" is an optional field and is left null when not set.
                    if (val_plugin)
                    {
                        dest.plugin = val_plugin;
                        val_plugin = nullptr;
                    }
                    auto ret = result->credentials.emplace(val_mapped, move(dest));
                    if (!ret.second)
                    {
                        MXB_WARNING(duplicate_key, val_mapped, arr_name, filepathc);
                    }
                };

                if (!all.unpack_arr(arr_creds, elem_ok, elem_fail, fmt, "mapped_user", &val_mapped,
                                    "plugin", &val_plugin, "password", &val_pw))
                {
                    MXB_ERROR(wrong_type, arr_creds, all.error_msg().c_str());
                    rval = false;
                }
            }

            if (rval)
            {
                MXB_NOTICE("Read %lu user map, %lu group map and %lu credential entries from '%s' for "
                           "listener '%s'.", result->user_map.size(), result->group_map.size(),
                           result->credentials.size(), filepathc, m_name.c_str());
                output = move(result);
            }
        }
        else
        {
            MXB_ERROR("Failed to load user mapping from file. %s", all.error_msg().c_str());
        }
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool Listener::read_proxy_networks(maxbase::proxy_protocol::SubnetArray& output)
{
    bool rval = false;
    auto parse_res = mxb::proxy_protocol::parse_networks_from_string(m_config.proxy_networks);
    if (parse_res.errmsg.empty())
    {
        output = std::move(parse_res.subnets);
        rval = true;
    }
    else
    {
        mxb_assert(!true);      // Validation should catch faulty setting.
    }
    return rval;
}
}
