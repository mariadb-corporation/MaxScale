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

#include "internal/configmanager.hh"
#include "internal/config_runtime.hh"
#include "internal/servermanager.hh"
#include "internal/monitormanager.hh"

namespace
{
using namespace std::string_literals;

static const std::unordered_set<std::string> keys_to_keep {
    CN_PARAMETERS, CN_RELATIONSHIPS, CN_MODULE, CN_ROUTER
};

json_t* create_config()
{
    json_t* res = json_object();

    auto remove_extra = [](json_t* data) {
            json_t* attr = json_object_get(data, CN_ATTRIBUTES);
            void* ptr;
            const char* key;
            json_t* ignored;

            json_object_foreach_safe(attr, ptr, key, ignored)
            {
                if (keys_to_keep.count(key) == 0)
                {
                    json_object_del(attr, key);
                }
            }

            // Remove the links, we don't need them
            json_object_del(data, CN_LINKS);
        };

    auto to_data = [&](json_t* json) {
            json_t* data = json_incref(json_object_get(json, CN_DATA));

            if (json_is_array(data))
            {
                json_t* value;
                size_t i;

                json_array_foreach(data, i, value)
                {
                    remove_extra(value);
                }
            }
            else
            {
                remove_extra(data);
            }

            json_decref(json);
            return data;
        };

    json_object_set_new(res, CN_MAXSCALE, to_data(config_maxscale_to_json("")));
    json_object_set_new(res, CN_SERVERS, to_data(ServerManager::server_list_to_json("")));
    json_object_set_new(res, CN_MONITORS, to_data(MonitorManager::monitor_list_to_json("")));
    json_object_set_new(res, CN_SERVICES, to_data(service_list_to_json("")));
    json_object_set_new(res, CN_LISTENERS, to_data(Listener::to_json_collection("")));
    json_object_set_new(res, CN_FILTERS, to_data(FilterDef::filter_list_to_json("")));

    return res;
}
}

namespace maxscale
{

bool have_dynamic_config()
{
    return false;
}

bool save_dynamic_config()
{
    json_t* res = create_config();
    std::string filename = mxs::datadir() + "/maxscale-config.json"s;
    bool ok = json_dump_file(res, filename.c_str(), JSON_COMPACT) == 0;
    json_decref(res);
    return ok;
}

bool load_dynamic_config()
{
    return false;
}
}
