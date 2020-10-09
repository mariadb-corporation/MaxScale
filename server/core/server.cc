/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
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

#include <maxbase/alloc.h>
#include <maxbase/atomic.hh>
#include <maxbase/format.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/log.hh>

#include <maxscale/config.hh>
#include <maxscale/config2.hh>
#include <maxscale/session.hh>
#include <maxscale/dcb.hh>
#include <maxscale/poll.hh>
#include <maxscale/ssl.hh>
#include <maxscale/paths.hh>
#include <maxscale/utils.h>
#include <maxscale/json_api.hh>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/routingworker.hh>

#include "internal/config.hh"

using maxbase::Worker;
using maxscale::RoutingWorker;

using std::string;
using Guard = std::lock_guard<std::mutex>;
using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace cfg = mxs::config;

const char CN_EXTRA_PORT[] = "extra_port";
const char CN_MONITORPW[] = "monitorpw";
const char CN_MONITORUSER[] = "monitoruser";
const char CN_PERSISTMAXTIME[] = "persistmaxtime";
const char CN_PERSISTPOOLMAX[] = "persistpoolmax";
const char CN_PRIORITY[] = "priority";
const char CN_PROXY_PROTOCOL[] = "proxy_protocol";

namespace
{

const char ERR_TOO_LONG_CONFIG_VALUE[] = "The new value for %s is too long. Maximum length is %i characters.";

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

class ServerSpec : public cfg::Specification
{
public:
    using cfg::Specification::Specification;

protected:

    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const
    {
        return do_post_validate(params);
    }

    bool post_validate(json_t* json) const
    {
        return do_post_validate(json);
    }
};

static const auto NO_QUOTES = cfg::ParamString::IGNORED;
static const auto AT_RUNTIME = cfg::Param::AT_RUNTIME;

static ServerSpec s_spec(CN_SERVERS, cfg::Specification::SERVER);

static cfg::ParamString s_type(&s_spec, CN_TYPE, "Object type", "server", NO_QUOTES);
static cfg::ParamString s_protocol(&s_spec, CN_PROTOCOL, "Server protocol (deprecated)", "", NO_QUOTES);
static cfg::ParamString s_authenticator(
    &s_spec, CN_AUTHENTICATOR, "Server authenticator (deprecated)", "", NO_QUOTES);

static cfg::ParamString s_address(&s_spec, CN_ADDRESS, "Server address", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamString s_socket(&s_spec, CN_SOCKET, "Server UNIX socket", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamCount s_port(&s_spec, CN_PORT, "Server port", 3306, AT_RUNTIME);
static cfg::ParamCount s_extra_port(&s_spec, CN_EXTRA_PORT, "Server extra port", 0, AT_RUNTIME);
static cfg::ParamCount s_priority(&s_spec, CN_PRIORITY, "Server priority", 0, AT_RUNTIME);
static cfg::ParamString s_monitoruser(&s_spec, CN_MONITORUSER, "Monitor user", "", NO_QUOTES, AT_RUNTIME);
static cfg::ParamString s_monitorpw(&s_spec, CN_MONITORPW, "Monitor password", "", NO_QUOTES, AT_RUNTIME);

static cfg::ParamCount s_persistpoolmax(
    &s_spec, CN_PERSISTPOOLMAX, "Maximum size of the persistent connection pool", 0, AT_RUNTIME);

static cfg::ParamSeconds s_persistmaxtime(
    &s_spec, CN_PERSISTMAXTIME, "Maximum time that a connection can be in the pool",
    cfg::INTERPRET_AS_SECONDS, 0s, AT_RUNTIME);

static cfg::ParamBool s_proxy_protocol(
    &s_spec, CN_PROXY_PROTOCOL, "Enable proxy protocol", false, AT_RUNTIME);

static Server::ParamDiskSpaceLimits s_disk_space_threshold(
    &s_spec, CN_DISK_SPACE_THRESHOLD, "Server disk space threshold");

static cfg::ParamEnum<int64_t> s_rank(
    &s_spec, CN_RANK, "Server rank",
    {
        {RANK_PRIMARY, "primary"},
        {RANK_SECONDARY, "secondary"}
    }, RANK_PRIMARY, AT_RUNTIME);

//
// TLS parameters
//

static cfg::ParamBool s_ssl(&s_spec, CN_SSL, "Enable TLS for server", false, AT_RUNTIME);

static cfg::ParamPath s_ssl_cert(
    &s_spec, CN_SSL_CERT, "TLS public certificate", cfg::ParamPath::R, "", AT_RUNTIME);
static cfg::ParamPath s_ssl_key(
    &s_spec, CN_SSL_KEY, "TLS private key", cfg::ParamPath::R, "", AT_RUNTIME);
static cfg::ParamPath s_ssl_ca(
    &s_spec, CN_SSL_CA_CERT, "TLS certificate authority", cfg::ParamPath::R, "", AT_RUNTIME);

static cfg::ParamEnum<mxb::ssl_version::Version> s_ssl_version(
    &s_spec, CN_SSL_VERSION, "Minimum TLS protocol version",
    {
        {mxb::ssl_version::SSL_TLS_MAX, "MAX"},
        {mxb::ssl_version::TLS10, "TLSv10"},
        {mxb::ssl_version::TLS11, "TLSv11"},
        {mxb::ssl_version::TLS12, "TLSv12"},
        {mxb::ssl_version::TLS13, "TLSv13"}
    }, mxb::ssl_version::SSL_TLS_MAX, AT_RUNTIME);

static cfg::ParamString s_ssl_cipher(&s_spec, CN_SSL_CIPHER, "TLS cipher list", "", NO_QUOTES, AT_RUNTIME);

static cfg::ParamCount s_ssl_cert_verify_depth(
    &s_spec, CN_SSL_CERT_VERIFY_DEPTH, "TLS certificate verification depth", 9, AT_RUNTIME);

static cfg::ParamBool s_ssl_verify_peer_certificate(
    &s_spec, CN_SSL_VERIFY_PEER_CERTIFICATE, "Verify TLS peer certificate", false, AT_RUNTIME);

static cfg::ParamBool s_ssl_verify_peer_host(
    &s_spec, CN_SSL_VERIFY_PEER_HOST, "Verify TLS peer host", false, AT_RUNTIME);

template<class Params>
bool ServerSpec::do_post_validate(Params params) const
{
    bool rval = true;
    auto monuser = s_monitoruser.get(params);
    auto monpw = s_monitorpw.get(params);

    if (monuser.empty() != monpw.empty())
    {
        MXS_ERROR("If '%s is defined, '%s' must also be defined.",
                  !monuser.empty() ? CN_MONITORUSER : CN_MONITORPW,
                  !monuser.empty() ? CN_MONITORPW : CN_MONITORUSER);
        rval = false;
    }

    if (monuser.length() > Server::MAX_MONUSER_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORUSER, Server::MAX_MONUSER_LEN);
        rval = false;
    }

    if (monpw.length() > Server::MAX_MONPW_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_MONITORPW, Server::MAX_MONPW_LEN);
        rval = false;
    }

    auto address = s_address.get(params);
    auto socket = s_socket.get(params);
    bool have_address = !address.empty();
    bool have_socket = !socket.empty();
    auto addr = have_address ? address : socket;

    if (have_socket && have_address)
    {
        MXS_ERROR("Both '%s=%s' and '%s=%s' defined: only one of the parameters can be defined",
                  CN_ADDRESS, address.c_str(), CN_SOCKET, socket.c_str());
        rval = false;
    }
    else if (!have_address && !have_socket)
    {
        MXS_ERROR("Missing a required parameter: either '%s' or '%s' must be defined",
                  CN_ADDRESS, CN_SOCKET);
        rval = false;
    }
    else if (have_address && addr[0] == '/')
    {
        MXS_ERROR("The '%s' parameter is not a valid IP or hostname", CN_ADDRESS);
        rval = false;
    }
    else if (addr.length() > Server::MAX_ADDRESS_LEN)
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, have_address ? CN_ADDRESS : CN_SOCKET, Server::MAX_ADDRESS_LEN);
        rval = false;
    }

    if (s_ssl.get(params) && s_ssl_cert.get(params).empty() != s_ssl_key.get(params).empty())
    {
        MXS_ERROR("Both '%s' and '%s' must be defined", s_ssl_cert.name().c_str(), s_ssl_key.name().c_str());
        rval = false;
    }

    return rval;
}

std::pair<bool, std::unique_ptr<mxs::SSLContext>> create_ssl(const char* name,
                                                             const mxs::ConfigParameters& params)
{
    bool ok = true;
    auto ssl = std::make_unique<mxs::SSLContext>();

    if (!ssl->read_configuration(name, params, false))
    {
        MXS_ERROR("Unable to initialize SSL for server '%s'", name);
        ok = false;
        ssl.reset();
    }
    else if (!ssl->valid())
    {
        // An empty ssl config should result in an empty pointer. This can be removed if Server stores
        // SSLContext as value.
        ssl.reset();
    }

    return {ok, std::move(ssl)};
}
}

Server::ParamDiskSpaceLimits::ParamDiskSpaceLimits(cfg::Specification* pSpecification,
                                                   const char* zName, const char* zDescription)
    : cfg::ConcreteParam<ParamDiskSpaceLimits, DiskSpaceLimits>(
        pSpecification, zName, zDescription, AT_RUNTIME, OPTIONAL, MXS_MODULE_PARAM_STRING, value_type())
{
}

std::string Server::ParamDiskSpaceLimits::type() const
{
    return "disk_space_limits";
}

std::string Server::ParamDiskSpaceLimits::to_string(Server::ParamDiskSpaceLimits::value_type value) const
{
    std::vector<std::string> tmp;
    std::transform(value.begin(), value.end(), std::back_inserter(tmp),
                   [](const auto& a) {
                       return a.first + ':' + std::to_string(a.second);
                   });
    return mxb::join(tmp, ",");
}

bool Server::ParamDiskSpaceLimits::from_string(const std::string& value, value_type* pValue,
                                               std::string* pMessage) const
{
    return config_parse_disk_space_threshold(pValue, value.c_str());
}

json_t* Server::ParamDiskSpaceLimits::to_json(value_type value) const
{
    json_t* obj = value.empty() ? json_null() : json_object();

    for (const auto& a : value)
    {
        json_object_set_new(obj, a.first.c_str(), json_integer(a.second));
    }

    return obj;
}

bool Server::ParamDiskSpaceLimits::from_json(const json_t* pJson, value_type* pValue,
                                             std::string* pMessage) const
{
    bool ok = false;

    if (json_is_object(pJson))
    {
        ok = true;
        const char* key;
        json_t* value;
        value_type newval;

        json_object_foreach(const_cast<json_t*>(pJson), key, value)
        {
            if (json_is_integer(value))
            {
                newval[key] = json_integer_value(value);
            }
            else
            {
                ok = false;
                *pMessage = "'"s + key + "' is not a JSON number.";
                break;
            }
        }
    }
    else if (json_is_string(pJson))
    {
        // Allow conversion from the INI format string to make it easier to configure this via maxctrl:
        // defining JSON objects with it is not very convenient.
        ok = from_string(json_string_value(pJson), pValue, pMessage);
    }
    else if (json_is_null(pJson))
    {
        ok = true;
    }
    else
    {
        *pMessage = "Not a JSON object or JSON null.";
    }

    return ok;
}

bool Server::configure(const mxs::ConfigParameters& params)
{
    return m_settings.configure(params) && post_configure();
}

bool Server::configure(json_t* params)
{
    return m_settings.configure(params) && post_configure();
}

Server::Settings::Settings(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
    , m_type(this, &s_type)
    , m_protocol(this, &s_protocol)
    , m_authenticator(this, &s_authenticator)
    , m_address(this, &s_address)
    , m_socket(this, &s_socket)
    , m_port(this, &s_port)
    , m_extra_port(this, &s_extra_port)
    , m_priority(this, &s_priority)
    , m_monitoruser(this, &s_monitoruser)
    , m_monitorpw(this, &s_monitorpw)
    , m_persistpoolmax(this, &s_persistpoolmax)
    , m_persistmaxtime(this, &s_persistmaxtime)
    , m_proxy_protocol(this, &s_proxy_protocol)
    , m_disk_space_threshold(this, &s_disk_space_threshold)
    , m_rank(this, &s_rank)
    , m_ssl(this, &s_ssl)
    , m_ssl_cert(this, &s_ssl_cert)
    , m_ssl_key(this, &s_ssl_key)
    , m_ssl_ca(this, &s_ssl_ca)
    , m_ssl_version(this, &s_ssl_version)
    , m_ssl_cert_verify_depth(this, &s_ssl_cert_verify_depth)
    , m_ssl_verify_peer_certificate(this, &s_ssl_verify_peer_certificate)
    , m_ssl_verify_peer_host(this, &s_ssl_verify_peer_host)
    , m_ssl_cipher(this, &s_ssl_cipher)
{
}

bool Server::Settings::post_configure()
{
    auto addr = !m_address.get().empty() ? m_address.get() : m_socket.get();

    careful_strcpy(address, MAX_ADDRESS_LEN, addr);
    careful_strcpy(monuser, MAX_MONUSER_LEN, m_monitoruser.get());
    careful_strcpy(monpw, MAX_MONPW_LEN, m_monitorpw.get());

    m_have_disk_space_limits.store(!m_disk_space_threshold.get().empty());

    return true;
}

// static
const cfg::Specification& Server::specification()
{
    return s_spec;
}

std::unique_ptr<Server> Server::create(const char* name, const mxs::ConfigParameters& params)
{
    std::unique_ptr<Server> rval;

    if (s_spec.validate(params))
    {
        if (auto server = std::make_unique<Server>(name))
        {
            if (server->configure(params))
            {
                rval = std::move(server);
            }
        }
    }

    return rval;
}

std::unique_ptr<Server> Server::create(const char* name, json_t* json)
{
    std::unique_ptr<Server> rval;

    if (s_spec.validate(json))
    {
        if (auto server = std::make_unique<Server>(name))
        {
            if (server->configure(json))
            {
                rval = std::move(server);
            }
        }
    }

    return rval;
}

Server* Server::create_test_server()
{
    static int next_id = 1;
    string name = "TestServer" + std::to_string(next_id++);
    return new Server(name);
}

void Server::cleanup_persistent_connections() const
{
    RoutingWorker::execute_concurrently(
        [this]() {
            RoutingWorker::get_current()->evict_dcbs(this, RoutingWorker::Evict::EXPIRED);
        });
}

uint64_t Server::status() const
{
    return m_status;
}

void Server::set_status(uint64_t bit)
{
    m_status |= bit;
}

void Server::clear_status(uint64_t bit)
{
    m_status &= ~bit;
}

void Server::assign_status(uint64_t status)
{
    m_status = status;
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

bool Server::set_address(const string& new_address)
{
    bool rval = false;
    if (new_address.length() <= MAX_ADDRESS_LEN)
    {
        careful_strcpy(m_settings.address, MAX_ADDRESS_LEN, new_address);
        rval = true;
    }
    else
    {
        MXS_ERROR(ERR_TOO_LONG_CONFIG_VALUE, CN_ADDRESS, MAX_ADDRESS_LEN);
    }
    return rval;
}

void Server::set_port(int new_port)
{
    m_settings.m_port.set(new_port);
}

void Server::set_extra_port(int new_port)
{
    m_settings.m_extra_port.set(new_port);
}

std::shared_ptr<mxs::SSLContext> Server::ssl() const
{
    return *m_ssl_ctx;
}

mxs::SSLConfig Server::ssl_config() const
{
    std::lock_guard<std::mutex> guard(m_ssl_lock);
    return m_ssl_config;
}

bool Server::proxy_protocol() const
{
    return m_settings.m_proxy_protocol.get();
}

void Server::set_proxy_protocol(bool proxy_protocol)
{
    m_settings.m_proxy_protocol.set(proxy_protocol);
}

uint8_t Server::charset() const
{
    return m_charset;
}

void Server::set_charset(uint8_t charset)
{
    m_charset = charset;
}

Server::PoolStats& Server::pool_stats()
{
    return m_pool_stats;
}
void Server::set_variables(std::unordered_map<std::string, std::string>&& variables)
{
    std::lock_guard<std::mutex> guard(m_var_lock);
    m_variables = variables;
}

std::string Server::get_variable(const std::string& key) const
{
    std::lock_guard<std::mutex> guard(m_var_lock);
    auto it = m_variables.find(key);
    return it == m_variables.end() ? "" : it->second;
}

uint64_t Server::status_from_string(const char* str)
{
    static std::vector<std::pair<const char*, uint64_t>> status_bits =
    {
        {"running",      SERVER_RUNNING },
        {"master",       SERVER_MASTER  },
        {"slave",        SERVER_SLAVE   },
        {"synced",       SERVER_JOINED  },
        {"maintenance",  SERVER_MAINT   },
        {"maint",        SERVER_MAINT   },
        {"drain",        SERVER_DRAINING},
        {"blr",          SERVER_BLR     },
        {"binlogrouter", SERVER_BLR     }
    };

    for (const auto& a : status_bits)
    {
        if (strcasecmp(str, a.first) == 0)
        {
            return a.second;
        }
    }

    return 0;
}

void Server::set_gtid_list(const std::vector<std::pair<uint32_t, uint64_t>>& domains)
{
    mxs::MainWorker::get()->execute(
        [this, domains]() {
            auto gtids = *m_gtids;

            for (const auto& p : domains)
            {
                gtids[p.first] = p.second;
            }

            m_gtids.assign(gtids);
        }, mxb::Worker::EXECUTE_AUTO);
}

void Server::clear_gtid_list()
{
    mxs::MainWorker::get()->execute(
        [this]() {
            m_gtids->clear();
            m_gtids.assign(*m_gtids);
        }, mxb::Worker::EXECUTE_AUTO);
}

uint64_t Server::gtid_pos(uint32_t domain) const
{
    const auto& gtids = *m_gtids;
    auto it = gtids.find(domain);
    return it != gtids.end() ? it->second : 0;
}

void Server::set_version(uint64_t version_num, const std::string& version_str)
{
    if (version_str != m_info.version_string())
    {
        MXS_NOTICE("Server '%s' version: %s", name(), version_str.c_str());
    }

    m_info.set(version_num, version_str);
}

json_t* Server::json_attributes() const
{
    /** Resource attributes */
    json_t* attr = json_object();

    /** Store server parameters in attributes */
    json_t* params = json_object();
    m_settings.fill(params);

    // Return either address/port or socket, not both
    auto socket = json_object_get(params, CN_SOCKET);

    if (socket && !json_is_null(socket))
    {
        mxb_assert(json_is_string(socket));
        json_object_set_new(params, CN_ADDRESS, json_null());
        json_object_set_new(params, CN_PORT, json_null());
    }
    else
    {
        json_object_set_new(params, CN_SOCKET, json_null());
    }

    // Remove unwanted parameters
    json_object_del(params, CN_TYPE);
    json_object_del(params, CN_AUTHENTICATOR);
    json_object_del(params, CN_PROTOCOL);

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    string stat = status_string();
    json_object_set_new(attr, CN_STATE, json_string(stat.c_str()));

    json_object_set_new(attr, CN_VERSION_STRING, json_string(m_info.version_string()));
    json_object_set_new(attr, "replication_lag", json_integer(replication_lag()));

    cleanup_persistent_connections();

    json_t* statistics = stats().to_json();
    json_object_set_new(statistics, "persistent_connections", json_integer(m_pool_stats.n_persistent));
    json_object_set_new(statistics, "max_pool_size", json_integer(m_pool_stats.persistmax));
    json_object_set_new(statistics, "reused_connections", json_integer(m_pool_stats.n_from_pool));
    json_object_set_new(statistics, "connection_pool_empty", json_integer(m_pool_stats.n_new_conn));
    maxbase::Duration response_ave(mxb::from_secs(response_time_average()));
    json_object_set_new(statistics, "adaptive_avg_select_time",
                        json_string(mxb::to_string(response_ave).c_str()));

    json_object_set_new(attr, "statistics", statistics);
    return attr;
}

json_t* Server::to_json_data(const char* host) const
{
    json_t* rval = json_object();

    /** Add resource identifiers */
    json_object_set_new(rval, CN_ID, json_string(name()));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVERS));

    /** Attributes */
    json_object_set_new(rval, CN_ATTRIBUTES, json_attributes());
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVERS, name()));

    return rval;
}

bool Server::post_configure()
{
    json_t* js = m_settings.to_json();
    auto params = mxs::ConfigParameters::from_json(js);
    json_decref(js);

    bool ok;
    std::shared_ptr<mxs::SSLContext> ctx;
    std::tie(ok, ctx) = create_ssl(m_name.c_str(), params);

    if (ok)
    {
        m_ssl_ctx.assign(ctx);
        std::lock_guard<std::mutex> guard(m_ssl_lock);
        m_ssl_config = ctx ? ctx->config() : mxs::SSLConfig();
    }

    return ok;
}

void Server::VersionInfo::set(uint64_t version, const std::string& version_str)
{
    uint32_t major = version / 10000;
    uint32_t minor = (version - major * 10000) / 100;
    uint32_t patch = version - major * 10000 - minor * 100;

    Type new_type = Type::UNKNOWN;
    auto version_strz = version_str.c_str();
    if (strcasestr(version_strz, "clustrix"))
    {
        new_type = Type::CLUSTRIX;
    }
    else if (strcasestr(version_strz, "binlogrouter"))
    {
        new_type = Type::BLR;
    }
    else if (strcasestr(version_strz, "mariadb"))
    {
        // Needs to be after Clustrix and BLR as their version strings may include "mariadb".
        new_type = Type::MARIADB;
    }
    else if (!version_str.empty())
    {
        new_type = Type::MYSQL;     // Used for any unrecognized server types.
    }

    /* This only protects against concurrent writing which could result in garbled values. Reads are not
     * synchronized. Since writing is rare, this is an unlikely issue. Readers should be prepared to
     * sometimes get inconsistent values. */
    Guard lock(m_lock);

    m_type = new_type;
    m_version_num.total = version;
    m_version_num.major = major;
    m_version_num.minor = minor;
    m_version_num.patch = patch;
    careful_strcpy(m_version_str, MAX_VERSION_LEN, version_str);
}

const Server::VersionInfo::Version& Server::VersionInfo::version_num() const
{
    return m_version_num;
}

Server::VersionInfo::Type Server::VersionInfo::type() const
{
    return m_type;
}

const char* Server::VersionInfo::version_string() const
{
    return m_version_str;
}

bool SERVER::VersionInfo::is_database() const
{
    auto t = m_type;
    return t == Type::MARIADB || t == Type::CLUSTRIX || t == Type::MYSQL;
}

const SERVER::VersionInfo& Server::info() const
{
    return m_info;
}

ServerEndpoint::ServerEndpoint(mxs::Component* up, MXS_SESSION* session, Server* server)
    : m_up(up)
    , m_session(session)
    , m_server(server)
{
}

ServerEndpoint::~ServerEndpoint()
{
    if (is_open())
    {
        close();
    }
}

mxs::Target* ServerEndpoint::target() const
{
    return m_server;
}

bool ServerEndpoint::connect()
{
    mxb_assert(!is_open());
    mxb::LogScope scope(m_server->name());
    auto worker = mxs::RoutingWorker::get_current();

    if ((m_dcb = worker->get_backend_dcb(m_server, m_session, this)))
    {
        m_server->stats().add_connection();
        m_conn = m_dcb->protocol();
    }

    return m_dcb != nullptr;
}

void ServerEndpoint::close()
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    DCB::close(m_dcb);
    m_dcb = nullptr;

    m_server->stats().remove_connection();
}

bool ServerEndpoint::is_open() const
{
    return m_dcb;
}

int32_t ServerEndpoint::routeQuery(GWBUF* buffer)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    return m_conn->write(buffer);
}

int32_t ServerEndpoint::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    down.push_back(this);
    return m_up->clientReply(buffer, down, reply);
}

bool ServerEndpoint::handleError(mxs::ErrorType type, GWBUF* error,
                                 mxs::Endpoint* down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    mxb_assert(is_open());
    return m_up->handleError(type, error, this, reply);
}

std::unique_ptr<mxs::Endpoint> Server::get_connection(mxs::Component* up, MXS_SESSION* session)
{
    return std::unique_ptr<mxs::Endpoint>(new ServerEndpoint(up, session, this));
}
