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

#include "internal/configmanager.hh"
#include "internal/config_runtime.hh"
#include "internal/servermanager.hh"
#include "internal/monitormanager.hh"

#include <set>

namespace
{
using namespace std::string_literals;

const char CN_VERSION[] = "version";
const char CN_CONFIG[] = "config";

class ConfigManager
{
public:

    mxb::Json create_config()
    {
        json_t* arr = json_array();

        append_config(arr, ServerManager::server_list_to_json(""));
        append_config(arr, MonitorManager::monitor_list_to_json(""));
        append_config(arr, service_list_to_json(""));
        append_config(arr, FilterDef::filter_list_to_json(""));
        append_config(arr, Listener::to_json_collection(""));
        append_config(arr, config_maxscale_to_json(""));

        json_t* obj = json_object();
        json_object_set_new(obj, CN_CONFIG, arr);
        json_object_set_new(obj, CN_VERSION, json_integer(m_version));

        mxb::Json rval(mxb::Json::Type::NONE);
        rval.reset(obj);
        return rval;
    }

    void process_config(mxb::Json&& new_json)
    {
        // TODO: Keep the old JSON so that we don't have to create it again for every change
        mxb::Json old_json = create_config();
        int64_t next_version = new_json.get_int(CN_VERSION);

        // TODO: This must be an error
        mxb_assert(next_version > m_version);

        std::set<std::string> old_names;
        std::set<std::string> new_names;

        auto new_objects = new_json.get_array_elems(CN_CONFIG);
        auto old_objects = old_json.get_array_elems(CN_CONFIG);

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
            update_object(obj.get_string(CN_ID), obj.get_string(CN_TYPE), obj);
        }

        m_version = next_version;
    }

private:

    void remove_old_object(const std::string& name, const std::string& type)
    {
        MXS_INFO("Would remove: %s %s", name.c_str(), type.c_str());
    }

    void create_new_object(const std::string& name, const std::string& type, mxb::Json& obj)
    {
        MXS_INFO("Would create: %s %s %s", name.c_str(), type.c_str(), obj.to_string().c_str());
    }

    void update_object(const std::string& name, const std::string& type, const mxb::Json& json)
    {
        MXS_INFO("Would update: %s %s %s", name.c_str(), type.c_str(), json.to_string().c_str());
    }

    auto remove_extra_data(json_t* data)
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

    void append_config(json_t* arr, json_t* json)
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

    int64_t m_version {0};
};

ConfigManager manager;

std::string dynamic_config_filename()
{
    return std::string(mxs::datadir()) + "/maxscale-config.json";
}
}

namespace maxscale
{

bool have_dynamic_config()
{
    // TODO: Enable this once the loading is implemented
    return false;
}

bool save_dynamic_config()
{
    return manager.create_config().save(dynamic_config_filename(), mxb::Json::COMPACT);
}

bool load_dynamic_config()
{
    bool ok = true;
    std::string filename = dynamic_config_filename();
    mxb::Json new_json(mxb::Json::Type::NONE);

    if (!new_json.load(filename))
    {
        MXS_ERROR("Failed to load dynamic config file from '%s': %s",
                  filename.c_str(), new_json.error_msg().c_str());
        ok = false;
    }
    else
    {
        manager.process_config(std::move(new_json));
    }

    return ok;
}
}
