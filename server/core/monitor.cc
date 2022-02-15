/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file monitor.c  - The monitor module management routines
 */
#include <maxscale/monitor.hh>

#include <atomic>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <string>
#include <sstream>
#include <set>
#include <zlib.h>
#include <sys/stat.h>
#include <vector>
#include <mutex>
#include <mysql.h>
#include <mysqld_error.h>

#include <maxbase/alloc.h>
#include <maxbase/format.hh>
#include <maxscale/clock.h>
#include <maxscale/http.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mariadb.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/maxscale.h>
#include <maxscale/mysql_utils.hh>
#include <maxscale/paths.hh>
#include <maxscale/secrets.hh>
#include <maxscale/utils.hh>

#include "internal/config.hh"
#include "internal/externcmd.hh"
#include "internal/monitor.hh"
#include "internal/modules.hh"
#include "internal/server.hh"
#include "internal/service.hh"

/** Schema version, journals must have a matching version */
#define MMB_SCHEMA_VERSION 2

/** Constants for byte lengths of the values */
#define MMB_LEN_BYTES          4
#define MMB_LEN_SCHEMA_VERSION 1
#define MMB_LEN_CRC32          4
#define MMB_LEN_VALUE_TYPE     1
#define MMB_LEN_SERVER_STATUS  8

/** Type of the stored value */
enum stored_value_type
{
    SVT_SERVER = 1,  // Generic server state information
    SVT_MASTER = 2,  // The master server name
};

using std::string;
using std::set;
using Guard = std::lock_guard<std::mutex>;
using maxscale::Monitor;
using maxscale::MonitorServer;
using ConnectResult = maxscale::MonitorServer::ConnectResult;
using namespace std::literals::chrono_literals;
using std::chrono::duration_cast;
using std::chrono::seconds;
using std::chrono::milliseconds;

const char CN_BACKEND_CONNECT_ATTEMPTS[]  = "backend_connect_attempts";
const char CN_BACKEND_CONNECT_TIMEOUT[]   = "backend_connect_timeout";
const char CN_BACKEND_READ_TIMEOUT[]      = "backend_read_timeout";
const char CN_BACKEND_WRITE_TIMEOUT[]     = "backend_write_timeout";
const char CN_DISK_SPACE_CHECK_INTERVAL[] = "disk_space_check_interval";
const char CN_EVENTS[]                    = "events";
const char CN_JOURNAL_MAX_AGE[]           = "journal_max_age";
const char CN_MONITOR_INTERVAL[]          = "monitor_interval";
const char CN_SCRIPT[]                    = "script";
const char CN_SCRIPT_TIMEOUT[]            = "script_timeout";

namespace
{

class ThisUnit
{
public:
    /**
     * Mark a monitor as the monitor of the server. A server may only be monitored by one monitor.
     *
     * @param server Server to claim. The name is not checked to be a valid server name.
     * @param new_owner Monitor which claims the server
     * @param existing_owner If server is already monitored, the owning monitor name is written here
     * @return True if success, false if server was claimed by another monitor
     */
    bool claim_server(const string& server, const string& new_owner, string* existing_owner)
    {
        mxb_assert(Monitor::is_main_worker());
        bool claim_success = false;
        auto iter          = m_server_owners.find(server);
        if (iter != m_server_owners.end())
        {
            // Server is already claimed by a monitor.
            *existing_owner = iter->second;
        }
        else
        {
            m_server_owners[server] = new_owner;
            claim_success           = true;
        }
        return claim_success;
    }

    /**
     * Mark a server as unmonitored.
     *
     * @param server The server name
     */
    void release_server(const string& server)
    {
        mxb_assert(Monitor::is_main_worker());
        auto iter = m_server_owners.find(server);
        mxb_assert(iter != m_server_owners.end());
        m_server_owners.erase(iter);
    }

    string claimed_by(const string& server)
    {
        mxb_assert(Monitor::is_main_worker());
        string rval;
        auto iter = m_server_owners.find(server);
        if (iter != m_server_owners.end())
        {
            rval = iter->second;
        }
        return rval;
    }

private:
    // Global map of servername->monitorname. Not mutexed, as this should only be accessed
    // from the admin thread.
    std::map<string, string> m_server_owners;
};

ThisUnit this_unit;

/** Server type specific bits */
const uint64_t server_type_bits = SERVER_MASTER | SERVER_SLAVE | SERVER_JOINED;

/** All server bits */
const uint64_t all_server_bits = SERVER_RUNNING | SERVER_MAINT | SERVER_MASTER | SERVER_SLAVE | SERVER_JOINED;

const char journal_name[]     = "monitor.dat";
const char journal_template[] = "%s/%s/%s";

/**
 * @brief Remove .tmp suffix and rename file
 *
 * @param src File to rename
 * @return True if file was successfully renamed
 */
bool rename_tmp_file(Monitor* monitor, const char* src)
{
    bool rval = true;
    char dest[PATH_MAX + 1];
    snprintf(dest, sizeof(dest), journal_template, mxs::datadir(), monitor->name(), journal_name);

    if (rename(src, dest) == -1)
    {
        rval = false;
        MXS_ERROR(
            "Failed to rename journal file '%s' to '%s': %d, %s", src, dest, errno, mxs_strerror(errno));
    }

    return rval;
}

/**
 * @brief Open temporary file
 *
 * @param monitor Monitor
 * @param path Output where the path is stored
 * @return Opened file or NULL on error
 */
FILE* open_tmp_file(Monitor* monitor, char* path)
{
    int nbytes    = snprintf(path, PATH_MAX, journal_template, mxs::datadir(), monitor->name(), "");
    int max_bytes = PATH_MAX - (int) sizeof(journal_name);
    FILE* rval    = NULL;

    if (nbytes < max_bytes && mxs_mkdir_all(path, 0744))
    {
        strcat(path, journal_name);
        strcat(path, "XXXXXX");
        int fd = mkstemp(path);

        if (fd == -1)
        {
            MXS_ERROR("Failed to open file '%s': %d, %s", path, errno, mxs_strerror(errno));
        }
        else
        {
            rval = fdopen(fd, "w");
        }
    }
    else
    {
        MXS_ERROR("Path is too long: %d characters exceeds the maximum path "
                  "length of %d bytes",
            nbytes,
            max_bytes);
    }

    return rval;
}

/**
 * @brief Store server data to in-memory buffer
 *
 * @param monitor Monitor
 * @param data Pointer to in-memory buffer used for storage, should be at least
 *             PATH_MAX bytes long
 * @param size Size of @c data
 */
void store_data(Monitor* monitor, MonitorServer* master, uint8_t* data, uint32_t size)
{
    uint8_t* ptr = data;

    /** Store the data length */
    mxb_assert(sizeof(size) == MMB_LEN_BYTES);
    ptr = mxs_set_byte4(ptr, size);

    /** Then the schema version */
    *ptr++ = MMB_SCHEMA_VERSION;

    /** Store the states of all servers */
    for (MonitorServer* db : monitor->servers())
    {
        *ptr++ = (char) SVT_SERVER;                                   // Value type
        memcpy(ptr, db->server->name(), strlen(db->server->name()));  // Name of the server
        ptr += strlen(db->server->name());
        *ptr++ = '\0';  // Null-terminate the string

        auto status = db->server->status();
        static_assert(
            sizeof(status) == MMB_LEN_SERVER_STATUS, "Status size should be MMB_LEN_SERVER_STATUS bytes");
        ptr = maxscale::set_byteN(ptr, status, MMB_LEN_SERVER_STATUS);
    }

    /** Store the current root master if we have one */
    if (master)
    {
        *ptr++ = (char) SVT_MASTER;
        memcpy(ptr, master->server->name(), strlen(master->server->name()));
        ptr += strlen(master->server->name());
        *ptr++ = '\0';  // Null-terminate the string
    }

    /** Calculate the CRC32 for the complete payload minus the CRC32 bytes */
    uint32_t crc = crc32(0L, NULL, 0);
    crc          = crc32(crc, (uint8_t*) data + MMB_LEN_BYTES, size - MMB_LEN_CRC32);
    mxb_assert(sizeof(crc) == MMB_LEN_CRC32);

    ptr = mxs_set_byte4(ptr, crc);
    mxb_assert(ptr - data == size + MMB_LEN_BYTES);
}

/**
 * Check that memory area contains a null terminator
 */
static bool has_null_terminator(const char* data, const char* end)
{
    while (data < end)
    {
        if (*data == '\0')
        {
            return true;
        }
        data++;
    }

    return false;
}

/**
 * Process a generic server
 */
const char* process_server(Monitor* monitor, const char* data, const char* end)
{
    for (MonitorServer* db : monitor->servers())
    {
        if (strcmp(db->server->name(), data) == 0)
        {
            const unsigned char* sptr = (unsigned char*) strchr(data, '\0');
            mxb_assert(sptr);
            sptr++;

            uint64_t status     = maxscale::get_byteN(sptr, MMB_LEN_SERVER_STATUS);
            db->mon_prev_status = status;
            db->server->set_status(status);
            db->set_pending_status(status);
            break;
        }
    }

    data += strlen(data) + 1 + MMB_LEN_SERVER_STATUS;

    return data;
}

/**
 * Process a master
 */
const char* process_master(Monitor* monitor, MonitorServer** master, const char* data, const char* end)
{
    if (master)
    {
        for (MonitorServer* db : monitor->servers())
        {
            if (strcmp(db->server->name(), data) == 0)
            {
                *master = db;
                break;
            }
        }
    }

    data += strlen(data) + 1;

    return data;
}

/**
 * Check that the calculated CRC32 matches the one stored on disk
 */
bool check_crc32(const uint8_t* data, uint32_t size, const uint8_t* crc_ptr)
{
    uint32_t crc            = mxs_get_byte4(crc_ptr);
    uint32_t calculated_crc = crc32(0L, NULL, 0);
    calculated_crc          = crc32(calculated_crc, data, size);
    return calculated_crc == crc;
}

/**
 * Process the stored journal data
 */
bool process_data_file(Monitor* monitor, MonitorServer** master, const char* data, const char* crc_ptr)
{
    const char* ptr = data;
    MXB_AT_DEBUG(const char* prevptr = ptr);

    while (ptr < crc_ptr)
    {
        /** All values contain a null terminated string */
        if (!has_null_terminator(ptr, crc_ptr))
        {
            MXS_ERROR("Possible corrupted journal file (no null terminator found). Ignoring.");
            return false;
        }

        stored_value_type type = (stored_value_type) ptr[0];
        ptr += MMB_LEN_VALUE_TYPE;

        switch (type)
        {
        case SVT_SERVER:
            ptr = process_server(monitor, ptr, crc_ptr);
            break;

        case SVT_MASTER:
            ptr = process_master(monitor, master, ptr, crc_ptr);
            break;

        default:
            MXS_ERROR("Possible corrupted journal file (unknown stored value). Ignoring.");
            return false;
        }
        mxb_assert(prevptr != ptr);
        MXB_AT_DEBUG(prevptr = ptr);
    }

    mxb_assert(ptr == crc_ptr);
    return true;
}

bool check_disk_space_exhausted(MonitorServer* pMs,
    const std::string& path,
    const maxscale::disk::SizesAndName& san,
    int32_t max_percentage)
{
    bool disk_space_exhausted = false;

    int32_t used_percentage = ((san.total() - san.available()) / (double) san.total()) * 100;

    if (used_percentage >= max_percentage)
    {
        MXS_ERROR("Disk space on %s at %s is exhausted; %d%% of the the disk "
                  "mounted on the path %s has been used, and the limit it %d%%.",
            pMs->server->name(),
            pMs->server->address(),
            used_percentage,
            path.c_str(),
            max_percentage);
        disk_space_exhausted = true;
    }

    return disk_space_exhausted;
}

const char ERR_CANNOT_MODIFY[] = "The server is monitored, so only the maintenance status can be "
                                 "set/cleared manually. Status was not modified.";
const char WRN_REQUEST_OVERWRITTEN[]
    = "Previous maintenance/draining request was not yet read by the monitor and was overwritten.";

/* Is not really an event as the other values, but is a valid config setting and also the default.
 * Bitmask value matches all events. */
const MXS_ENUM_VALUE monitor_event_default = {"all", ~0ULL};

// Allowed values for the "events"-setting. Also defines the enum<->string conversion for events.
const MXS_ENUM_VALUE monitor_event_values[] = {monitor_event_default,
    {"master_down", MASTER_DOWN_EVENT},
    {"master_up", MASTER_UP_EVENT},
    {"slave_down", SLAVE_DOWN_EVENT},
    {"slave_up", SLAVE_UP_EVENT},
    {"server_down", SERVER_DOWN_EVENT},
    {"server_up", SERVER_UP_EVENT},
    {"synced_down", SYNCED_DOWN_EVENT},
    {"synced_up", SYNCED_UP_EVENT},
    {"donor_down", DONOR_DOWN_EVENT},
    {"donor_up", DONOR_UP_EVENT},
    {"lost_master", LOST_MASTER_EVENT},
    {"lost_slave", LOST_SLAVE_EVENT},
    {"lost_synced", LOST_SYNCED_EVENT},
    {"lost_donor", LOST_DONOR_EVENT},
    {"new_master", NEW_MASTER_EVENT},
    {"new_slave", NEW_SLAVE_EVENT},
    {"new_synced", NEW_SYNCED_EVENT},
    {"new_donor", NEW_DONOR_EVENT},
    {NULL}};
}  // namespace

namespace maxscale
{

Monitor::Monitor(const string& name, const string& module)
    : m_name(name)
    , m_module(module)
{
    memset(m_journal_hash, 0, sizeof(m_journal_hash));
}

void Monitor::stop()
{
    do_stop();

    for (auto db : m_servers)
    {
        // TODO: Should be db->close().
        mysql_close(db->con);
        db->con = NULL;
    }
}

const char* Monitor::name() const
{
    return m_name.c_str();
}

bool Monitor::configure(const mxs::ConfigParameters* params)
{
    m_settings.interval        = params->get_duration<milliseconds>(CN_MONITOR_INTERVAL).count();
    m_settings.journal_max_age = params->get_duration<seconds>(CN_JOURNAL_MAX_AGE).count();
    m_settings.events          = params->get_enum(CN_EVENTS, monitor_event_values);

    MonitorServer::ConnectionSettings& conn_settings = m_settings.shared.conn_settings;
    conn_settings.read_timeout     = params->get_duration<seconds>(CN_BACKEND_READ_TIMEOUT).count();
    conn_settings.write_timeout    = params->get_duration<seconds>(CN_BACKEND_WRITE_TIMEOUT).count();
    conn_settings.connect_timeout  = params->get_duration<seconds>(CN_BACKEND_CONNECT_TIMEOUT).count();
    conn_settings.connect_attempts = params->get_integer(CN_BACKEND_CONNECT_ATTEMPTS);
    conn_settings.username         = params->get_string(CN_USER);
    conn_settings.password         = params->get_string(CN_PASSWORD);

    // Disk check interval is given in ms, duration is constructed from seconds.
    auto dsc_interval = params->get_duration<milliseconds>(CN_DISK_SPACE_CHECK_INTERVAL).count();
    // 0 implies disabling -> save negative value to interval.
    m_settings.disk_space_check_interval = (dsc_interval > 0) ? milliseconds(dsc_interval) : -1s;

    // First, remove all servers.
    remove_all_servers();

    bool error = false;
    string name_not_found;
    auto servers_temp = params->get_server_list(CN_SERVERS, &name_not_found);
    if (name_not_found.empty())
    {
        for (auto elem : servers_temp)
        {
            if (!add_server(elem))
            {
                error = true;
            }
        }
    }
    else
    {
        MXB_ERROR("Server '%s' configured for monitor '%s' does not exist.", name_not_found.c_str(), name());
        error = true;
    }


    /* The previous config values were normal types and were checked by the config manager
     * to be correct. The following is a complicated type and needs to be checked separately. */
    auto threshold_string = params->get_string(CN_DISK_SPACE_THRESHOLD);
    if (!set_disk_space_threshold(threshold_string))
    {
        MXS_ERROR("Invalid value for '%s' for monitor %s: %s",
            CN_DISK_SPACE_THRESHOLD,
            name(),
            threshold_string.c_str());
        error = true;
    }

    m_settings.script_timeout = params->get_duration<seconds>(CN_SCRIPT_TIMEOUT).count();
    m_settings.script         = params->get_string(CN_SCRIPT);
    if (m_settings.script.empty())
    {
        // Reset current external cmd if any.
        m_scriptcmd.reset();
    }
    else
    {
        m_scriptcmd = ExternalCmd::create(m_settings.script, m_settings.script_timeout);
        if (!m_scriptcmd)
        {
            MXS_ERROR("Failed to initialize script '%s'.", m_settings.script.c_str());
            error = true;
        }
    }

    if (!error)
    {
        // Store the parameters, needed for serialization.
        m_parameters = *params;
        // Store module name into parameter storage.
        m_parameters.set(CN_MODULE, m_module);
    }
    return !error;
}

const mxs::ConfigParameters& Monitor::parameters() const
{
    return m_parameters;
}

const Monitor::Settings& Monitor::settings() const
{
    return m_settings;
}

const MonitorServer::ConnectionSettings& Monitor::conn_settings() const
{
    return m_settings.shared.conn_settings;
}

long Monitor::ticks() const
{
    return m_ticks.load(std::memory_order_acquire);
}

const char* Monitor::state_string() const
{
    return is_running() ? "Running" : "Stopped";
}

Monitor::~Monitor()
{
    for (auto server : m_servers)
    {
        // TODO: store unique pointers in the array
        delete server;
    }
    m_servers.clear();
}

/**
 * Add a server to the monitor. Fails if server is already monitored.
 *
 * @param server  A server
 * @return True if server was added
 */
bool Monitor::add_server(SERVER* server)
{
    // This should only be called from the admin thread while the monitor is stopped.
    mxb_assert(!is_running() && is_main_worker());
    bool success = false;
    string existing_owner;
    if (this_unit.claim_server(server->name(), m_name, &existing_owner))
    {
        auto new_server = create_server(server, m_settings.shared);
        m_servers.push_back(new_server);
        server_added(server);
        success = true;
    }
    else
    {
        MXS_ERROR("Server '%s' is already monitored by '%s', cannot add it to another monitor.",
            server->name(),
            existing_owner.c_str());
    }
    return success;
}

void Monitor::server_added(SERVER* server)
{
    service_add_server(this, server);
}

void Monitor::server_removed(SERVER* server)
{
    service_remove_server(this, server);
}

/**
 * Remove all servers from the monitor.
 */
void Monitor::remove_all_servers()
{
    // This should only be called from the admin thread while the monitor is stopped.
    mxb_assert(!is_running() && is_main_worker());
    for (auto mon_server : m_servers)
    {
        mxb_assert(this_unit.claimed_by(mon_server->server->name()) == m_name);
        this_unit.release_server(mon_server->server->name());
        server_removed(mon_server->server);
        delete mon_server;
    }
    m_servers.clear();
}

json_t* Monitor::to_json(const char* host) const
{
    const char CN_MONITOR_DIAGNOSTICS[] = "monitor_diagnostics";
    const char CN_TICKS[]               = "ticks";

    // This function mostly reads settings-type data, which is only written to by the admin thread,
    // The rest is safe to read without mutexes.
    mxb_assert(Monitor::is_main_worker());
    json_t* rval = json_object();
    json_t* attr = json_object();
    json_t* rel  = json_object();

    auto my_name = name();
    json_object_set_new(rval, CN_ID, json_string(my_name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_MONITORS));

    json_object_set_new(attr, CN_MODULE, json_string(m_module.c_str()));
    json_object_set_new(attr, CN_STATE, json_string(state_string()));
    json_object_set_new(attr, CN_TICKS, json_integer(ticks()));

    /** Monitor parameters */
    json_object_set_new(attr, CN_PARAMETERS, parameters_to_json());

    if (is_running())
    {
        json_t* diag = diagnostics();
        if (diag)
        {
            json_object_set_new(attr, CN_MONITOR_DIAGNOSTICS, diag);
        }
    }

    std::string self = std::string(MXS_JSON_API_MONITORS) + name() + "/relationships/";

    if (!m_servers.empty())
    {
        json_t* mon_rel = mxs_json_relationship(host, self + "servers", MXS_JSON_API_SERVERS);
        for (MonitorServer* db : m_servers)
        {
            mxs_json_add_relation(mon_rel, db->server->name(), CN_SERVERS);
        }
        json_object_set_new(rel, CN_SERVERS, mon_rel);
    }

    if (auto services = service_relations_to_monitor(this, host, self + "services"))
    {
        json_object_set_new(rel, CN_SERVICES, services);
    }

    json_object_set_new(rval, CN_RELATIONSHIPS, rel);
    json_object_set_new(rval, CN_ATTRIBUTES, attr);
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_MONITORS, my_name));
    return rval;
}

json_t* Monitor::parameters_to_json() const
{
    json_t* rval          = json_object();
    const MXS_MODULE* mod = get_module(m_module.c_str(), MODULE_MONITOR);
    auto my_config        = parameters();
    config_add_module_params_json(
        &my_config, {CN_TYPE, CN_MODULE, CN_SERVERS}, common_monitor_params(), mod->parameters, rval);
    return rval;
}

bool Monitor::test_permissions(const string& query)
{
    if (m_servers.empty() || mxs::Config::get().skip_permission_checks.get())
    {
        return true;
    }

    bool rval = false;

    for (MonitorServer* mondb : m_servers)
    {
        auto result = mondb->ping_or_connect();

        if (!connection_is_ok(result))
        {
            MXS_ERROR("[%s] Failed to connect to server '%s' ([%s]:%d) when"
                      " checking monitor user credentials and permissions.",
                name(),
                mondb->server->name(),
                mondb->server->address(),
                mondb->server->port());

            if (result != ConnectResult::ACCESS_DENIED)
            {
                rval = true;
            }
        }
        else if (mxs_mysql_query(mondb->con, query.c_str()) != 0)
        {
            switch (mysql_errno(mondb->con))
            {
            case ER_TABLEACCESS_DENIED_ERROR:
            case ER_COLUMNACCESS_DENIED_ERROR:
            case ER_SPECIFIC_ACCESS_DENIED_ERROR:
            case ER_PROCACCESS_DENIED_ERROR:
            case ER_KILL_DENIED_ERROR:
                rval = false;
                break;

            default:
                rval = true;
                break;
            }

            MXS_ERROR("[%s] Failed to execute query '%s' with user '%s'. MySQL error message: %s",
                name(),
                query.c_str(),
                conn_settings().username.c_str(),
                mysql_error(mondb->con));
        }
        else
        {
            rval           = true;
            MYSQL_RES* res = mysql_use_result(mondb->con);
            if (res == NULL)
            {
                MXS_ERROR("[%s] Result retrieval failed when checking monitor permissions: %s",
                    name(),
                    mysql_error(mondb->con));
            }
            else
            {
                mysql_free_result(res);
            }

            mondb->maybe_fetch_session_track();
        }
    }

    return rval;
}

json_t* Monitor::monitored_server_json_attributes(const SERVER* srv) const
{
    json_t* rval = nullptr;
    auto comp    = [srv](MonitorServer* ms) {
        return ms->server == srv;
    };

    auto iter = std::find_if(m_servers.begin(), m_servers.end(), comp);
    if (iter != m_servers.end())
    {
        auto mon_srv = *iter;
        rval         = json_object();
        json_object_set_new(rval, "node_id", json_integer(mon_srv->node_id));
        json_object_set_new(rval, "master_id", json_integer(mon_srv->master_id));

        const char* event_name = get_event_name(mon_srv->last_event);
        time_t t               = maxscale_started() + MXS_CLOCK_TO_SEC(mon_srv->triggered_at);
        json_object_set_new(rval, "last_event", json_string(event_name));
        json_object_set_new(rval, "triggered_at", json_string(http_to_date(t).c_str()));

        if (auto extra = diagnostics(mon_srv))
        {
            json_object_update(rval, extra);
            json_decref(extra);
        }
    }
    return rval;
}

void Monitor::wait_for_status_change()
{
    mxb_assert(is_running());
    mxb_assert(Monitor::is_main_worker());

    // Store the tick count before we request the change
    auto start = ticks();

    // Set a flag so the next loop happens sooner.
    m_status_change_pending.store(true, std::memory_order_release);

    while (start == ticks())
    {
        std::this_thread::sleep_for(milliseconds(100));
    }
}

void MonitorServer::stash_current_status()
{
    mon_prev_status = server->status();
    pending_status  = server->status();
}

void MonitorServer::set_pending_status(uint64_t bits)
{
    pending_status |= bits;
}

void MonitorServer::clear_pending_status(uint64_t bits)
{
    pending_status &= ~bits;
}

/*
 * Determine a monitor event, defined by the difference between the old
 * status of a server and the new status.
 *
 * @return  monitor_event_t     A monitor event (enum)
 *
 * @note This function must only be called from mon_process_state_changes
 */
mxs_monitor_event_t MonitorServer::get_event_type() const
{
    typedef enum
    {
        DOWN_EVENT,
        UP_EVENT,
        LOSS_EVENT,
        NEW_EVENT,
        UNSUPPORTED_EVENT
    } general_event_type;

    general_event_type event_type = UNSUPPORTED_EVENT;

    uint64_t prev    = mon_prev_status & all_server_bits;
    uint64_t present = server->status() & all_server_bits;

    if (prev == present)
    {
        /* This should never happen */
        mxb_assert(false);
        return UNDEFINED_EVENT;
    }

    if ((prev & SERVER_RUNNING) == 0)
    {
        /* The server was not running previously */
        if ((present & SERVER_RUNNING) != 0)
        {
            event_type = UP_EVENT;
        }
        else
        {
            /* Otherwise, was not running and still is not running. This should never happen. */
            mxb_assert(false);
        }
    }
    else
    {
        /* Previous state must have been running */
        if ((present & SERVER_RUNNING) == 0)
        {
            event_type = DOWN_EVENT;
        }
        else
        {
            /** These are used to detect whether we actually lost something or
             * just transitioned from one state to another */
            uint64_t prev_bits    = prev & (SERVER_MASTER | SERVER_SLAVE);
            uint64_t present_bits = present & (SERVER_MASTER | SERVER_SLAVE);

            /* Was running and still is */
            if ((!prev_bits || !present_bits || prev_bits == present_bits) && (prev & server_type_bits))
            {
                /* We used to know what kind of server it was */
                event_type = LOSS_EVENT;
            }
            else
            {
                /* We didn't know what kind of server it was, now we do*/
                event_type = NEW_EVENT;
            }
        }
    }

    mxs_monitor_event_t rval = UNDEFINED_EVENT;

    switch (event_type)
    {
    case UP_EVENT:
        rval = (present & SERVER_MASTER) ? MASTER_UP_EVENT
             : (present & SERVER_SLAVE)  ? SLAVE_UP_EVENT
             : (present & SERVER_JOINED) ? SYNCED_UP_EVENT
                                         : SERVER_UP_EVENT;
        break;

    case DOWN_EVENT:
        rval = (prev & SERVER_MASTER) ? MASTER_DOWN_EVENT
             : (prev & SERVER_SLAVE)  ? SLAVE_DOWN_EVENT
             : (prev & SERVER_JOINED) ? SYNCED_DOWN_EVENT
                                      : SERVER_DOWN_EVENT;
        break;

    case LOSS_EVENT:
        rval = (prev & SERVER_MASTER) ? LOST_MASTER_EVENT
             : (prev & SERVER_SLAVE)  ? LOST_SLAVE_EVENT
             : (prev & SERVER_JOINED) ? LOST_SYNCED_EVENT
                                      : UNDEFINED_EVENT;
        break;

    case NEW_EVENT:
        rval = (present & SERVER_MASTER) ? NEW_MASTER_EVENT
             : (present & SERVER_SLAVE)  ? NEW_SLAVE_EVENT
             : (present & SERVER_JOINED) ? NEW_SYNCED_EVENT
                                         : UNDEFINED_EVENT;
        break;

    default:
        /* This should never happen */
        mxb_assert(false);
        break;
    }

    mxb_assert(rval != UNDEFINED_EVENT);
    return rval;
}

const char* Monitor::get_event_name(mxs_monitor_event_t event)
{
    for (int i = 0; monitor_event_values[i].name; i++)
    {
        if (monitor_event_values[i].enum_value == event)
        {
            return monitor_event_values[i].name;
        }
    }

    mxb_assert(!true);
    return "undefined_event";
}

const char* MonitorServer::get_event_name()
{
    return Monitor::get_event_name(last_event);
}

string Monitor::gen_serverlist(int status, CredentialsApproach approach)
{
    string rval;
    rval.reserve(100 * m_servers.size());

    string separator;
    for (auto mon_server : m_servers)
    {
        auto server = static_cast<Server*>(mon_server->server);
        if (status == 0 || server->status() & status)
        {
            if (approach == CredentialsApproach::EXCLUDE)
            {
                rval += separator + mxb::string_printf("[%s]:%i", server->address(), server->port());
            }
            else
            {
                string user                    = conn_settings().username;
                string password                = conn_settings().password;
                string server_specific_monuser = server->monitor_user();
                if (!server_specific_monuser.empty())
                {
                    user     = server_specific_monuser;
                    password = server->monitor_password();
                }

                rval += separator
                      + mxb::string_printf(
                          "%s:%s@[%s]:%d", user.c_str(), password.c_str(), server->address(), server->port());
            }
            separator = ",";
        }
    }
    return rval;
}

/**
 * Check if current monitored server status has changed.
 *
 * @return              true if status has changed
 */
bool MonitorServer::status_changed()
{
    bool rval = false;

    /* Previous status is -1 if not yet set */
    if (mon_prev_status != static_cast<uint64_t>(-1))
    {
        uint64_t old_status = mon_prev_status & all_server_bits;
        uint64_t new_status = server->status() & all_server_bits;

        /**
         * The state has changed if the relevant state bits are not the same,
         * the server is either running, stopping or starting and the server is
         * not going into maintenance or coming out of it
         */
        if (old_status != new_status && ((old_status | new_status) & SERVER_MAINT) == 0
            && ((old_status | new_status) & SERVER_RUNNING) == SERVER_RUNNING)
        {
            rval = true;
        }
    }

    return rval;
}

bool MonitorServer::auth_status_changed()
{
    uint64_t old_status = mon_prev_status;
    uint64_t new_status = server->status();

    return old_status != static_cast<uint64_t>(-1) && old_status != new_status
        && (old_status & SERVER_AUTH_ERROR) != (new_status & SERVER_AUTH_ERROR);
}

/**
 * Check if current monitored server has a loggable failure status.
 *
 * @return true if failed status can be logged or false
 */
bool MonitorServer::should_print_fail_status()
{
    return server->is_down() && mon_err_count == 0;
}

MonitorServer* Monitor::find_parent_node(MonitorServer* target)
{
    MonitorServer* rval = NULL;

    if (target->master_id > 0)
    {
        for (MonitorServer* node : m_servers)
        {
            if (node->node_id == target->master_id)
            {
                rval = node;
                break;
            }
        }
    }

    return rval;
}

std::string Monitor::child_nodes(MonitorServer* parent)
{
    std::stringstream ss;

    if (parent->node_id > 0)
    {
        bool have_content = false;
        for (MonitorServer* node : m_servers)
        {
            if (node->master_id == parent->node_id)
            {
                if (have_content)
                {
                    ss << ",";
                }

                ss << "[" << node->server->address() << "]:" << node->server->port();
                have_content = true;
            }
        }
    }

    return ss.str();
}

int Monitor::launch_command(MonitorServer* ptr)
{
    m_scriptcmd->reset_substituted();

    // A generator function is ran only if the matching substitution keyword is found.

    auto gen_initiator = [ptr] {
        return mxb::string_printf("[%s]:%d", ptr->server->address(), ptr->server->port());
    };

    auto gen_parent = [this, ptr] {
        string ss;
        MonitorServer* parent = find_parent_node(ptr);
        if (parent)
        {
            ss = mxb::string_printf("[%s]:%d", parent->server->address(), parent->server->port());
        }
        return ss;
    };

    m_scriptcmd->match_substitute("$INITIATOR", gen_initiator);
    m_scriptcmd->match_substitute("$PARENT", gen_parent);

    m_scriptcmd->match_substitute("$CHILDREN", [this, ptr] { return child_nodes(ptr); });

    m_scriptcmd->match_substitute("$EVENT", [ptr] { return ptr->get_event_name(); });

    m_scriptcmd->match_substitute("$CREDENTIALS", [this] {
        // Provides credentials for all servers.
        return gen_serverlist(0, CredentialsApproach::INCLUDE);
    });

    m_scriptcmd->match_substitute("$NODELIST", [this] { return gen_serverlist(SERVER_RUNNING); });

    m_scriptcmd->match_substitute("$LIST", [this] { return gen_serverlist(0); });

    m_scriptcmd->match_substitute("$MASTERLIST", [this] { return gen_serverlist(SERVER_MASTER); });

    m_scriptcmd->match_substitute("$SLAVELIST", [this] { return gen_serverlist(SERVER_SLAVE); });

    m_scriptcmd->match_substitute("$SYNCEDLIST", [this] { return gen_serverlist(SERVER_JOINED); });

    int rv = m_scriptcmd->externcmd_execute();
    if (rv == 0)
    {
        MXS_NOTICE("Executed monitor script on event '%s'. Script was: '%s'",
            ptr->get_event_name(),
            m_scriptcmd->substituted());
    }
    else if (rv == -1)
    {
        // Internal error
        MXS_ERROR("Failed to execute script on server state change event '%s'. Script was: '%s'",
            ptr->get_event_name(),
            m_scriptcmd->substituted());
    }
    else
    {
        // Script returned a non-zero value
        MXS_ERROR("Script returned %d on event '%s'. Script was: '%s'",
            rv,
            ptr->get_event_name(),
            m_scriptcmd->substituted());
    }
    return rv;
}

MonitorServer::ConnectResult MonitorServer::ping_or_connect_to_db(
    const MonitorServer::ConnectionSettings& sett, SERVER& server, MYSQL** ppConn, std::string* pError)
{
    mxb_assert(ppConn);
    mxb_assert(pError);
    auto pConn = *ppConn;
    if (pConn)
    {
        mxb::StopWatch timer;
        /** Return if the connection is OK */
        if (mysql_ping(pConn) == 0)
        {
            long time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
            server.set_ping(time_us);
            return ConnectResult::OLDCONN_OK;
        }
    }

    string uname    = sett.username;
    string passwd   = sett.password;
    const auto& srv = static_cast<const Server&>(server);  // Clean this up later.

    string server_specific_monuser = srv.monitor_user();
    if (!server_specific_monuser.empty())
    {
        uname  = server_specific_monuser;
        passwd = srv.monitor_password();
    }

    auto dpwd = mxs::decrypt_password(passwd);

    auto connect = [&pConn, &sett, &server, &uname, &dpwd](int port) {
        if (pConn)
        {
            mysql_close(pConn);
        }
        pConn = mysql_init(nullptr);
        mysql_optionsv(pConn, MYSQL_OPT_CONNECT_TIMEOUT, &sett.connect_timeout);
        mysql_optionsv(pConn, MYSQL_OPT_READ_TIMEOUT, &sett.read_timeout);
        mysql_optionsv(pConn, MYSQL_OPT_WRITE_TIMEOUT, &sett.write_timeout);
        mysql_optionsv(pConn, MYSQL_PLUGIN_DIR, mxs::connector_plugindir());
        mysql_optionsv(pConn, MARIADB_OPT_MULTI_STATEMENTS, nullptr);
        return mxs_mysql_real_connect(pConn, &server, port, uname.c_str(), dpwd.c_str()) != nullptr;
    };

    ConnectResult conn_result = ConnectResult::REFUSED;
    for (int i = 0; i < sett.connect_attempts; i++)
    {
        auto start = time(nullptr);
        // Try first with normal port, then with extra-port.
        if (connect(server.port()))
        {
            conn_result = ConnectResult::NEWCONN_OK;
            break;
        }
        else
        {
            auto extra_port = server.extra_port();
            if (extra_port > 0)
            {
                if (connect(extra_port))
                {
                    conn_result = ConnectResult::NEWCONN_OK;
                    MXS_WARNING(
                        "Could not connect with normal port to server '%s', using extra_port", server.name());
                    break;
                }
            }
        }

        if (conn_result == ConnectResult::REFUSED && difftime(time(nullptr), start) >= sett.connect_timeout)
        {
            conn_result = ConnectResult::TIMEOUT;
        }

        *pError  = mysql_error(pConn);
        auto err = mysql_errno(pConn);
        mysql_close(pConn);
        pConn = nullptr;

        if (err == ER_ACCESS_DENIED_ERROR || err == ER_ACCESS_DENIED_NO_PASSWORD_ERROR)
        {
            conn_result = ConnectResult::ACCESS_DENIED;
        }
    }

    *ppConn = pConn;

    if (conn_result == ConnectResult::NEWCONN_OK)
    {
        // If a new connection was created, measure ping separately.
        mxb::StopWatch timer;
        long time_us = mxs::Target::PING_UNDEFINED;
        if (mysql_ping(pConn) == 0)
        {
            time_us = std::chrono::duration_cast<std::chrono::microseconds>(timer.split()).count();
        }
        server.set_ping(time_us);
    }

    return conn_result;
}

ConnectResult MonitorServer::ping_or_connect()
{
    auto old_type = server->info().type();
    auto connect  = [this] {
        return ping_or_connect_to_db(m_shared.conn_settings, *server, &con, &m_latest_error);
    };

    auto res = connect();
    if (res == ConnectResult::NEWCONN_OK)
    {
        mxs_mysql_update_server_version(server, con);
        if (server->info().type() != old_type)
        {
            /**
             * The server type affects the init commands sent by mxs_mysql_real_connect.
             * If server type changed, reconnect so that the correct commands are sent.
             * This typically only happens during startup.
             */
            mysql_close(con);
            con = nullptr;
            res = connect();
        }
    }
    return res;
}

static constexpr seconds session_track_update_interval = 10min;

bool MonitorServer::should_fetch_session_track()
{
    bool rval = false;
    // Only fetch variables from real servers.
    return is_database() && (mxb::Clock::now() - m_last_session_track_update) > session_track_update_interval;
}

/**
 * Fetch 'session_track_system_variables' from the server and store it in the SERVER object.
 */
void MonitorServer::fetch_session_track()
{
    if (auto r = mxs::execute_query(con, "select @@session_track_system_variables;"))
    {
        MXS_INFO("'session_track_system_variables' loaded from '%s', next update in %ld seconds.",
            server->name(),
            session_track_update_interval.count());
        m_last_session_track_update = mxb::Clock::now();

        if (r->next_row() && r->get_col_count() > 0)
        {
            server->set_session_track_system_variables(r->get_string(0));
        }
    }
}

/**
 * Is the return value one of the 'OK' values.
 *
 * @param connect_result Return value of mon_ping_or_connect_to_db
 * @return True of connection is ok
 */
bool Monitor::connection_is_ok(ConnectResult connect_result)
{
    return connect_result == ConnectResult::OLDCONN_OK || connect_result == ConnectResult::NEWCONN_OK;
}

string Monitor::get_server_monitor(const SERVER* server)
{
    return this_unit.claimed_by(server->name());
}

bool Monitor::is_main_worker()
{
    return mxs::MainWorker::is_main_worker();
}

/**
 * Log an error about the failure to connect to a backend server and why it happened.
 *
 * @param rval Return value of mon_ping_or_connect_to_db
 */
void MonitorServer::log_connect_error(ConnectResult rval)
{
    mxb_assert(!Monitor::connection_is_ok(rval));
    const char TIMED_OUT[] = "Monitor timed out when connecting to server %s[%s:%d] : '%s'";
    const char REFUSED[]   = "Monitor was unable to connect to server %s[%s:%d] : '%s'";
    MXS_ERROR(rval == ConnectResult::TIMEOUT ? TIMED_OUT : REFUSED,
        server->name(),
        server->address(),
        server->port(),
        m_latest_error.c_str());
}

void MonitorServer::log_state_change(const std::string& reason)
{
    string prev = Target::status_to_string(mon_prev_status, server->stats().n_current);
    string next = server->status_string();
    MXS_NOTICE("Server changed state: %s[%s:%u]: %s. [%s] -> [%s]%s%s",
        server->name(),
        server->address(),
        server->port(),
        get_event_name(),
        prev.c_str(),
        next.c_str(),
        reason.empty() ? "" : ": ",
        reason.c_str());
}

void Monitor::hangup_failed_servers()
{
    for (MonitorServer* ptr : m_servers)
    {
        if (ptr->status_changed() && (!(ptr->server->is_usable()) || !(ptr->server->is_in_cluster())))
        {
            BackendDCB::hangup(ptr->server);
        }
    }
}

void MonitorServer::mon_report_query_error()
{
    MXS_ERROR("Failed to execute query on server '%s' ([%s]:%d): %s",
        server->name(),
        server->address(),
        server->port(),
        mysql_error(con));
}

void Monitor::check_maintenance_requests()
{
    /* In theory, the admin may be modifying the server maintenance status during this function. The overall
     * maintenance flag should be read-written atomically to prevent missing a value. */
    bool was_pending = m_status_change_pending.exchange(false, std::memory_order_acq_rel);
    if (was_pending)
    {
        for (auto server : m_servers)
        {
            server->apply_status_requests();
        }
    }
}

void Monitor::detect_handle_state_changes()
{
    bool master_down = false;
    bool master_up   = false;

    for (MonitorServer* ptr : m_servers)
    {
        if (ptr->status_changed())
        {
            /**
             * The last executed event will be needed if a passive MaxScale is
             * promoted to an active one and the last event that occurred on
             * a server was a master_down event.
             *
             * In this case, a failover script should be called if no master_up
             * or new_master events are triggered within a pre-defined time limit.
             */
            mxs_monitor_event_t event = ptr->get_event_type();
            ptr->last_event           = event;
            ptr->triggered_at         = mxs_clock();
            ptr->log_state_change(annotate_state_change(ptr));

            if (event == MASTER_DOWN_EVENT)
            {
                master_down = true;
            }
            else if (event == MASTER_UP_EVENT || event == NEW_MASTER_EVENT)
            {
                master_up = true;
            }

            if (m_scriptcmd && (event & m_settings.events))
            {
                launch_command(ptr);
            }
        }
        else if (ptr->auth_status_changed())
        {
            ptr->log_state_change("");
        }
    }

    if (master_down && master_up)
    {
        MXS_NOTICE("Master switch detected: lost a master and gained a new one");
    }
}

int Monitor::get_data_file_path(char* path) const
{
    int rv = snprintf(path, PATH_MAX, journal_template, mxs::datadir(), name(), journal_name);
    return rv;
}

/**
 * @brief Open stored journal file
 *
 * @param monitor Monitor to reload
 * @param path Output where path is stored
 * @return Opened file or NULL on error
 */
FILE* Monitor::open_data_file(Monitor* monitor, char* path)
{
    FILE* rval = NULL;
    int nbytes = monitor->get_data_file_path(path);

    if (nbytes < PATH_MAX)
    {
        if ((rval = fopen(path, "rb")) == NULL && errno != ENOENT)
        {
            MXS_ERROR("Failed to open journal file: %d, %s", errno, mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Path is too long: %d characters exceeds the maximum path "
                  "length of %d bytes",
            nbytes,
            PATH_MAX);
    }

    return rval;
}

void Monitor::store_server_journal(MonitorServer* master)
{
    auto monitor = this;  // TODO: cleanup later
    /** Calculate how much memory we need to allocate */
    uint32_t size = MMB_LEN_SCHEMA_VERSION + MMB_LEN_CRC32;

    for (MonitorServer* db : m_servers)
    {
        /** Each server is stored as a type byte and a null-terminated string
         * followed by eight byte server status. */
        size += MMB_LEN_VALUE_TYPE + strlen(db->server->name()) + 1 + MMB_LEN_SERVER_STATUS;
    }

    if (master)
    {
        /** The master server name is stored as a null terminated string */
        size += MMB_LEN_VALUE_TYPE + strlen(master->server->name()) + 1;
    }

    /** 4 bytes for file length, 1 byte for schema version and 4 bytes for CRC32 */
    uint32_t buffer_size = size + MMB_LEN_BYTES;
    uint8_t* data        = (uint8_t*) MXS_MALLOC(buffer_size);
    char path[PATH_MAX + 1];

    if (data)
    {
        /** Store the data in memory first and compare the current hash to
         * the hash of the last stored journal. This isn't a fool-proof
         * method of detecting changes but any failures are mainly of
         * theoretical nature. */
        store_data(monitor, master, data, size);
        uint8_t hash[SHA_DIGEST_LENGTH];
        SHA1(data, size, hash);

        if (memcmp(monitor->m_journal_hash, hash, sizeof(hash)) != 0)
        {
            FILE* file = open_tmp_file(monitor, path);

            if (file)
            {
                /** Write the data to a temp file and rename it to the final name */
                if (fwrite(data, 1, buffer_size, file) == buffer_size && fflush(file) == 0)
                {
                    if (!rename_tmp_file(monitor, path))
                    {
                        unlink(path);
                    }
                    else
                    {
                        memcpy(monitor->m_journal_hash, hash, sizeof(hash));
                    }
                }
                else
                {
                    MXS_ERROR("Failed to write journal data to disk: %d, %s", errno, mxs_strerror(errno));
                }
                fclose(file);
            }
        }
    }
    MXS_FREE(data);
}

void Monitor::load_server_journal(MonitorServer** master)
{
    auto monitor = this;  // TODO: cleanup later
    char path[PATH_MAX];
    FILE* file = open_data_file(monitor, path);

    if (file)
    {
        uint32_t size = 0;
        size_t bytes  = fread(&size, 1, MMB_LEN_BYTES, file);
        mxb_assert(sizeof(size) == MMB_LEN_BYTES);

        if (bytes == MMB_LEN_BYTES)
        {
            /** Payload contents:
             *
             * - One byte of schema version
             * - `size - 5` bytes of data
             * - Trailing 4 bytes of CRC32
             */
            char* data = (char*) MXS_MALLOC(size);

            if (data && (bytes = fread(data, 1, size, file)) == size)
            {
                if (*data == MMB_SCHEMA_VERSION)
                {
                    if (check_crc32(
                            (uint8_t*) data, size - MMB_LEN_CRC32, (uint8_t*) data + size - MMB_LEN_CRC32))
                    {
                        if (process_data_file(
                                monitor, master, data + MMB_LEN_SCHEMA_VERSION, data + size - MMB_LEN_CRC32))
                        {
                            MXS_INFO("Loaded server states from journal file: %s", path);
                        }
                    }
                    else
                    {
                        MXS_ERROR("CRC32 mismatch in journal file. Ignoring.");
                    }
                }
                else
                {
                    MXS_ERROR("Unknown journal schema version: %d", (int) *data);
                }
            }
            else if (data)
            {
                if (ferror(file))
                {
                    MXS_ERROR("Failed to read journal file: %d, %s", errno, mxs_strerror(errno));
                }
                else
                {
                    MXS_ERROR("Failed to read journal file: Expected %u bytes, "
                              "read %lu bytes.",
                        size,
                        bytes);
                }
            }
            MXS_FREE(data);
        }
        else
        {
            if (ferror(file))
            {
                MXS_ERROR("Failed to read journal file length: %d, %s", errno, mxs_strerror(errno));
            }
            else
            {
                MXS_ERROR("Failed to read journal file length: Expected %d bytes, "
                          "read %lu bytes.",
                    MMB_LEN_BYTES,
                    bytes);
            }
        }

        fclose(file);
    }
}

void Monitor::remove_server_journal()
{
    char path[PATH_MAX];
    if (get_data_file_path(path) < PATH_MAX)
    {
        unlink(path);
    }
    else
    {
        MXS_ERROR("Path to monitor journal directory is too long.");
    }
}

bool Monitor::journal_is_stale() const
{
    bool is_stale = true;
    char path[PATH_MAX];
    auto max_age = m_settings.journal_max_age;
    if (get_data_file_path(path) < PATH_MAX)
    {
        struct stat st;

        if (stat(path, &st) == 0)
        {
            time_t tdiff = time(NULL) - st.st_mtim.tv_sec;

            if (tdiff >= max_age)
            {
                MXS_NOTICE("Journal file was created %ld seconds ago. Maximum journal "
                           "age is %ld seconds.",
                    tdiff,
                    max_age);
            }
            else
            {
                is_stale = false;
            }
        }
        else if (errno == ENOENT)
        {
            is_stale = false;
        }
        else
        {
            MXS_ERROR("Failed to inspect journal file: %d, %s", errno, mxs_strerror(errno));
        }
    }
    else
    {
        MXS_ERROR("Path to monitor journal directory is too long.");
    }

    return is_stale;
}

MonitorServer* Monitor::get_monitored_server(SERVER* search_server)
{
    mxb_assert(search_server);
    for (const auto iter : m_servers)
    {
        if (iter->server == search_server)
        {
            return iter;
        }
    }
    return nullptr;
}

std::vector<MonitorServer*> Monitor::get_monitored_serverlist(const string& key, bool* error_out)
{
    std::vector<MonitorServer*> monitored_array;
    // Check that value exists.
    if (!m_parameters.contains(key))
    {
        return monitored_array;
    }

    string name_error;
    auto servers = m_parameters.get_server_list(key, &name_error);
    if (!servers.empty())
    {
        // All servers in the array must be monitored by the given monitor.
        for (auto elem : servers)
        {
            MonitorServer* mon_serv = get_monitored_server(elem);
            if (mon_serv)
            {
                monitored_array.push_back(mon_serv);
            }
            else
            {
                MXS_ERROR("Server '%s' is not monitored by monitor '%s'.", elem->name(), name());
                *error_out = true;
            }
        }

        if (monitored_array.size() < servers.size())
        {
            monitored_array.clear();
        }
    }
    else
    {
        MXS_ERROR(
            "Serverlist setting '%s' contains invalid server name '%s'.", key.c_str(), name_error.c_str());
        *error_out = true;
    }

    return monitored_array;
}

bool Monitor::set_disk_space_threshold(const string& dst_setting)
{
    mxb_assert(!is_running());
    DiskSpaceLimits new_dst;
    bool rv = config_parse_disk_space_threshold(&new_dst, dst_setting.c_str());
    if (rv)
    {
        m_settings.shared.monitor_disk_limits = new_dst;
    }
    return rv;
}

bool Monitor::can_be_disabled(const MonitorServer& server, DisableType type, std::string* errmsg_out) const
{
    return true;
}

bool Monitor::set_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    MonitorServer* msrv = get_monitored_server(srv);
    mxb_assert(msrv);

    if (!msrv)
    {
        MXS_ERROR("Monitor %s requested to set status of server %s that it does not monitor.",
            name(),
            srv->address());
        return false;
    }

    bool written = false;

    if (is_running())
    {
        /* This server is monitored, in which case modifying any other status bit than Maintenance is
         * disallowed. */
        if (bit & ~(SERVER_MAINT | SERVER_DRAINING))
        {
            MXS_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else
        {
            /* Maintenance and being-drained are set/cleared using a special variable which the
             * monitor reads when starting the next update cycle. */
            MonitorServer::StatusRequest request;
            auto type = DisableType::DRAIN;
            if (bit & SERVER_MAINT)
            {
                request = MonitorServer::MAINT_ON;
                type    = DisableType::MAINTENANCE;
            }
            else
            {
                mxb_assert(bit & SERVER_DRAINING);
                request = MonitorServer::DRAINING_ON;
            }

            if (can_be_disabled(*msrv, type, errmsg_out))
            {
                msrv->add_status_request(request);
                written = true;

                // Wait until the monitor picks up the change
                wait_for_status_change();
            }
        }
    }
    else
    {
        /* The monitor is not running, the bit can be set directly */
        srv->set_status(bit);
        written = true;
    }

    return written;
}

bool Monitor::clear_server_status(SERVER* srv, int bit, string* errmsg_out)
{
    MonitorServer* msrv = get_monitored_server(srv);
    mxb_assert(msrv);

    if (!msrv)
    {
        MXS_ERROR("Monitor %s requested to clear status of server %s that it does not monitor.",
            name(),
            srv->address());
        return false;
    }

    bool written = false;

    if (is_running())
    {
        if (bit & ~(SERVER_MAINT | SERVER_DRAINING))
        {
            MXS_ERROR(ERR_CANNOT_MODIFY);
            if (errmsg_out)
            {
                *errmsg_out = ERR_CANNOT_MODIFY;
            }
        }
        else
        {
            MonitorServer::StatusRequest request;
            if (bit & SERVER_MAINT)
            {
                request = MonitorServer::MAINT_OFF;
            }
            else
            {
                mxb_assert(bit & SERVER_DRAINING);
                request = MonitorServer::DRAINING_OFF;
            }

            msrv->add_status_request(request);
            written = true;

            // Wait until the monitor picks up the change
            wait_for_status_change();
        }
    }
    else
    {
        /* The monitor is not running, the bit can be cleared directly */
        srv->clear_status(bit);
        written = true;
    }

    return written;
}

void Monitor::populate_services()
{
    mxb_assert(!is_running());

    for (MonitorServer* pMs : m_servers)
    {
        service_add_server(this, pMs->server);
    }
}

void Monitor::deactivate()
{
    if (is_running())
    {
        stop();
    }
    remove_all_servers();
}

bool Monitor::check_disk_space_this_tick()
{
    bool should_update_disk_space = false;
    auto check_interval           = m_settings.disk_space_check_interval;

    if ((check_interval.count() > 0) && m_disk_space_checked.split() > check_interval)
    {
        should_update_disk_space = true;
        // Whether or not disk space check succeeds, reset the timer. This way, disk space is always
        // checked during the same tick for all servers.
        m_disk_space_checked.restart();
    }
    return should_update_disk_space;
}

bool Monitor::server_status_request_waiting() const
{
    return m_status_change_pending.load(std::memory_order_acquire);
}

const Monitor::ServerVector& Monitor::servers() const
{
    return m_servers;
}

std::vector<SERVER*> Monitor::real_servers() const
{
    std::vector<SERVER*> rval(m_servers.size());
    std::transform(m_servers.begin(), m_servers.end(), rval.begin(), std::mem_fn(&MonitorServer::server));
    return rval;
}

MonitorServer* Monitor::create_server(SERVER* server, const MonitorServer::SharedSettings& shared)
{
    return new MonitorServer(server, shared);
}

MonitorWorker::MonitorWorker(const string& name, const string& module)
    : Monitor(name, module)
    , m_thread_running(false)
    , m_shutdown(0)
    , m_checked(false)
    , m_loop_called(get_time_ms())
{}

bool MonitorWorker::is_running() const
{
    return Worker::state() != Worker::STOPPED && Worker::state() != Worker::FINISHED;
}

void MonitorWorker::do_stop()
{
    // This should only be called by monitor_stop().
    mxb_assert(Monitor::is_main_worker());
    mxb_assert(is_running());
    mxb_assert(m_thread_running.load() == true);

    Worker::shutdown();
    Worker::join();
    m_thread_running.store(false, std::memory_order_release);
}

json_t* MonitorWorker::diagnostics() const
{
    return json_object();
}

json_t* MonitorWorker::diagnostics(MonitorServer* server) const
{
    return json_object();
}

bool MonitorWorker::start()
{
    // This should only be called by monitor_start(). NULL worker is allowed since the main worker may
    // not exist during program start/stop.
    mxb_assert(Monitor::is_main_worker());
    mxb_assert(!is_running());
    mxb_assert(m_thread_running.load() == false);

    if (journal_is_stale())
    {
        MXS_NOTICE("Removing stale journal file for monitor '%s'.", name());
        remove_server_journal();
    }

    if (!m_checked)
    {
        if (!has_sufficient_permissions())
        {
            MXS_ERROR("Failed to start monitor. See earlier errors for more information.");
        }
        else
        {
            m_checked = true;
        }
    }

    bool started = false;
    if (m_checked)
    {
        m_loop_called = get_time_ms() - settings().interval;  // Next tick should happen immediately.
        if (!Worker::start())
        {
            MXS_ERROR("Failed to start worker for monitor '%s'.", name());
        }
        else
        {
            // Ok, so the thread started. Let's wait until we can be certain the
            // state has been updated.
            m_semaphore.wait();

            started = m_thread_running.load(std::memory_order_acquire);
            if (!started)
            {
                // Ok, so the initialization failed and the thread will exit.
                // We need to wait on it so that the thread resources will not leak.
                Worker::join();
            }
        }
    }
    return started;
}

// static
int64_t MonitorWorker::get_time_ms()
{
    timespec t;

    MXB_AT_DEBUG(int rv =) clock_gettime(CLOCK_MONOTONIC_COARSE, &t);
    mxb_assert(rv == 0);

    return t.tv_sec * 1000 + (t.tv_nsec / 1000000);
}

bool MonitorServer::can_update_disk_space_status() const
{
    return m_ok_to_check_disk_space
        && (!m_shared.monitor_disk_limits.empty() || server->have_disk_space_limits());
}

void MonitorServer::update_disk_space_status()
{
    auto pMs = this;  // TODO: Clean
    std::map<std::string, disk::SizesAndName> info;

    int rv = disk::get_info_by_path(pMs->con, &info);

    if (rv == 0)
    {
        // Server-specific setting takes precedence.
        auto dst = pMs->server->get_disk_space_limits();
        if (dst.empty())
        {
            dst = m_shared.monitor_disk_limits;
        }

        bool disk_space_exhausted   = false;
        int32_t star_max_percentage = -1;
        std::set<std::string> checked_paths;

        for (const auto& dst_item : dst)
        {
            string path            = dst_item.first;
            int32_t max_percentage = dst_item.second;

            if (path == "*")
            {
                star_max_percentage = max_percentage;
            }
            else
            {
                auto j = info.find(path);

                if (j != info.end())
                {
                    const disk::SizesAndName& san = j->second;

                    disk_space_exhausted = check_disk_space_exhausted(pMs, path, san, max_percentage);
                    checked_paths.insert(path);
                }
                else
                {
                    MXS_WARNING("Disk space threshold specified for %s even though server %s at %s"
                                "does not have that.",
                        path.c_str(),
                        pMs->server->name(),
                        pMs->server->address());
                }
            }
        }

        if (star_max_percentage != -1)
        {
            for (auto j = info.begin(); j != info.end(); ++j)
            {
                string path = j->first;

                if (checked_paths.find(path) == checked_paths.end())
                {
                    const disk::SizesAndName& san = j->second;

                    disk_space_exhausted = check_disk_space_exhausted(pMs, path, san, star_max_percentage);
                }
            }
        }

        if (disk_space_exhausted)
        {
            pMs->pending_status |= SERVER_DISK_SPACE_EXHAUSTED;
        }
        else
        {
            pMs->pending_status &= ~SERVER_DISK_SPACE_EXHAUSTED;
        }
    }
    else
    {
        SERVER* pServer = pMs->server;

        if (mysql_errno(pMs->con) == ER_UNKNOWN_TABLE)
        {
            // Disable disk space checking for this server.
            m_ok_to_check_disk_space = false;

            MXS_ERROR("Disk space cannot be checked for %s at %s, because either the "
                      "version (%s) is too old, or the DISKS information schema plugin "
                      "has not been installed. Disk space checking has been disabled.",
                pServer->name(),
                pServer->address(),
                pServer->info().version_string());
        }
        else
        {
            MXS_ERROR("Checking the disk space for %s at %s failed due to: (%d) %s",
                pServer->name(),
                pServer->address(),
                mysql_errno(pMs->con),
                mysql_error(pMs->con));
        }
    }
}

bool MonitorWorker::configure(const mxs::ConfigParameters* pParams)
{
    return Monitor::configure(pParams);
}

bool MonitorWorker::has_sufficient_permissions()
{
    return true;
}

void MonitorWorker::flush_server_status()
{
    for (MonitorServer* pMs : servers())
    {
        if (!pMs->server->is_in_maint())
        {
            pMs->server->assign_status(pMs->pending_status);
        }
    }
}

void MonitorWorkerSimple::pre_loop()
{
    m_master = nullptr;
    load_server_journal(&m_master);
    // Add another overridable function for derived classes (e.g. pre_loop_monsimple) if required.
}

void MonitorWorkerSimple::post_loop() {}

void MonitorWorkerSimple::pre_tick() {}

void MonitorWorkerSimple::post_tick() {}

void MonitorWorkerSimple::tick()
{
    check_maintenance_requests();
    pre_tick();

    const bool should_update_disk_space = check_disk_space_this_tick();

    for (MonitorServer* pMs : servers())
    {
        if (!pMs->server->is_in_maint())
        {
            pMs->mon_prev_status = pMs->server->status();
            pMs->pending_status  = pMs->server->status();

            ConnectResult rval = pMs->ping_or_connect();

            if (connection_is_ok(rval))
            {
                pMs->maybe_fetch_session_track();
                pMs->clear_pending_status(SERVER_AUTH_ERROR);
                pMs->set_pending_status(SERVER_RUNNING);

                if (should_update_disk_space && pMs->can_update_disk_space_status())
                {
                    pMs->update_disk_space_status();
                }

                update_server_status(pMs);
            }
            else
            {
                /**
                 * TODO: Move the bits that do not represent a state out of
                 * the server state bits. This would allow clearing the state by
                 * zeroing it out.
                 */
                pMs->clear_pending_status(MonitorServer::SERVER_DOWN_CLEAR_BITS);

                if (rval == ConnectResult::ACCESS_DENIED)
                {
                    pMs->set_pending_status(SERVER_AUTH_ERROR);
                }

                if (pMs->status_changed() && pMs->should_print_fail_status())
                {
                    pMs->log_connect_error(rval);
                }
            }

            if (pMs->server->is_down())
            {
                pMs->mon_err_count += 1;
            }
            else
            {
                pMs->mon_err_count = 0;
            }
        }
    }

    post_tick();

    flush_server_status();
    process_state_changes();
    hangup_failed_servers();
    store_server_journal(m_master);
}

void MonitorWorker::pre_loop() {}

void MonitorWorker::post_loop() {}

void MonitorWorker::process_state_changes()
{
    detect_handle_state_changes();
}

bool MonitorWorker::pre_run()
{
    bool rv = false;
    m_ticks.store(0, std::memory_order_release);

    if (mysql_thread_init() == 0)
    {
        rv = true;
        // Write and post the semaphore to signal the admin thread that the start is succeeding.
        m_thread_running.store(true, std::memory_order_release);
        m_semaphore.post();

        pre_loop();
        delayed_call(1, &MonitorWorker::call_run_one_tick, this);
    }
    else
    {
        MXS_ERROR("mysql_thread_init() failed for %s. The monitor cannot start.", name());
        m_semaphore.post();
    }

    return rv;
}

void MonitorWorker::post_run()
{
    post_loop();

    mysql_thread_end();
}

bool MonitorWorker::call_run_one_tick(Worker::Call::action_t action)
{
    /** This is both the minimum sleep between two ticks and also the maximum time between early
     *  wakeup checks. */
    const int base_interval_ms = 100;
    if (action == Worker::Call::EXECUTE)
    {
        int64_t now = get_time_ms();
        // Enough time has passed,
        if ((now - m_loop_called > settings().interval)
            // or a server status change request is waiting,
            || server_status_request_waiting()
            // or a monitor-specific condition is met.
            || immediate_tick_required())
        {
            m_loop_called = now;
            run_one_tick();
            now = get_time_ms();
        }

        int64_t ms_to_next_call = settings().interval - (now - m_loop_called);
        // ms_to_next_call will be negative, if the run_one_tick() call took
        // longer than one monitor interval.
        int64_t delay = ((ms_to_next_call <= 0) || (ms_to_next_call >= base_interval_ms)) ? base_interval_ms
                                                                                          : ms_to_next_call;

        delayed_call(delay, &MonitorWorker::call_run_one_tick, this);
    }
    return false;
}

void MonitorWorker::run_one_tick()
{
    tick();
    m_ticks.store(ticks() + 1, std::memory_order_release);
}

bool MonitorWorker::immediate_tick_required()
{
    bool rval = false;
    if (m_immediate_tick_requested.load(std::memory_order_relaxed))
    {
        m_immediate_tick_requested.store(false, std::memory_order_relaxed);
        rval = true;
    }
    return rval;
}

void MonitorWorker::request_immediate_tick()
{
    m_immediate_tick_requested.store(true, std::memory_order_relaxed);
}

MonitorServer::MonitorServer(SERVER* server, const SharedSettings& shared)
    : server(server)
    , m_shared(shared)
{
    // Initialize 'm_last_session_track_update' so that an update is performed 1s after monitor start.
    m_last_session_track_update = mxb::Clock::now() - session_track_update_interval + 1s;
}

MonitorServer::~MonitorServer()
{
    if (con)
    {
        mysql_close(con);
    }
}

void MonitorServer::apply_status_requests()
{
    // The admin can only modify the [Maintenance] and [Drain] bits.
    int admin_msg = m_status_request.exchange(NO_CHANGE, std::memory_order_acq_rel);

    switch (admin_msg)
    {
    case MonitorServer::MAINT_ON:
        server->set_status(SERVER_MAINT);
        break;

    case MonitorServer::MAINT_OFF:
        server->clear_status(SERVER_MAINT);
        break;

    case MonitorServer::DRAINING_ON:
        server->set_status(SERVER_DRAINING);
        break;

    case MonitorServer::DRAINING_OFF:
        server->clear_status(SERVER_DRAINING);
        break;

    case MonitorServer::NO_CHANGE:
        break;

    default:
        mxb_assert(!true);
    }
}

void MonitorServer::add_status_request(StatusRequest request)
{
    int previous_request = m_status_request.exchange(request, std::memory_order_acq_rel);
    // Warn if the previous request hasn't been read.
    if (previous_request != NO_CHANGE)
    {
        MXS_WARNING(WRN_REQUEST_OVERWRITTEN);
    }
}

bool MonitorServer::is_database() const
{
    return server->info().is_database();
}

void MonitorServer::maybe_fetch_session_track()
{
    if (should_fetch_session_track())
    {
        fetch_session_track();
    }
}
}  // namespace maxscale

const MXS_MODULE_PARAM* common_monitor_params()
{
    static const MXS_MODULE_PARAM config_monitor_params[]
        = {{CN_TYPE, MXS_MODULE_PARAM_STRING, CN_MONITOR, MXS_MODULE_OPT_REQUIRED},
            {CN_MODULE, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {CN_USER, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {CN_PASSWORD, MXS_MODULE_PARAM_PASSWORD, NULL, MXS_MODULE_OPT_REQUIRED},
            {CN_SERVERS, MXS_MODULE_PARAM_SERVERLIST},
            {CN_MONITOR_INTERVAL, MXS_MODULE_PARAM_DURATION, "2000ms"},
            {CN_BACKEND_CONNECT_TIMEOUT, MXS_MODULE_PARAM_DURATION, "3s", MXS_MODULE_OPT_DURATION_S},
            {CN_BACKEND_READ_TIMEOUT, MXS_MODULE_PARAM_DURATION, "3s", MXS_MODULE_OPT_DURATION_S},
            {CN_BACKEND_WRITE_TIMEOUT, MXS_MODULE_PARAM_DURATION, "3s", MXS_MODULE_OPT_DURATION_S},
            {CN_BACKEND_CONNECT_ATTEMPTS, MXS_MODULE_PARAM_COUNT, "1"},
            {CN_JOURNAL_MAX_AGE, MXS_MODULE_PARAM_DURATION, "28800s", MXS_MODULE_OPT_DURATION_S},
            {CN_DISK_SPACE_THRESHOLD, MXS_MODULE_PARAM_STRING},
            {CN_DISK_SPACE_CHECK_INTERVAL, MXS_MODULE_PARAM_DURATION, "0ms"},
            // Cannot be a path type as the script may have parameters
            {CN_SCRIPT, MXS_MODULE_PARAM_STRING},
            {CN_SCRIPT_TIMEOUT, MXS_MODULE_PARAM_DURATION, "90s", MXS_MODULE_OPT_DURATION_S},
            {CN_EVENTS,
                MXS_MODULE_PARAM_ENUM,
                monitor_event_default.name,
                MXS_MODULE_OPT_NONE,
                monitor_event_values},
            {NULL}};
    return config_monitor_params;
}

mxs::Monitor::Test::Test(mxs::Monitor* monitor)
    : m_monitor(monitor)
{}

mxs::Monitor::Test::~Test() {}

void mxs::Monitor::Test::remove_servers()
{
    // Copy SERVERs before removing from monitor
    std::vector<SERVER*> copy;
    for (auto ms : m_monitor->m_servers)
    {
        copy.push_back(ms->server);
    }

    m_monitor->remove_all_servers();
    for (auto srv : copy)
    {
        delete srv;  // MonitorServer dtor doesn't delete the base server.
    }
}

void mxs::Monitor::Test::add_server(SERVER* new_server)
{
    m_monitor->add_server(new_server);
}
