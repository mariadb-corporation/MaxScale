/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
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
#include <maxscale/paths.h>
#include <maxscale/utils.h>
#include <maxscale/json_api.hh>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/maxscale.h>
#include <maxscale/monitor.hh>
#include <maxscale/routingworker.hh>

#include "internal/poll.hh"
#include "internal/config.hh"
#include "internal/modules.hh"

using maxbase::Worker;
using maxscale::RoutingWorker;
using maxscale::Monitor;

using std::string;
using Guard = std::lock_guard<std::mutex>;
using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace cfg = mxs::config;

const char CN_MONITORPW[] = "monitorpw";
const char CN_MONITORUSER[] = "monitoruser";
const char CN_PERSISTMAXTIME[] = "persistmaxtime";
const char CN_PERSISTPOOLMAX[] = "persistpoolmax";
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

static ServerSpec s_spec("server", cfg::Specification::SERVER);

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
// TLS parameters, only configurable at server creation time
//

static cfg::ParamEnum<bool> s_ssl(      // TODO: Deprecate the non-boolean values
    &s_spec, CN_SSL, "Enable TLS for server",
    {
        {true, "required"},
        {true, "true"},
        {true, "yes"},
        {true, "on"},
        {true, "1"},
        {false, "disabled"},
        {false, "false"},
        {false, "no"},
        {false, "off"},
        {false, "0"},
    }, false);

static cfg::ParamPath s_ssl_cert(&s_spec, CN_SSL_CERT, "TLS public certificate", cfg::ParamPath::R, "");
static cfg::ParamPath s_ssl_key(&s_spec, CN_SSL_KEY, "TLS private key", cfg::ParamPath::R, "");
static cfg::ParamPath s_ssl_ca(&s_spec, CN_SSL_CA_CERT, "TLS certificate authority", cfg::ParamPath::R, "");

static cfg::ParamEnum<ssl_method_type_t> s_ssl_version(
    &s_spec, CN_SSL_VERSION, "Minimum TLS protocol version",
    {
        {SERVICE_SSL_TLS_MAX, "MAX"},
        {SERVICE_TLS10, "TLSv10"},
        {SERVICE_TLS11, "TLSv11"},
        {SERVICE_TLS12, "TLSv12"},
        {SERVICE_TLS13, "TLSv13"}
    }, SERVICE_SSL_TLS_MAX);


static cfg::ParamCount s_ssl_cert_verify_depth(
    &s_spec, CN_SSL_CERT_VERIFY_DEPTH, "TLS certificate verification depth", 9);

static cfg::ParamBool s_ssl_verify_peer_certificate(
    &s_spec, CN_SSL_VERIFY_PEER_CERTIFICATE, "Verify TLS peer certificate", false);

static cfg::ParamBool s_ssl_verify_peer_host(
    &s_spec, CN_SSL_VERIFY_PEER_HOST, "Verify TLS peer host", false);

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

void Server::configure(const mxs::ConfigParameters& params)
{
    m_settings.configure(params);
}

void Server::configure(json_t* params)
{
    m_settings.configure(params);
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
cfg::Specification* Server::specification()
{
    return &s_spec;
}

Server* Server::create(const char* name, const mxs::ConfigParameters& params)
{
    Server* rval = nullptr;

    if (s_spec.validate(params))
    {
        auto ssl = create_ssl(name, params);

        if (ssl.first)
        {
            if (auto server = std::make_unique<Server>(name, std::move(ssl.second)))
            {
                server->configure(params);
                rval = server.release();
            }
        }
    }

    return rval;
}

Server* Server::create(const char* name, json_t* json)
{
    Server* rval = nullptr;

    if (s_spec.validate(json))
    {
        auto ssl = create_ssl(name, mxs::ConfigParameters::from_json(json));

        if (ssl.first)
        {
            if (auto server = std::make_unique<Server>(name, std::move(ssl.second)))
            {
                server->configure(json);
                rval = server.release();
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

const mxs::SSLProvider& Server::ssl() const
{
    return m_ssl_provider;
}

mxs::SSLProvider& Server::ssl()
{
    return m_ssl_provider;
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
        {"running",     SERVER_RUNNING   },
        {"master",      SERVER_MASTER    },
        {"slave",       SERVER_SLAVE     },
        {"synced",      SERVER_JOINED    },
        {"maintenance", SERVER_MAINT     },
        {"maint",       SERVER_MAINT     },
        {"stale",       SERVER_WAS_MASTER},
        {"drain",       SERVER_DRAINING  }
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
    if (version_str != version_string())
    {
        MXS_NOTICE("Server '%s' version: %s", name(), version_str.c_str());
    }

    m_info.set(version_num, version_str);
}

/**
 * Creates a server configuration at the location pointed by @c filename
 *
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
bool Server::create_server_config(const char* filename) const
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing server '%s': %d, %s",
                  filename, name(), errno, mxs_strerror(errno));
        return false;
    }

    std::ostringstream ss;
    m_settings.persist(ss);

    if (dprintf(file, "%s", ss.str().c_str()) == -1)
    {
        MXS_ERROR("Could not write serialized configuration to file '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    close(file);
    return true;
}

bool Server::serialize() const
{
    bool rval = false;
    string final_filename = mxb::string_printf("%s/%s.cnf", get_config_persistdir(), name());
    string temp_filename = final_filename + ".tmp";
    auto zTempFilename = temp_filename.c_str();

    if (unlink(zTempFilename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary server configuration at '%s': %d, %s",
                  zTempFilename, errno, mxs_strerror(errno));
    }
    else if (create_server_config(zTempFilename))
    {
        if (rename(zTempFilename, final_filename.c_str()) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary server configuration at '%s': %d, %s",
                      zTempFilename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

json_t* Server::json_attributes() const
{
    /** Resource attributes */
    json_t* attr = json_object();

    /** Store server parameters in attributes */
    json_t* params = json_object();
    m_settings.fill(params);

    // Remove unwanted parameters
    json_object_del(params, CN_TYPE);
    json_object_del(params, CN_AUTHENTICATOR);
    json_object_del(params, CN_PROTOCOL);

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    string stat = status_string();
    json_object_set_new(attr, CN_STATE, json_string(stat.c_str()));

    json_object_set_new(attr, CN_VERSION_STRING, json_string(version_string().c_str()));
    json_object_set_new(attr, "replication_lag", json_integer(replication_lag()));

    cleanup_persistent_connections();

    json_t* statistics = stats().to_json();
    json_object_set_new(statistics, "persistent_connections", json_integer(m_pool_stats.n_persistent));
    maxbase::Duration response_ave(response_time_average());
    json_object_set_new(statistics, "adaptive_avg_select_time", json_string(to_string(response_ave).c_str()));

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

const MXS_MODULE_PARAM* common_server_params()
{
    static const MXS_MODULE_PARAM config_server_params[] =
    {
        {CN_TYPE,           MXS_MODULE_PARAM_STRING,   CN_SERVER, MXS_MODULE_OPT_REQUIRED  },
        {CN_ADDRESS,        MXS_MODULE_PARAM_STRING},
        {CN_SOCKET,         MXS_MODULE_PARAM_STRING},
        {CN_PROTOCOL,       MXS_MODULE_PARAM_STRING,   NULL,      MXS_MODULE_OPT_DEPRECATED},
        {CN_PORT,           MXS_MODULE_PARAM_COUNT,    "3306"},
        {CN_EXTRA_PORT,     MXS_MODULE_PARAM_COUNT,    "0"},
        {CN_AUTHENTICATOR,  MXS_MODULE_PARAM_STRING,   NULL,      MXS_MODULE_OPT_DEPRECATED},
        {CN_MONITORUSER,    MXS_MODULE_PARAM_STRING},
        {CN_MONITORPW,      MXS_MODULE_PARAM_PASSWORD},
        {CN_PERSISTPOOLMAX, MXS_MODULE_PARAM_COUNT,    "0"},
        {CN_PERSISTMAXTIME, MXS_MODULE_PARAM_DURATION, "0",       MXS_MODULE_OPT_DURATION_S},
        {CN_PROXY_PROTOCOL, MXS_MODULE_PARAM_BOOL,     "false"},
        {CN_PRIORITY,       MXS_MODULE_PARAM_COUNT,    "0"},
        {
            CN_SSL, MXS_MODULE_PARAM_ENUM, "false", MXS_MODULE_OPT_ENUM_UNIQUE, ssl_setting_values()
        },
        {CN_SSL_CERT,       MXS_MODULE_PARAM_PATH,     NULL,      MXS_MODULE_OPT_PATH_R_OK },
        {CN_SSL_KEY,        MXS_MODULE_PARAM_PATH,     NULL,      MXS_MODULE_OPT_PATH_R_OK },
        {CN_SSL_CA_CERT,    MXS_MODULE_PARAM_PATH,     NULL,      MXS_MODULE_OPT_PATH_R_OK },
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
            CN_DISK_SPACE_THRESHOLD, MXS_MODULE_PARAM_STRING
        },
        {
            CN_RANK, MXS_MODULE_PARAM_ENUM, DEFAULT_RANK, MXS_MODULE_OPT_ENUM_UNIQUE, rank_values
        },
        {NULL}
    };
    return config_server_params;
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
    mxb::LogScope scope(m_server->name());
    auto worker = mxs::RoutingWorker::get_current();

    if ((m_dcb = worker->get_backend_dcb(m_server, m_session, this)))
    {
        m_server->stats().add_connection();
    }

    return m_dcb != nullptr;
}

void ServerEndpoint::close()
{
    mxb::LogScope scope(m_server->name());
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
    return m_dcb->protocol_write(buffer);
}

int32_t ServerEndpoint::clientReply(GWBUF* buffer, mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    down.push_back(this);
    return m_up->clientReply(buffer, down, reply);
}

bool ServerEndpoint::handleError(mxs::ErrorType type, GWBUF* error,
                                 mxs::Endpoint* down, const mxs::Reply& reply)
{
    mxb::LogScope scope(m_server->name());
    return m_up->handleError(type, error, this, reply);
}

std::unique_ptr<mxs::Endpoint> Server::get_connection(mxs::Component* up, MXS_SESSION* session)
{
    return std::unique_ptr<mxs::Endpoint>(new ServerEndpoint(up, session, this));
}
