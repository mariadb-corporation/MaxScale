/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

#include <maxscale/cn_strings.hh>
#include <maxscale/paths.hh>
#include <maxscale/json.hh>
#include <maxbase/json.hh>

#include "internal/config.hh"
#include "internal/configmanager.hh"
#include "internal/config_runtime.hh"
#include "internal/servermanager.hh"
#include "internal/monitormanager.hh"

#include <set>
#include <fstream>

namespace
{
const char CN_VERSION[] = "version";
const char CN_CONFIG[] = "config";
const char CN_CLUSTER_NAME[] = "cluster_name";

struct ThisUnit
{
    mxs::ConfigManager* manager {nullptr};
};

ThisUnit this_unit;
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
{
    mxb_assert(!this_unit.manager);
    this_unit.manager = this;
}

ConfigManager::~ConfigManager()
{
    mxb_assert(this_unit.manager == this);
}

void ConfigManager::sync()
{
    // TODO: Execute the following:
    //    SELECT config FROM mysql.maxscale_config WHERE CLUSTER = '%s' AND version > %d
}

bool ConfigManager::load_cached_config()
{
    bool have_config = false;
    std::string filename = dynamic_config_filename();
    const std::string& cluster = cluster_name();

    // Check only if the file exists. If it does, try to load it.
    if (!cluster.empty() && access(filename.c_str(), F_OK) == 0)
    {
        mxb::Json new_json(mxb::Json::Type::NONE);

        if (new_json.load(filename))
        {
            std::string cluster_name = new_json.get_string(CN_CLUSTER_NAME);

            if (cluster_name == cluster)
            {
                MXS_NOTICE("Using cached configuration for cluster '%s': %s",
                           cluster_name.c_str(), filename.c_str());

                m_current_config = std::move(new_json);
                have_config = true;
            }
            else
            {
                MXS_WARNING("Found cached configuration for cluster '%s' when configured "
                            "to use cluster '%s', ignoring the cached configuration: %s",
                            cluster_name.c_str(), cluster.c_str(), filename.c_str());
            }
        }
    }

    return have_config;
}

bool ConfigManager::process_cached_config()
{
    bool ok = true;

    try
    {
        mxb::Json config = std::move(m_current_config);

        // Storing an empty object in the current JSON will cause all objects to be treated as new.
        m_current_config = mxb::Json(mxb::Json::Type::OBJECT);

        process_config(std::move(config));
    }
    catch (const ConfigManager::Exception& e)
    {
        MXS_ERROR("%s", e.what());
        ok = false;
    }

    return ok;
}

bool ConfigManager::start()
{
    if (cluster_name().empty())
    {
        return true;
    }

    // TODO: Execute the following:
    //    START TRANSACTION
    //    SELECT version FROM mysql.maxscale_config WHERE CLUSTER = '%s' FOR UPDATE
    return true;
}

void ConfigManager::rollback()
{
    if (cluster_name().empty())
    {
        return;
    }

    // TODO: Execute the following:
    //    ROLLBACK
}

bool ConfigManager::commit()
{
    if (cluster_name().empty())
    {
        return true;
    }

    bool ok = false;

    // Increment the current version and create the JSON
    ++m_version;
    mxb::Json config = create_config();
    std::string payload = config.to_string(mxb::Json::Format::COMPACT);

    // TODO: Execute the following:
    //    UPDATE mysql.maxscale_config SET data = '%s', version = %d WHERE CLUSTER = '%s'
    //    COMMIT

    // Store the cached value locally on disk.
    std::string filename = dynamic_config_filename();
    std::string tmpname = filename + ".tmp";
    std::ofstream file(tmpname);

    if (file.write(payload.c_str(), payload.size()) && file.flush()
        && rename(tmpname.c_str(), filename.c_str()) == 0)
    {
        // Config successfully stored, stash it for later use
        m_current_config = std::move(config);
        ok = true;
    }
    else
    {
        // TODO: Make the next sync take place immediately after this call.
        --m_version;
    }

    return ok;
}

mxb::Json ConfigManager::create_config()
{
    mxb::Json arr(mxb::Json::Type::ARRAY);

    append_config(arr.get_json(), ServerManager::server_list_to_json(""));
    append_config(arr.get_json(), MonitorManager::monitor_list_to_json(""));
    append_config(arr.get_json(), service_list_to_json(""));
    append_config(arr.get_json(), FilterDef::filter_list_to_json(""));
    append_config(arr.get_json(), Listener::to_json_collection(""));
    append_config(arr.get_json(), config_maxscale_to_json(""));

    mxb::Json rval(mxb::Json::Type::OBJECT);

    rval.set_object(CN_CONFIG, arr);
    rval.set_int(CN_VERSION, m_version);

    const std::string& cluster = cluster_name();
    mxb_assert(!cluster.empty());
    rval.set_string(CN_CLUSTER_NAME, cluster);

    return rval;
}

void ConfigManager::process_config(mxb::Json&& new_json)
{
    int64_t next_version = new_json.get_int(CN_VERSION);

    if (next_version <= m_version)
    {
        throw error("Not processing old configuration: got version ",
                    m_version, ", found version ", next_version, " in the configuration.");
    }

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
            update_object(obj.get_string(CN_ID), type, obj);
        }
    }

    m_version = next_version;
    m_current_config = std::move(new_json);
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

void ConfigManager::remove_old_object(const std::string& name, const std::string& type)
{
    switch (to_type(type))
    {
    case Type::SERVERS:
        if (!runtime_destroy_server(ServerManager::find_by_unique_name(name), true))
        {
            throw error("Failed to destroy server '", name, "'");
        }
        break;

    case Type::MONITORS:
        if (!runtime_destroy_monitor(MonitorManager::find_monitor(name.c_str()), true))
        {
            throw error("Failed to destroy monitor '", name, "'");
        }
        break;

    case Type::SERVICES:
        if (!runtime_destroy_service(Service::find(name), true))
        {
            throw error("Failed to destroy service '", name, "'");
        }
        break;

    case Type::LISTENERS:
        if (!runtime_destroy_listener(listener_find(name)))
        {
            throw error("Failed to destroy listener '", name, "'");
        }
        break;

    case Type::FILTERS:
        if (!runtime_destroy_filter(filter_find(name), true))
        {
            throw error("Failed to destroy filter '", name, "'");
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
        // Let the other objects express the two-way relationships
        obj.erase(CN_RELATIONSHIPS);

        if (!runtime_create_server_from_json(m_tmp.get_json()))
        {
            throw error("Failed to create server '", name, "'");
        }
        break;

    case Type::MONITORS:
        // Erase any service relationships, they can be expressed by services themselves
        obj.get_object(CN_RELATIONSHIPS).erase(CN_SERVICES);

        if (!runtime_create_monitor_from_json(m_tmp.get_json()))
        {
            throw error("Failed to create monitor '", name, "'");
        }
        break;

    case Type::SERVICES:
        {
            // Create services without relationships, they will be handled by the update step
            auto rel = obj.get_object(CN_RELATIONSHIPS);
            obj.erase(CN_RELATIONSHIPS);

            if (!runtime_create_service_from_json(m_tmp.get_json()))
            {
                throw error("Failed to create service '", name, "'");
            }

            obj.set_object(CN_RELATIONSHIPS, rel);
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
    // The maxscales type should not be "new" as it is always created even if there are no other objects.
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
        if (!runtime_alter_server_from_json(ServerManager::find_by_unique_name(name), js))
        {
            throw error("Failed to update server '", name, "'");
        }
        break;

    case Type::MONITORS:
        if (!runtime_alter_monitor_from_json(MonitorManager::find_monitor(name.c_str()), js))
        {
            throw error("Failed to update monitor '", name, "'");
        }
        break;

    case Type::SERVICES:
        if (!runtime_alter_service_from_json(Service::find(name), js))
        {
            throw error("Failed to update service '", name, "'");
        }
        break;

    case Type::LISTENERS:
        if (!runtime_alter_listener_from_json(listener_find(name), js))
        {
            throw error("Failed to update listener '", name, "'");
        }
        break;

    case Type::FILTERS:
        if (!runtime_alter_filter_from_json(filter_find(name), js))
        {
            throw error("Failed to update filter '", name, "'");
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

std::string ConfigManager::dynamic_config_filename() const
{
    return std::string(mxs::datadir()) + "/maxscale-config.json";
}

const std::string& ConfigManager::cluster_name() const
{
    return mxs::Config::get().config_sync_cluster;
}
}
