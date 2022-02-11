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

#include <maxscale/ccdefs.hh>

#include <maxscale/cn_strings.hh>
#include <maxscale/json.hh>
#include <maxscale/paths.hh>
#include <maxscale/secrets.hh>
#include <maxscale/utils.hh>
#include <maxbase/json.hh>

#include "internal/config.hh"
#include "internal/configmanager.hh"
#include "internal/config_runtime.hh"
#include "internal/servermanager.hh"
#include "internal/monitormanager.hh"
#include "internal/modules.hh"

#include <set>
#include <fstream>
#include <mysqld_error.h>

using namespace std::chrono;

namespace
{
const char CN_CHECKSUM[] = "checksum";
const char CN_CLUSTER_NAME[] = "cluster_name";
const char CN_CONFIG[] = "config";
const char CN_VERSION[] = "version";
const char CN_NODES[] = "nodes";
const char CN_ORIGIN[] = "origin";
const char CN_STATUS[] = "status";

const char STATUS_OK[] = "OK";
const char SCOPE_NAME[] = "ConfigManager";
const char TABLE[] = "mysql.maxscale_config";

struct ThisUnit
{
    mxs::ConfigManager* manager {nullptr};
};

ThisUnit this_unit;

// It's possible for the configuration data to contain single quotes (e.g. in a password or a regex). Since
// we're using single quotes for delimiting strings, we must escape them. Using double quotes isn't a
// realistic option as the JSON data is full of them.
std::string escape_for_sql(const std::string& str)
{
    auto sql = str;
    size_t pos = sql.find('\'');

    while (pos != std::string::npos)
    {
        sql.replace(pos, 1, "\\'");
        pos = sql.find('\'', pos + 2);
    }

    return sql;
}

const std::string& hostname()
{
    return mxs::Config::get().nodename;
}

std::string sql_create_table(int max_len)
{
    std::ostringstream ss;
    ss << "CREATE TABLE IF NOT EXISTS " << TABLE << "("
       << "cluster VARCHAR(" << max_len << ") PRIMARY KEY, "
       << "version BIGINT NOT NULL, "
       << "config JSON NOT NULL, "
       << "origin VARCHAR(254) NOT NULL, "
       << "nodes JSON NOT NULL"
       << ") ENGINE=InnoDB";
    return ss.str();
}

std::string sql_insert(const std::string& cluster, int64_t version, const std::string& payload)
{
    const auto& host = hostname();
    std::ostringstream ss;

    ss << "INSERT INTO " << TABLE << "(cluster, version, config, origin, nodes) VALUES ("
       << "'" << escape_for_sql(cluster) << "', "
       << version + 1 << ", "
       << "'" << escape_for_sql(payload) << "', "
       << "'" << host << "', "
       << "JSON_OBJECT('" << host << "', '" << STATUS_OK << "')"
       << ")";

    return ss.str();
}

std::string sql_update(const std::string& cluster, int64_t version, const std::string& payload)
{
    const auto& host = hostname();
    std::ostringstream ss;

    ss << "UPDATE " << TABLE << " SET version = version + 1, "
       << "config = '" << escape_for_sql(payload) << "', "
       << "origin = '" << host << "' ,"
       << "nodes = JSON_OBJECT('" << host << "', '" << STATUS_OK << "')"
       << "WHERE version = " << version << " AND cluster = '" << escape_for_sql(cluster) << "'";

    return ss.str();
}

std::string sql_select_for_update(const std::string& cluster)
{
    std::ostringstream ss;
    ss << "SELECT version FROM " << TABLE << " WHERE "
       << "cluster = '" << escape_for_sql(cluster) << "' FOR UPDATE";
    return ss.str();
}

std::string sql_select_version(const std::string& cluster)
{
    std::ostringstream ss;
    ss << "SELECT version, nodes FROM " << TABLE << " WHERE cluster = '" << escape_for_sql(cluster) << "'";
    return ss.str();
}

std::string sql_select_config(const std::string& cluster, int64_t version)
{
    std::ostringstream ss;
    ss << "SELECT config, version, origin FROM " << TABLE << " WHERE "
       << "version > " << version << " AND cluster = '" << escape_for_sql(cluster) << "'";
    return ss.str();
}

std::string sql_update_status(const std::string& cluster, int64_t version, const std::string& status)
{
    mxb_assert(escape_for_sql(hostname()) == hostname());

    std::ostringstream ss;
    ss << "UPDATE " << TABLE << " SET nodes = JSON_SET"
       << "(nodes, CONCAT('$.', JSON_QUOTE('" << hostname() << "')), '" << escape_for_sql(status) << "') "
       << "WHERE version = " << version << " AND cluster = '" << escape_for_sql(cluster) << "'";
    return ss.str();
}
}

namespace maxscale
{

// static
ConfigManager* ConfigManager::get()
{
    return this_unit.manager;
}

ConfigManager::ConfigManager(mxs::MainWorker* main_worker)
    : m_worker(main_worker)
    , m_status_msg(STATUS_OK)
{
    mxb_assert(!this_unit.manager);
    this_unit.manager = this;
}

ConfigManager::~ConfigManager()
{
    mxb_assert(this_unit.manager == this);
    mxb_assert_message(m_dcid == 0, "Sync should be off when ConfigManager is destroyed");
}

void ConfigManager::reconnect()
{
    m_reconnect = true;
}

bool ConfigManager::sync_callback(mxb::Worker::Call::action_t action)
{
    if (action == mxb::Worker::Call::EXECUTE)
    {
        sync();

        m_dcid = m_worker->delayed_call(mxs::Config::get().config_sync_interval,
                                        &ConfigManager::sync_callback, this);
    }
    else
    {
        m_dcid = 0;
    }

    return false;
}

void ConfigManager::start_sync()
{
    m_dcid = m_worker->delayed_call(mxs::Config::get().config_sync_interval,
                                    &ConfigManager::sync_callback, this);

    // Queue a sync to take place right after startup
    queue_sync();
}

void ConfigManager::stop_sync()
{
    mxb_assert(mxs::MainWorker::is_main_worker());

    if (m_dcid)
    {
        m_worker->cancel_delayed_call(m_dcid);
        m_dcid = 0;
    }
}

void ConfigManager::refresh()
{
    m_current_config = create_config(m_version);
}

void ConfigManager::queue_sync()
{
    m_worker->execute(
        [this]() {
            sync();
        }, mxb::Worker::EXECUTE_QUEUED);
}

void ConfigManager::sync()
{
    mxb::LogScope scope(SCOPE_NAME);
    m_cluster = get_cluster();

    if (!m_cluster.empty())
    {
        int64_t next_version = m_version;

        try
        {
            auto config = fetch_config();

            if (config.valid())
            {
                next_version = config.get_int(CN_VERSION);
                MXS_NOTICE("Updating to configuration version %ld", next_version);

                process_config(config);

                // Config updated, save a local version of it.
                mxb_assert(config.get_int(CN_VERSION) > 0);
                save_config(config.to_string(mxb::Json::Format::COMPACT));

                // TODO: If we fail to apply the new configuration, we could try to wipe out all existing
                // objects and apply it again. This should always succeed if only valid states are stored in
                // the cluster. If someone manually introduced a bad configuration or some bug causes a bad
                // state to be generated, this might cause a complete outage for a brief period of time.

                // Runtime state updated and config cached on disk, the config change was successful
                m_version = next_version;
                m_current_config = std::move(config);
                m_log_sync_error = true;

                try_update_status(STATUS_OK);
            }
        }
        catch (const ConfigManager::Exception& e)
        {
            if (m_log_sync_error)
            {
                MXS_ERROR("Failed to sync configuration: %s", e.what());
                m_log_sync_error = false;
            }

            try_update_status(e.what());

            if (next_version > m_version)
            {
                if (revert_changes())
                {
                    MXS_WARNING("Successfully reverted the failed configuration change, "
                                "ignoring configuration version %ld.", next_version);
                }

                // Regardless of what happens, re-calculate the current configuration and update the version.
                // This will prevent repeated attempts to apply the same configuration while still allowing
                // the internal state to be correctly represented by m_current_config.
                m_version = next_version;
                m_current_config = create_config(m_version);
            }
        }
    }
}

bool ConfigManager::revert_changes()
{
    bool rval = false;

    try
    {
        // Try to revert any changes that might've been done
        auto prev_config = std::move(m_current_config);
        m_current_config = create_config(m_version);
        process_config(prev_config);
        rval = true;
    }
    catch (const ConfigManager::Exception& e)
    {
        MXS_ERROR("Failed to revert the failed configuration change, the MaxScale configuration "
                  "is in an indeterminate state. The error that caused the failure was: %s",
                  e.what());

        if (discard_config())
        {
            MXS_ALERT("Aborting the MaxScale process...");
            raise(SIGABRT);
        }
        else
        {
            MXS_ERROR("Cached configuration was not removed, cannot safely abort the process.");
        }
    }

    return rval;
}

bool ConfigManager::load_cached_config()
{
    mxb::LogScope scope(SCOPE_NAME);
    bool have_config = false;
    std::string filename = dynamic_config_filename();
    m_cluster = get_cluster();

    // Check only if the file exists. If it does, try to load it.
    if (!m_cluster.empty() && access(filename.c_str(), F_OK) == 0)
    {
        mxb::Json new_json(mxb::Json::Type::UNDEFINED);

        if (new_json.load(filename))
        {
            std::string cluster_name = new_json.get_string(CN_CLUSTER_NAME);
            int64_t version = new_json.get_int(CN_VERSION);

            if (cluster_name == m_cluster)
            {
                MXS_NOTICE("Using cached configuration for cluster '%s', version %ld: %s",
                           cluster_name.c_str(), version, filename.c_str());

                m_current_config = std::move(new_json);
                have_config = true;
            }
            else
            {
                MXS_WARNING("Found cached configuration for cluster '%s' when configured "
                            "to use cluster '%s', ignoring the cached configuration: %s",
                            cluster_name.c_str(), m_cluster.c_str(), filename.c_str());
            }
        }
    }

    return have_config;
}

ConfigManager::Startup ConfigManager::process_cached_config()
{
    mxb::LogScope scope(SCOPE_NAME);
    Startup status = Startup::OK;

    try
    {
        mxb::Json config = std::move(m_current_config);

        // Storing an empty object in the current JSON will cause all objects to be treated as new.
        m_current_config = mxb::Json(mxb::Json::Type::OBJECT);

        process_config(config);

        if (!MonitorManager::find_monitor(m_cluster.c_str()))
        {
            throw error("Cluster '", m_cluster, "' is not a part of the cached configuration");
        }

        m_version = config.get_int(CN_VERSION);
        m_current_config = std::move(config);
    }
    catch (const ConfigManager::Exception& e)
    {
        MXS_ERROR("Failed to apply cached configuration: %s", e.what());
        status = discard_config() ? Startup::RESTART : Startup::ERROR;

        // Reset the current configuration to signal that it must be recreated in the next process_config call
        m_current_config.reset();
    }

    return status;
}

bool ConfigManager::start()
{
    mxb::LogScope scope(SCOPE_NAME);
    bool ok = true;
    m_cluster = get_cluster();

    if (!m_cluster.empty())
    {
        try
        {
            if (!m_current_config.valid())
            {
                // If we're using the static configuration, the initial configuration must be created. This
                // makes sure that if the operation doesn't change anything or it changes config_sync_cluster,
                // it won't be sent to the cluster.
                m_current_config = create_config(m_version);
            }

            verify_sync();
        }
        catch (const Exception& e)
        {
            MXS_ERROR("Cannot start configuration change: %s", e.what());
            ok = false;
            rollback();
        }
    }

    return ok;
}

void ConfigManager::rollback()
{
    mxb::LogScope scope(SCOPE_NAME);
    if (!m_cluster.empty())
    {
        m_conn.cmd("ROLLBACK");
    }
}

bool ConfigManager::commit()
{
    mxb::LogScope scope(SCOPE_NAME);
    if (m_cluster.empty())
    {
        return true;
    }

    bool ok = false;

    try
    {
        mxb::Json config = create_config(m_version + 1);

        if (config.get_object(CN_CONFIG) == m_current_config.get_object(CN_CONFIG))
        {
            MXS_INFO("Resulting configuration is the same as current configuration, ignoring update.");
            rollback();
            return true;
        }

        std::string payload = config.to_string(mxb::Json::Format::COMPACT);
        update_config(payload);
        mxb_assert(config.get_int(CN_VERSION) > 0);
        save_config(payload);

        // Config successfully updated in the cluster and cached locally
        m_current_config = std::move(config);
        m_status_msg = STATUS_OK;
        m_origin = hostname();
        ++m_version;
        ok = true;
    }
    catch (const Exception& e)
    {
        MXS_ERROR("Cannot complete configuration change: %s", e.what());
        rollback();

        // Try to revert any changes that were applied
        revert_changes();
        m_current_config = create_config(m_version);
    }

    return ok;
}

std::string ConfigManager::checksum() const
{
    std::string rval;

    if (m_current_config)
    {
        auto cnf = m_current_config.get_object(CN_CONFIG).to_string(mxb::Json::Format::COMPACT);
        rval = mxs::checksum<mxs::SHA1Checksum>(cnf);
    }

    return rval;
}

mxb::Json ConfigManager::to_json() const
{
    mxb::Json obj;

    // It's possible for m_current_config to be valid and m_version to be 0 if no actual changes have been
    // made but modules were reconfigured. This can happen for example when the config_sync_cluster is changed
    // before any other modifications have been done.
    bool enabled = !get_cluster().empty() && m_current_config.valid() && m_version;

    if (enabled)
    {
        obj.set_string(CN_CHECKSUM, checksum());
        obj.set_int(CN_VERSION, m_version);
        obj.set_object(CN_NODES, m_nodes);
        obj.set_string(CN_ORIGIN, m_origin);
        obj.set_string(CN_STATUS, m_status_msg);
    }
    else
    {
        obj = mxb::Json(mxb::Json::Type::JSON_NULL);
    }

    return obj;
}

void ConfigManager::save_config(const std::string& payload)
{
    std::string filename = dynamic_config_filename();
    std::string tmpname = filename + ".tmp";
    std::ofstream file(tmpname);

    if (!file.write(payload.c_str(), payload.size()) || !file.flush()
        || rename(tmpname.c_str(), filename.c_str()) != 0)
    {
        MXS_WARNING("Failed to save configuration at '%s': %d, %s",
                    filename.c_str(), errno, mxb_strerror(errno));
    }
}

bool ConfigManager::discard_config()
{
    bool discarded = false;
    std::string old_name = dynamic_config_filename();
    std::string new_name = old_name + ".bad-config";

    if (rename(old_name.c_str(), new_name.c_str()) == 0)
    {
        MXS_ERROR("Renamed cached configuration, using static configuration on next startup. "
                  "A copy of the bad cached configuration is stored at: %s", new_name.c_str());
        discarded = true;
    }
    else
    {
        if (errno == ENOENT)
        {
            // If the file doesn't exist, this means we're attempting to sync with the cluster using the
            // static configuration. If this transition fails, we must not trigger a restart as that'll cause
            // an endless restart loop.
        }
        else
        {
            MXS_ALERT("Failed to rename cached configuration file at '%s': %d, %s.",
                      old_name.c_str(), errno, mxs_strerror(errno));

            if (unlink(old_name.c_str()) == 0)
            {
                MXS_ERROR("Removed cached configuration, using static configuration on next startup.");
                discarded = true;
            }
            else
            {
                MXS_ALERT("Failed to discard bad cached configuration file at '%s': %d, %s.",
                          old_name.c_str(), errno, mxs_strerror(errno));
            }
        }
    }

    return discarded;
}

mxb::Json ConfigManager::create_config(int64_t version)
{
    bool mask = config_mask_passwords();
    config_set_mask_passwords(false);
    mxb::Json arr(mxb::Json::Type::ARRAY);

    append_config(arr.get_json(), ServerManager::server_list_to_json(""));
    append_config(arr.get_json(), MonitorManager::monitor_list_to_json(""));
    append_config(arr.get_json(), service_list_to_json(""));
    append_config(arr.get_json(), FilterDef::filter_list_to_json(""));
    append_config(arr.get_json(), Listener::to_json_collection(""));
    append_config(arr.get_json(), remove_local_parameters(config_maxscale_to_json("")));

    mxb::Json rval(mxb::Json::Type::OBJECT);

    rval.set_object(CN_CONFIG, arr);
    rval.set_int(CN_VERSION, version);

    mxb_assert(!m_cluster.empty());
    rval.set_string(CN_CLUSTER_NAME, m_cluster);

    config_set_mask_passwords(mask);
    return rval;
}

void ConfigManager::process_config(const mxb::Json& new_json)
{
    if (!m_current_config.valid())
    {
        // If we're using the static configuration, create the initial configuration before starting the
        // processing of the configuration. This makes sure that m_current_config represents the current state
        // before we start comparing to it.
        m_current_config = create_config(m_version);
    }

    mxb_assert(new_json.valid());

    std::set<std::string> old_names;
    std::set<std::string> new_names;

    auto new_objects = new_json.get_array_elems(CN_CONFIG);
    auto old_objects = m_current_config.get_array_elems(CN_CONFIG);

    for (const auto& obj : new_objects)
    {
        new_names.insert(obj.get_string(CN_ID));
    }

    for (const auto& obj : old_objects)
    {
        old_names.insert(obj.get_string(CN_ID));
    }

    std::set<std::string> removed;
    std::set_difference(old_names.begin(), old_names.end(),
                        new_names.begin(), new_names.end(),
                        std::inserter(removed, removed.begin()));

    std::set<std::string> added;
    std::set_difference(new_names.begin(), new_names.end(),
                        old_names.begin(), old_names.end(),
                        std::inserter(added, added.begin()));


    for (const auto& obj : new_objects)
    {
        auto name = obj.get_string(CN_ID);

        if (added.find(name) == added.end())
        {
            // This is an existing object, check if it has been destroyed and then created again. This can be
            // detected by changes in the object type, the module it uses or any parameter that cannot be
            // modified at runtime.
            auto it = std::find_if(old_objects.begin(), old_objects.end(), [&](const auto& o) {
                                       return o.get_string(CN_ID) == name;
                                   });
            mxb_assert(it != old_objects.end());

            std::ostringstream reason;

            if (!is_same_object(obj, *it, reason))
            {
                // A conflicting change was detected, add it to both the removed and added sets. This will
                // first destroy it and then recreate it using the new configuration.
                MXS_NOTICE("Recreating object '%s': %s", name.c_str(), reason.str().c_str());
                removed.insert(name);
                added.insert(name);
            }
        }
    }

    // Iterate the config in reverse to remove the objects in the reverse dependency order.
    for (auto it = old_objects.rbegin(); it != old_objects.rend(); ++it)
    {
        auto name = it->get_string(CN_ID);

        if (removed.find(name) != removed.end())
        {
            remove_old_object(name, it->get_string(CN_TYPE));
        }
    }

    for (auto& obj : new_objects)
    {
        auto name = obj.get_string(CN_ID);

        if (added.find(name) != added.end())
        {
            // Pass the object as a non-const reference in case it needs to be modified before use. For
            // all objects except listeners, the new object must be created without relationships to
            // make sure all objects exist before the links between them are established.
            create_new_object(name, obj.get_string(CN_TYPE), obj);
        }
    }

    for (const auto& obj : new_objects)
    {
        auto name = obj.get_string(CN_ID);
        auto type = obj.get_string(CN_TYPE);

        if (added.find(name) == added.end() || to_type(type) == Type::SERVICES)
        {
            update_object(name, type, obj);
        }
    }
}

ConfigManager::Type ConfigManager::to_type(const std::string& type)
{
    static const std::unordered_map<std::string, Type> types
    {
        {CN_SERVERS, Type::SERVERS},
        {CN_MONITORS, Type::MONITORS},
        {CN_SERVICES, Type::SERVICES},
        {CN_LISTENERS, Type::LISTENERS},
        {CN_FILTERS, Type::FILTERS},
        {CN_MAXSCALE, Type::MAXSCALE}
    };

    auto it = types.find(type);
    return it != types.end() ? it->second : Type::UNKNOWN;
}

bool ConfigManager::is_same_object(const mxb::Json& lhs, const mxb::Json& rhs, std::ostringstream& reason)
{
    bool rval = false;
    auto lhs_type = lhs.at(CN_TYPE);
    auto rhs_type = rhs.at(CN_TYPE);

    if (lhs_type == rhs_type)
    {
        std::ostringstream ss;
        mxs::ModuleType mod_type;

        switch (to_type(lhs.get_string(CN_TYPE)))
        {
        case Type::MONITORS:
            ss << CN_MODULE;
            mod_type = mxs::ModuleType::MONITOR;
            break;

        case Type::FILTERS:
            ss << CN_MODULE;
            mod_type = mxs::ModuleType::FILTER;
            break;

        case Type::SERVICES:
            ss << CN_ROUTER;
            mod_type = mxs::ModuleType::ROUTER;
            break;

        case Type::LISTENERS:
            ss << CN_PARAMETERS << "/" << CN_PROTOCOL;
            mod_type = mxs::ModuleType::PROTOCOL;
            break;

        case Type::SERVERS:
            // Servers are never recreated as all their parameters can be modified at runtime
            return true;
            break;

        case Type::MAXSCALE:
            // Only one MaxScale exists and it is only updated
            return true;

        case Type::UNKNOWN:
            mxb_assert_message(!true, "Unknown type of JSON: %s", rhs.to_string().c_str());
            // If this ever happens, the error is reported in the updating code.
            return true;
        }

        std::string mod_name = ss.str();
        auto lhs_attr = lhs.at(CN_ATTRIBUTES);
        auto rhs_attr = rhs.at(CN_ATTRIBUTES);
        auto lhs_module = lhs_attr.at(mod_name).get_string();
        auto rhs_module = rhs_attr.at(mod_name).get_string();

        // Checking that one of the modules is not empty makes it easier to write the code that handles the
        // module parameter checks. The lack of a module is detected later on when the update will fail.
        if (!lhs_module.empty() && lhs_module == rhs_module)
        {
            rval = same_unmodifiable_parameters(lhs_attr.at(CN_PARAMETERS),
                                                rhs_attr.at(CN_PARAMETERS),
                                                lhs_module, mod_type, reason);
        }
        else
        {
            reason << "module changed from '" << lhs_module << "' to '" << rhs_module << "'";
        }
    }
    else
    {
        reason << "object changed type from '" << lhs_type.get_string()
               << "' to '" << rhs_type.get_string() << "'";
    }

    return rval;
}

bool ConfigManager::same_unmodifiable_parameters(const mxb::Json& lhs_params, const mxb::Json& rhs_params,
                                                 const std::string& name, mxs::ModuleType type,
                                                 std::ostringstream& reason)
{
    bool rval = true;
    const MXS_MODULE* mod = get_module(name, type);
    mxb_assert_message(mod, "Could not find module '%s'", name.c_str());

    if (mod->specification)
    {
        for (const auto& param : *mod->specification)
        {
            if (!param.second->is_modifiable_at_runtime()
                && lhs_params.at(param.first) != rhs_params.at(param.first))
            {
                reason << "Parameter '" << param.first << "' is not the same in both configurations";
                rval = false;
            }
        }
    }
    else
    {
        // A module with no specification, not supported
    }

    return rval;
}

void ConfigManager::remove_old_object(const std::string& name, const std::string& type)
{
    switch (to_type(type))
    {
    case Type::SERVERS:
        if (auto* server = ServerManager::find_by_unique_name(name))
        {
            if (!runtime_destroy_server(server, true))
            {
                throw error("Failed to destroy server '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a server");
        }
        break;

    case Type::MONITORS:
        if (auto* monitor = MonitorManager::find_monitor(name.c_str()))
        {
            if (!runtime_destroy_monitor(monitor, true))
            {
                throw error("Failed to destroy monitor '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a monitor");
        }
        break;

    case Type::SERVICES:
        if (auto* service = Service::find(name))
        {
            if (!runtime_destroy_service(service, true))
            {
                throw error("Failed to destroy service '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a service");
        }
        break;

    case Type::LISTENERS:
        if (auto listener = listener_find(name))
        {
            if (!runtime_destroy_listener(listener))
            {
                throw error("Failed to destroy listener '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a listener");
        }
        break;

    case Type::FILTERS:
        if (auto filter = filter_find(name))
        {
            if (!runtime_destroy_filter(filter, true))
            {
                throw error("Failed to destroy filter '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a filter");
        }
        break;

    case Type::MAXSCALE:
    case Type::UNKNOWN:
        mxb_assert(!true);
        throw error("Found old object of unexpected type '", type, "': ", name);
        break;
    }
}

void ConfigManager::create_new_object(const std::string& name, const std::string& type, mxb::Json& obj)
{
    m_tmp.set_object(CN_DATA, obj);

    switch (to_type(type))
    {
    case Type::SERVERS:
        {
            // Hide the relationships for new objects, the relationships are handled in the update step.
            auto rel = obj.get_object(CN_RELATIONSHIPS);

            if (rel)
            {
                obj.erase(CN_RELATIONSHIPS);
            }

            if (!runtime_create_server_from_json(m_tmp.get_json()))
            {
                throw error("Failed to create server '", name, "'");
            }

            if (rel)
            {
                obj.set_object(CN_RELATIONSHIPS, rel);
            }
        }
        break;

    case Type::MONITORS:
        {
            // Hide the service relationship for new objects, it will be handled in the update step. Leaving
            // the servers relationship intact reduces the amount of messages that are logged.
            mxb::Json svc = obj.at("/relationships/services");

            if (svc)
            {
                obj.get_object(CN_RELATIONSHIPS).erase(CN_SERVICES);
            }

            if (!runtime_create_monitor_from_json(m_tmp.get_json()))
            {
                throw error("Failed to create monitor '", name, "'");
            }

            if (svc)
            {
                obj.get_object(CN_RELATIONSHIPS).set_object(CN_SERVICES, svc);
            }
        }
        break;

    case Type::SERVICES:
        {
            // Create services without relationships, they will be handled by the update step
            auto rel = obj.get_object(CN_RELATIONSHIPS);

            if (rel)
            {
                obj.erase(CN_RELATIONSHIPS);
            }

            if (!runtime_create_service_from_json(m_tmp.get_json()))
            {
                throw error("Failed to create service '", name, "'");
            }

            if (rel)
            {
                obj.set_object(CN_RELATIONSHIPS, rel);
            }
        }
        break;

    case Type::LISTENERS:
        if (!runtime_create_listener_from_json(m_tmp.get_json()))
        {
            throw error("Failed to create listener '", name, "'");
        }
        break;

    case Type::FILTERS:
        if (!runtime_create_filter_from_json(m_tmp.get_json()))
        {
            throw error("Failed to create filter '", name, "'");
        }
        break;

    case Type::MAXSCALE:
        // We'll end up here when we're loading a cached configuration
        mxb_assert(m_version == 0);
        break;

    case Type::UNKNOWN:
        mxb_assert(!true);
        throw error("Found new object of unexpected type '", type, "': ", name);
        break;
    }
}

void ConfigManager::update_object(const std::string& name, const std::string& type, const mxb::Json& json)
{
    m_tmp.set_object(CN_DATA, json);
    json_t* js = m_tmp.get_json();

    switch (to_type(type))
    {
    case Type::SERVERS:
        if (auto* server = ServerManager::find_by_unique_name(name))
        {
            if (!runtime_alter_server_from_json(server, js))
            {
                throw error("Failed to update server '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a server");
        }
        break;

    case Type::MONITORS:
        if (auto* monitor = MonitorManager::find_monitor(name.c_str()))
        {
            if (!runtime_alter_monitor_from_json(monitor, js))
            {
                throw error("Failed to update monitor '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a monitor");
        }
        break;

    case Type::SERVICES:
        if (auto* service = Service::find(name))
        {
            if (!runtime_alter_service_from_json(service, js))
            {
                throw error("Failed to update service '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a service");
        }
        break;

    case Type::LISTENERS:
        if (auto listener = listener_find(name))
        {
            if (!runtime_alter_listener_from_json(listener, js))
            {
                throw error("Failed to update listener '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a listener");
        }
        break;

    case Type::FILTERS:
        if (auto filter = filter_find(name))
        {
            if (!runtime_alter_filter_from_json(filter, js))
            {
                throw error("Failed to update filter '", name, "'");
            }
        }
        else
        {
            throw error("The object '", name, "' is not a filter");
        }
        break;

    case Type::MAXSCALE:
        if (!runtime_alter_maxscale_from_json(js))
        {
            throw error("Failed to configure global options");
        }
        break;

    case Type::UNKNOWN:
        mxb_assert(!true);
        throw error("Found object of unexpected type '", type, "': ", name);
        break;
    }
}

void ConfigManager::remove_extra_data(json_t* data)
{
    static const std::unordered_set<std::string> keys_to_keep {
        CN_PARAMETERS, CN_MODULE, CN_ROUTER
    };

    json_t* attr = json_object_get(data, CN_ATTRIBUTES);
    void* ptr;
    const char* key;
    json_t* value;

    json_object_foreach_safe(attr, ptr, key, value)
    {
        if (keys_to_keep.count(key) == 0)
        {
            json_object_del(attr, key);
        }
        else
        {
            mxs::json_remove_nulls(value);
        }
    }

    // Remove the links, we don't need them
    json_object_del(data, CN_LINKS);
}

void ConfigManager::append_config(json_t* arr, json_t* json)
{
    json_t* data = json_object_get(json, CN_DATA);

    if (json_is_array(data))
    {
        json_t* value;
        size_t i;

        json_array_foreach(data, i, value)
        {
            remove_extra_data(value);
        }

        json_array_extend(arr, data);
    }
    else
    {
        remove_extra_data(data);
        json_array_append(arr, data);
    }

    json_decref(json);
}

json_t* ConfigManager::remove_local_parameters(json_t* json)
{
    json_t* params = mxb::json_ptr(json, "/data/attributes/parameters");
    mxb_assert(params);

    json_object_del(params, CN_CONFIG_SYNC_CLUSTER);
    json_object_del(params, CN_CONFIG_SYNC_USER);
    json_object_del(params, CN_CONFIG_SYNC_PASSWORD);

    return json;
}

std::string ConfigManager::dynamic_config_filename() const
{
    return std::string(mxs::datadir()) + "/maxscale-config.json";
}

const std::string& ConfigManager::get_cluster() const
{
    return mxs::Config::get().config_sync_cluster;
}

SERVER* ConfigManager::get_server() const
{
    SERVER* rval = nullptr;
    auto monitor = MonitorManager::find_monitor(m_cluster.c_str());
    mxb_assert(monitor);

    for (const auto& server : monitor->servers())
    {
        if (server->server->is_master())
        {
            rval = server->server;
            break;
        }
    }

    return rval;
}

void ConfigManager::connect()
{
    SERVER* server = get_server();

    if (!server)
    {
        throw error("No valid servers in cluster '", m_cluster, "'.");
    }
    else if (server != m_server || m_reconnect)
    {
        // New server, close old connection
        m_conn.close();
        m_server = nullptr;
    }

    if (!m_conn.is_open() || !m_conn.ping())
    {
        const auto& config = mxs::Config::get();
        auto& cfg = m_conn.connection_settings();

        cfg.user = config.config_sync_user;
        cfg.password = mxs::decrypt_password(config.config_sync_password);
        cfg.timeout = config.config_sync_timeout.count();
        cfg.ssl = server->ssl_config();

        if (!m_conn.open(server->address(), server->port()))
        {
            throw error("Failed to connect to '", server->name(), "' for configuration update: ",
                        m_conn.error());
        }

        m_server = server;
        m_reconnect = false;
    }

    mxb_assert(m_server);
}

void ConfigManager::verify_sync()
{
    connect();

    if (!m_conn.cmd("START TRANSACTION"))
    {
        throw error("Failed to start transaction: ", m_conn.error());
    }

    auto sql = sql_select_for_update(m_cluster);
    auto res = m_conn.query(sql);

    if (m_conn.errornum() == ER_NO_SUCH_TABLE)
    {
        if (!m_conn.cmd(sql_create_table(CLUSTER_MAX_LEN)))
        {
            throw error("Failed to create table for configuration sync: ", m_conn.error());
        }

        if (!m_conn.cmd("START TRANSACTION"))
        {
            throw error("Failed to start transaction: ", m_conn.error());
        }

        res = m_conn.query(sql);
    }

    if (m_conn.errornum() || !res)
    {
        throw error("Failed to check config version: ", m_conn.error());
    }

    m_row_exists = res->next_row();

    if (m_row_exists)
    {
        int64_t version = res->get_int(0);

        if (version != m_version)
        {
            queue_sync();
            throw error("Configuration conflict detected: version stored in the cluster",
                        " (", version, ") is not the same as the local version (", m_version, "),",
                        " MaxScale is out of sync.");
        }
    }
}

void ConfigManager::update_config(const std::string& payload)
{
    auto sql = m_row_exists ? sql_update : sql_insert;

    if (!m_conn.cmd(sql(m_cluster, m_version, payload)))
    {
        throw error("Failed to update: ", m_conn.error());
    }

    if (!m_conn.cmd("COMMIT"))
    {
        throw error("Failed to commit: ", m_conn.error());
    }
}

void ConfigManager::try_update_status(const std::string& msg)
{
    // Store the latest status for later use.
    m_status_msg = msg;

    // It doesn't really matter if this command fails as it is attempted again during the sync. We aren't
    // expecting it to fail so any errors might still be of interest.
    if (!m_conn.cmd(sql_update_status(m_cluster, m_version, msg)))
    {
        MXS_WARNING("Failed to update node state to '%s' for hostname '%s': %s",
                    msg.c_str(), hostname().c_str(), m_conn.error());
    }
}

mxb::Json ConfigManager::fetch_config()
{
    connect();

    mxb::Json config(mxb::Json::Type::UNDEFINED);
    auto res = m_conn.query(sql_select_version(m_cluster));

    if (!res)
    {
        if (m_conn.errornum() == ER_NO_SUCH_TABLE)
        {
            // The table hasn't been created which means no updates have been done.
            return config;
        }

        throw error("No result for version query: ", m_conn.error());
    }

    if (res->next_row())
    {
        int64_t version = res->get_int(0);

        // Store the status information regardless of its version.
        m_nodes.load_string(res->get_string(1));

        if (version <= m_version)
        {
            if (version < m_version && m_log_stale_cluster)
            {
                // Reverting the configuration is possible but it introduces a problem:
                // If the configuration on server-A causes server-B to be chosen and the configuration on
                // server-B causes server-A to be chosen, the configuration would oscillate between the two if
                // the version values were different. Ignoring older configurations guaratees that we
                // stabilize to some known configuration which is easier to deal with (for both MaxScale and
                // the users) than trying to figure out which of the configurations is the real one.
                mxb_assert(m_server);
                MXS_WARNING("The local configuration version (%ld) is ahead of the cluster "
                            "configuration (%ld) found on server '%s', ignoring to cluster "
                            "configuration.", m_version, version, m_server->name());
                m_log_stale_cluster = false;
            }
            else if (!m_nodes.contains(hostname()))
            {
                // The status update must have failed, try it again.
                try_update_status(m_status_msg);
            }

            return config;
        }
    }

    m_log_stale_cluster = true;

    res = m_conn.query(sql_select_config(m_cluster, m_version));

    if (!res)
    {
        throw error("No result for config query: ", m_conn.error());
    }

    if (res->next_row())
    {
        // We need to check whether the loading succeeds as it's possible that we're using a MariaDB version
        // which does not have the check constraint on the JSON datatype. It's also possible that the table is
        // modified with a different datatype. Neither of these happen in normal use but we should still check
        // it.
        if (!config.load_string(res->get_string(0)))
        {
            throw error("The configuration in the database was not valid JSON: ", config.error_msg());
        }

        int64_t config_version = config.get_int(CN_VERSION);
        int64_t db_version = res->get_int(1);

        // Store the origin even if we fail to apply the configuration
        m_origin = res->get_string(2);

        if (config_version != db_version)
        {
            MXS_WARNING("Version mismatch between JSON (%ld) and version field in database (%ld),"
                        " using version from database.", config_version, db_version);
            config.set_int(CN_VERSION, db_version);
        }
    }

    return config;
}
}
