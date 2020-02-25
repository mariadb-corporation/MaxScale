/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-02-10
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
}

Server* Server::server_alloc(const char* name, const mxs::ConfigParameters& params)
{
    auto monuser = params.get_string(CN_MONITORUSER);
    auto monpw = params.get_string(CN_MONITORPW);

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

    std::unique_ptr<mxs::SSLContext> ssl(new mxs::SSLContext);
    if (!ssl || !ssl->read_configuration(name, params, false))
    {
        MXS_ERROR("Unable to initialize SSL for server '%s'", name);
        return NULL;
    }
    // An empty ssl config should result in an empty pointer. This can be removed if Server stores SSLContext
    // as value.
    if (!ssl->valid())
    {
        ssl = nullptr;
    }

    Server* server = new(std::nothrow) Server(name, std::move(ssl));
    BackendDCB** persistent = (BackendDCB**)MXS_CALLOC(config_threadcount(), sizeof(*persistent));

    if (!server || !persistent)
    {
        delete server;
        MXS_FREE(persistent);
        return NULL;
    }

    auto address = params.contains(CN_ADDRESS) ?
        params.get_string(CN_ADDRESS) : params.get_string(CN_SOCKET);

    careful_strcpy(server->address, MAX_ADDRESS_LEN, address.c_str());
    if (address.length() > MAX_ADDRESS_LEN)
    {
        MXS_WARNING("Truncated server address '%s' to the maximum size of %i characters.",
                    address.c_str(), MAX_ADDRESS_LEN);
    }

    server->port = params.get_integer(CN_PORT);
    server->extra_port = params.get_integer(CN_EXTRA_PORT);
    server->m_settings.persistpoolmax = params.get_integer(CN_PERSISTPOOLMAX);
    server->m_settings.persistmaxtime = params.get_duration<std::chrono::seconds>(CN_PERSISTMAXTIME).count();
    server->proxy_protocol = params.get_bool(CN_PROXY_PROTOCOL);
    server->is_active = true;
    server->persistent = persistent;
    server->m_settings.rank = params.get_enum(CN_RANK, rank_values);
    mxb_assert(server->m_settings.rank > 0);

    if (!monuser.empty())
    {
        mxb_assert(!monpw.empty());
        server->set_monitor_user(monuser);
        server->set_monitor_password(monpw);
    }

    server->m_settings.all_parameters = params;
    for (auto p : params)
    {
        const string& param_name = p.first;
        const string& param_value = p.second;
        if (server->is_custom_parameter(param_name))
        {
            server->set_custom_parameter(param_name, param_value);
        }
    }

    return server;
}

Server::~Server()
{
    MXS_FREE(persistent);
}

Server* Server::create_test_server()
{
    static int next_id = 1;
    string name = "TestServer" + std::to_string(next_id++);
    return new Server(name);
}

void Server::printServer()
{
    printf("Server %p\n", this);
    printf("\tServer:                       %s\n", address);
    printf("\tPort:                         %d\n", port);
    printf("\tTotal connections:            %d\n", stats().n_connections);
    printf("\tCurrent connections:          %d\n", stats().n_current);
    printf("\tPersistent connections:       %d\n", pool_stats.n_persistent);
    printf("\tPersistent actual max:        %d\n", persistmax);
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

        rworker.evict_dcbs(const_cast<Server*>(m_server), RoutingWorker::Evict::EXPIRED);
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

void SERVER::set_status(uint64_t bit)
{
    m_status |= bit;

    /** clear error logged flag before the next failure */
    if (is_master())
    {
        master_err_is_logged = false;
    }
}

void SERVER::clear_status(uint64_t bit)
{
    m_status &= ~bit;
}

int64_t SERVER::ping() const
{
    return m_ping.load(std::memory_order_relaxed);
}

void SERVER::set_ping(int64_t ping)
{
    m_ping.store(ping, std::memory_order_relaxed);
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

void Server::set_custom_parameter(const string& name, const string& value)
{
    // Set/add the parameter in both containers.
    m_settings.all_parameters.set(name, value);
    Guard guard(m_settings.lock);
    m_settings.custom_parameters.set(name, value);
}

string Server::get_custom_parameter(const string& name) const
{
    Guard guard(m_settings.lock);
    return m_settings.custom_parameters.get_string(name);
}

void Server::set_normal_parameter(const std::string& name, const std::string& value)
{
    m_settings.all_parameters.set(name, value);
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

void SERVER::set_gtid_pos(uint32_t domain, uint64_t sequence)
{
    // Try to find the existing slot or reserve a free one for this domain
    for (auto& a : m_gtids)
    {
        auto d = a.domain.load(std::memory_order_relaxed);
        int64_t empty = -1;

        // Use this slot if it's already assigned for this domain. If it's not assigned, try to reserve it.
        // Since all threads iterate the list in the same order, we're guaranteed to only reserve one slot per
        // domain.
        if (d == domain || a.domain.compare_exchange_strong(empty, domain, std::memory_order_relaxed))
        {
            uint64_t current;
            do
            {
                current = a.sequence.load(std::memory_order_relaxed);

                if (sequence <= current)
                {
                    // Someone else stored a newer GTID position
                    break;
                }
            }
            while (!a.sequence.compare_exchange_weak(current, sequence,
                                                     std::memory_order_release,
                                                     std::memory_order_relaxed));

            return;
        }
    }
}

uint64_t SERVER::gtid_pos(uint32_t domain) const
{
    for (const auto& a : m_gtids)
    {
        auto seq = a.sequence.load(std::memory_order_acquire);
        auto dom = a.domain.load(std::memory_order_relaxed);

        if (dom == domain)
        {
            return seq;
        }
        else if (dom == -1)
        {
            // Found an empty slot. This means that no GTIDs have been stored for this domain.
            break;
        }
    }

    return 0;
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

    string config = generate_config_string(name(), m_settings.all_parameters, common_server_params(),
                                           nullptr);

    // Print custom parameters. The generate_config_string()-call doesn't print them.
    {
        Guard guard(m_settings.lock);
        for (const auto& elem : m_settings.custom_parameters)
        {
            config += elem.first + "=" + elem.second + "\n";
        }
    }


    if (dprintf(file, "%s", config.c_str()) == -1)
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

    config_add_module_params_json(
        &m_settings.all_parameters, {CN_TYPE}, common_server_params(),
        nullptr,    // no module-specific parameters
        params);

    // Add weighting parameters that weren't added by config_add_module_params_json
    {
        Guard guard(m_settings.lock);
        for (const auto& elem : m_settings.custom_parameters)
        {
            if (!json_object_get(params, elem.first.c_str()))
            {
                json_object_set_new(params, elem.first.c_str(), json_string(elem.second.c_str()));
            }
        }
    }

    json_object_set_new(attr, CN_PARAMETERS, params);

    /** Store general information about the server state */
    string stat = status_string();
    json_object_set_new(attr, CN_STATE, json_string(stat.c_str()));

    json_object_set_new(attr, CN_VERSION_STRING, json_string(version_string().c_str()));

    if (rlag >= 0)
    {
        json_object_set_new(attr, "replication_lag", json_integer(rlag));
    }

    if (node_ts > 0)
    {
        struct tm result;
        char timebuf[30];
        time_t tim = node_ts;
        asctime_r(localtime_r(&tim, &result), timebuf);
        mxb::trim(timebuf);

        json_object_set_new(attr, "last_heartbeat", json_string(timebuf));
    }

    /** Store statistics */

    cleanup_persistent_connections(this);

    json_t* statistics = stats().to_json();
    json_object_set_new(statistics, "persistent_connections", json_integer(pool_stats.n_persistent));
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

bool Server::is_custom_parameter(const string& name) const
{
    auto server_params = common_server_params();
    for (int i = 0; server_params[i].name; i++)
    {
        if (server_params[i].name == name)
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
