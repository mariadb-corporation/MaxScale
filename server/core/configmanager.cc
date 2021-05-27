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

namespace
{
using namespace std::string_literals;

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
        json_object_set_new(obj, "config", arr);
        json_object_set_new(obj, "version", json_integer(1));

        mxb::Json rval(mxb::Json::Type::NONE);
        rval.reset(obj);
        return rval;
    }

    void process_config(mxb::Json&& new_json)
    {
    }

private:

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
