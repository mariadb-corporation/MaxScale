/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/config_runtime.hh"

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include <strings.h>
#include <fcntl.h>
#include <unistd.h>

#include <maxbase/filesystem.hh>
#include <maxbase/format.hh>
#include <maxbase/jansson.hh>
#include <maxscale/json_api.hh>
#include <maxscale/listener.hh>
#include <maxscale/paths.hh>
#include <maxscale/router.hh>
#include <maxscale/users.hh>

#include "internal/adminusers.hh"
#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/servermanager.hh"

typedef std::set<std::string>    StringSet;
typedef std::vector<std::string> StringVector;

using std::tie;
using maxscale::Monitor;
using namespace std::literals::string_literals;

typedef std::function<bool (const std::string&, const std::string&)> JsonValidator;
typedef std::pair<const char*, JsonValidator>                        Relationship;

namespace
{

struct ThisUnit
{
    std::mutex               lock;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

static ThisUnit this_unit;

const char CN_DEFAULT[] = "default";

bool object_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVERS && ServerManager::find_by_unique_name(value);
}

bool service_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVICES && Service::find(value);
}

bool monitor_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_MONITORS && MonitorManager::find_monitor(value.c_str());
}

bool filter_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_FILTERS && filter_find(value.c_str());
}

//
// Constants for relationship validation
//
const Relationship to_server_rel
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVERS,
    object_relation_is_valid
};

const Relationship to_service_rel
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVICES,
    service_relation_is_valid
};

const Relationship to_monitor_rel
{
    MXS_JSON_PTR_RELATIONSHIPS_MONITORS,
    monitor_relation_is_valid
};

const Relationship to_filter_rel
{
    MXS_JSON_PTR_RELATIONSHIPS_FILTERS,
    filter_relation_is_valid
};

bool should_save()
{
    const auto& cnf = mxs::Config::get();
    return cnf.config_sync_cluster.empty() && cnf.persist_runtime_changes;
}

bool save_config(Server* server)
{
    bool ok = true;

    if (should_save())
    {
        if (server->in_static_config_state())
        {
            ok = runtime_discard_config(server->name());
        }
        else
        {
            std::ostringstream ss;
            server->persist(ss);
            ok = runtime_save_config(server->name(), ss.str());
        }
    }

    return ok;
}

bool save_config(Service* service)
{
    bool ok = true;

    if (should_save())
    {
        if (service->in_static_config_state())
        {
            ok = runtime_discard_config(service->name());
        }
        else
        {
            std::ostringstream ss;
            service->persist(ss);
            ok = runtime_save_config(service->name(), ss.str());
        }
    }

    return ok;
}

bool save_config(const mxs::SListener& listener)
{
    bool ok = true;

    if (should_save())
    {
        if (listener->in_static_config_state())
        {
            ok = runtime_discard_config(listener->name());
        }
        else
        {
            std::ostringstream ss;
            listener->persist(ss);
            ok = runtime_save_config(listener->name(), ss.str());
        }
    }

    return ok;
}

bool save_config(mxs::Monitor* monitor)
{
    bool ok = true;

    if (should_save())
    {
        if (monitor->in_static_config_state())
        {
            ok = runtime_discard_config(monitor->name());
        }
        else
        {
            std::ostringstream ss;
            MonitorManager::monitor_persist(monitor, ss);
            ok = runtime_save_config(monitor->name(), ss.str());
        }
    }

    return ok;
}

bool save_config(const SFilterDef& filter)
{
    bool ok = true;

    if (should_save())
    {
        if (filter->in_static_config_state())
        {
            ok = runtime_discard_config(filter->name());
        }
        else
        {
            std::ostringstream ss;
            filter->persist(ss);
            ok = runtime_save_config(filter->name(), ss.str());
        }
    }

    return ok;
}

bool save_config(const mxs::Config& config)
{
    bool ok = true;

    if (should_save())
    {
        std::ostringstream ss;
        config.persist_maxscale(ss);
        ok = runtime_save_config("maxscale", ss.str());
    }

    return ok;
}

std::string get_cycle_name(mxs::Target* item, mxs::Target* target)
{
    std::string rval;

    for (auto c : target->get_children())
    {
        if (c == item)
        {
            rval = item->name();
        }
        else
        {
            rval = get_cycle_name(item, c);
        }

        if (!rval.empty())
        {
            rval += " <- ";
            rval += target->name();
            break;
        }
    }

    return rval;
}

bool link_service_to_monitor(Service* service, mxs::Monitor* monitor)
{
    bool ok = service->change_cluster(monitor);

    if (!ok)
    {
        std::string err = service->cluster() ?
            "Service already uses cluster '"s + service->cluster()->name() + "'" :
            "Service uses targets";
        MXB_ERROR("Service '%s' cannot use cluster '%s': %s", service->name(), monitor->name(), err.c_str());
    }

    return ok;
}

bool unlink_service_from_monitor(Service* service, mxs::Monitor* monitor)
{
    bool ok = service->remove_cluster(monitor);

    if (!ok)
    {
        MXB_ERROR("Service '%s' does not use monitor '%s'", service->name(), monitor->name());
    }

    return ok;
}

bool check_link_target(Service* service, mxs::Target* target)
{
    bool rval = false;

    if (service == target)
    {
        MXB_ERROR("Cannot link '%s' to itself", service->name());
    }
    else if (service->has_target(target))
    {
        MXB_ERROR("Service '%s' already uses target '%s'", service->name(), target->name());
    }
    else
    {
        auto cycle = get_cycle_name(service, target);

        if (!cycle.empty())
        {
            MXB_ERROR("Linking '%s' to '%s' would result in a circular configuration: %s",
                      target->name(), service->name(), cycle.c_str());
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

bool link_service(Service* service, const StringSet& children)
{
    bool rval = true;

    for (const auto& child : children)
    {
        if (auto monitor = MonitorManager::find_monitor(child.c_str()))
        {
            rval = link_service_to_monitor(service, monitor);
        }
        else if (auto cluster = service->cluster())
        {
            MXB_ERROR("The servers of the service '%s' are defined by the monitor '%s'. "
                      "Servers cannot explicitly be added to the service.",
                      service->name(), cluster->name());
            rval = false;
        }
        else if (auto server = ServerManager::find_by_unique_name(child))
        {
            if (check_link_target(service, server))
            {
                service->add_target(server);
            }
            else
            {
                rval = false;
            }
        }
        else if (auto other = Service::find(child))
        {
            if (check_link_target(service, other) && service->protocol_is_compatible(other))
            {
                service->add_target(other);
            }
            else
            {
                rval = false;
            }
        }
        else
        {
            MXB_ERROR("Could not find target with name '%s'", child.c_str());
            rval = false;
        }

        if (!rval)
        {
            break;
        }
    }

    if (rval)
    {
        rval = save_config(service);
    }

    return rval;
}

bool link_monitor(mxs::Monitor* monitor, const StringSet& children)
{
    bool rval = true;

    for (const auto& child : children)
    {
        if (auto srv = ServerManager::find_by_unique_name(child))
        {
            std::string error_msg;
            if (!MonitorManager::add_server_to_monitor(monitor, srv, &error_msg))
            {
                MXB_ERROR("%s", error_msg.c_str());
                rval = false;
            }
        }
        else if (auto service = Service::find(child))
        {
            rval = link_service_to_monitor(service, monitor);
        }
        else
        {
            MXB_ERROR("No server or service named '%s' found", child.c_str());
            rval = false;
        }

        if (!rval)
        {
            break;
        }
    }

    if (rval)
    {
        rval = save_config(monitor);
    }

    return rval;
}

bool runtime_link_target(const StringSet& children, const StringSet& parents)
{
    if (children.empty())
    {
        return true;
    }

    bool rval = true;
    auto names = mxb::join(children, ", ", "'");

    for (const auto& parent : parents)
    {
        if (auto service = Service::find(parent))
        {
            rval = link_service(service, children);
        }
        else if (auto monitor = MonitorManager::find_monitor(parent.c_str()))
        {
            rval = link_monitor(monitor, children);
        }
        else
        {
            MXB_ERROR("No monitor or service named '%s' found", parent.c_str());
            rval = false;
        }

        if (rval)
        {
            MXB_NOTICE("Added %s to '%s'", names.c_str(), parent.c_str());
        }
        else
        {
            break;
        }
    }

    return rval;
}

bool unlink_service(Service* service, const StringSet& children)
{
    bool rval = true;

    for (const auto& child : children)
    {
        if (auto monitor = MonitorManager::find_monitor(child.c_str()))
        {
            rval = unlink_service_from_monitor(service, monitor);
        }
        else if (auto cluster = service->cluster())
        {
            MXB_ERROR("The servers of the service '%s' are defined by the monitor '%s'. "
                      "Servers cannot explicitly be removed from the service.",
                      service->name(), cluster->name());
            rval = false;
        }
        else if (auto server = SERVER::find_by_unique_name(child))
        {
            // TODO: Should we check that the service actually uses the server?
            service->remove_target(server);
        }
        else if (auto other = Service::find(child))
        {
            service->remove_target(other);
        }
        else
        {
            MXB_ERROR("Target '%s' not found", child.c_str());
            rval = false;
        }

        if (!rval)
        {
            break;
        }
    }

    if (rval)
    {
        rval = save_config(service);
    }

    return rval;
}

bool unlink_monitor(mxs::Monitor* monitor, const StringSet& children)
{
    bool rval = true;

    for (const auto& child : children)
    {
        if (auto srv = ServerManager::find_by_unique_name(child))
        {
            std::string error_msg;
            if (!MonitorManager::remove_server_from_monitor(monitor, srv, &error_msg))
            {
                MXB_ERROR("%s", error_msg.c_str());
                rval = false;
            }
        }
        else if (auto service = Service::find(child))
        {
            rval = unlink_service_from_monitor(service, monitor);
        }
        else
        {
            MXB_ERROR("No server named '%s' found", child.c_str());
            rval = false;
        }

        if (!rval)
        {
            break;
        }
    }

    if (rval)
    {
        rval = save_config(monitor);
    }

    return rval;
}

bool runtime_unlink_target(const StringSet& children, const StringSet& parents)
{
    if (children.empty())
    {
        return true;
    }

    bool rval = true;
    auto names = mxb::join(children, ", ", "'");

    for (const auto& parent : parents)
    {
        if (auto service = Service::find(parent))
        {
            rval = unlink_service(service, children);
        }
        else if (auto monitor = MonitorManager::find_monitor(parent.c_str()))
        {
            rval = unlink_monitor(monitor, children);
        }
        else
        {
            MXB_ERROR("No monitor or service named '%s' found", parent.c_str());
            rval = false;
        }

        if (rval)
        {
            MXB_NOTICE("Removed %s from '%s'", names.c_str(), parent.c_str());
        }
        else
        {
            break;
        }
    }

    return rval;
}

/**
 * Check if the value is a non-empty JSON string
 *
 * @param json Value to check
 * @param path JSON Pointer to the value
 *
 * @return True if the value pointed to by `path` is a non-empty JSON string
 */
bool is_valid_string(json_t* json, const char* path)
{
    bool ok = false;
    json_t* val = mxb::json_ptr(json, path);

    if (!val)
    {
        MXB_ERROR("Request body does not define the '%s' field.", path);
    }
    else if (!json_is_string(val))
    {
        MXB_ERROR("The '%s' field is not a string.", path);
    }
    else if (json_string_length(val) == 0)
    {
        MXB_ERROR("Value '%s' is empty.", path);
    }
    else
    {
        ok = true;
    }

    return ok;
}

bool extract_ordered_relations(json_t* json,
                               StringVector& relations,
                               Relationship rel)
{
    bool rval = true;
    json_t* arr = mxb::json_ptr(json, rel.first);

    if (arr && json_is_array(arr))
    {
        size_t size = json_array_size(arr);

        for (size_t j = 0; j < size; j++)
        {
            json_t* obj = json_array_get(arr, j);
            json_t* id = json_object_get(obj, CN_ID);
            json_t* type = mxb::json_ptr(obj, CN_TYPE);

            if (id && json_is_string(id)
                && type && json_is_string(type))
            {
                std::string id_value = json_string_value(id);
                std::string type_value = json_string_value(type);

                if (rel.second(type_value, id_value))
                {
                    relations.push_back(id_value);
                }
                else
                {
                    MXB_ERROR("'%s' is not a valid object of type '%s'",
                              id_value.c_str(), type_value.c_str());
                    rval = false;
                }
            }
            else
            {
                MXB_ERROR("Malformed relationship object");
                rval = false;
            }
        }
    }

    return rval;
}

bool extract_relations(json_t* json,
                       StringSet& relations,
                       Relationship rel)
{
    StringVector values;
    bool rval = extract_ordered_relations(json, values, rel);
    relations.insert(values.begin(), values.end());
    return rval;
}

bool is_null_relation(json_t* json, const char* relation)
{
    std::string str(relation);
    size_t pos = str.rfind("/data");

    mxb_assert(pos != std::string::npos);
    str = str.substr(0, pos);

    json_t* data = mxb::json_ptr(json, relation);
    json_t* base = mxb::json_ptr(json, str.c_str());

    return (data && json_is_null(data)) || (base && json_is_null(base));
}

const char* json_type_to_string(const json_t* json)
{
    mxb_assert(json);

    if (json_is_object(json))
    {
        return "an object";
    }
    else if (json_is_array(json))
    {
        return "an array";
    }
    else if (json_is_string(json))
    {
        return "a string";
    }
    else if (json_is_integer(json))
    {
        return "an integer";
    }
    else if (json_is_real(json))
    {
        return "a real number";
    }
    else if (json_is_boolean(json))
    {
        return "a boolean";
    }
    else if (json_is_null(json))
    {
        return "a null value";
    }
    else
    {
        mxb_assert(!true);
        return "an unknown type";
    }
}

const char* get_string_or_null(json_t* json, const char* path)
{
    const char* rval = NULL;
    json_t* value = mxb::json_ptr(json, path);

    if (value && json_is_string(value))
    {
        rval = json_string_value(value);
    }

    return rval;
}

bool runtime_is_string_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxb::json_ptr(json, path);

    if (value && !json_is_string(value) && !json_is_null(value))
    {
        MXB_ERROR("Parameter '%s' is not a string but %s", path, json_type_to_string(value));
        rval = false;
    }

    return rval;
}

bool runtime_is_bool_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxb::json_ptr(json, path);

    if (value && !json_is_boolean(value) && !json_is_null(value))
    {
        MXB_ERROR("Parameter '%s' is not a boolean but %s", path, json_type_to_string(value));
        rval = false;
    }

    return rval;
}

bool runtime_is_size_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxb::json_ptr(json, path);

    if (value)
    {
        if (!json_is_integer(value) && !json_is_string(value) && !json_is_null(value))
        {
            MXB_ERROR("Parameter '%s' is not an integer or a string but %s",
                      path, json_type_to_string(value));
            rval = false;
        }
        else if ((json_is_integer(value) && json_integer_value(value) < 0)
                 || (json_is_string(value) && !get_suffixed_size(json_string_value(value), nullptr)))
        {
            MXB_ERROR("Parameter '%s' is not a valid size", path);
            rval = false;
        }
    }

    return rval;
}

bool runtime_is_count_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxb::json_ptr(json, path);

    if (value)
    {
        if (!json_is_integer(value) && !json_is_null(value))
        {
            MXB_ERROR("Parameter '%s' is not an integer but %s", path, json_type_to_string(value));
            rval = false;
        }
        else if (json_is_integer(value) && json_integer_value(value) < 0)
        {
            MXB_ERROR("Parameter '%s' is a negative integer", path);
            rval = false;
        }
    }

    return rval;
}

/** Check that the body at least defies a data member */
bool is_valid_resource_body(json_t* json)
{
    bool rval = true;

    if (mxb::json_ptr(json, MXS_JSON_PTR_DATA) == NULL)
    {
        MXB_ERROR("No '%s' field defined", MXS_JSON_PTR_DATA);
        rval = false;
    }
    else
    {
        // Check that the relationship JSON is well-formed
        const std::vector<const char*> relations =
        {
            MXS_JSON_PTR_RELATIONSHIPS "/servers",
            MXS_JSON_PTR_RELATIONSHIPS "/services",
            MXS_JSON_PTR_RELATIONSHIPS "/monitors",
            MXS_JSON_PTR_RELATIONSHIPS "/filters",
        };

        for (auto it = relations.begin(); it != relations.end(); it++)
        {
            json_t* j = mxb::json_ptr(json, *it);

            if (j && !json_is_object(j))
            {
                MXB_ERROR("Relationship '%s' is not an object", *it);
                rval = false;
            }
        }
    }

    return rval;
}

bool server_contains_required_fields(json_t* json)
{
    bool rval = false;
    auto err = mxs_is_valid_json_resource(json);

    if (!err.empty())
    {
        MXB_ERROR("%s", err.c_str());
    }
    else if (!mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS))
    {
        MXB_ERROR("Field '%s' is not defined", MXS_JSON_PTR_PARAMETERS);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool unlink_target_from_objects(const std::string& target, StringSet& relations)
{
    return runtime_unlink_target({target}, relations);
}

bool link_target_to_objects(const std::string& target, StringSet& relations)
{
    bool rval = true;

    if (!runtime_link_target({target}, relations))
    {
        rval = false;
    }

    return rval;
}

bool server_to_object_relations(Server* server, json_t* old_json, json_t* new_json)
{
    if (mxb::json_ptr(new_json, MXS_JSON_PTR_RELATIONSHIPS_SERVICES) == NULL
        && mxb::json_ptr(new_json, MXS_JSON_PTR_RELATIONSHIPS_MONITORS) == NULL)
    {
        /** No change to relationships */
        return true;
    }

    bool rval = true;
    StringSet old_relations;
    StringSet new_relations;

    for (const auto& a : {to_service_rel, to_monitor_rel})
    {
        // Extract only changed or deleted relationships
        if (is_null_relation(new_json, a.first) || mxb::json_ptr(new_json, a.first))
        {
            if (!extract_relations(new_json, new_relations, a)
                || !extract_relations(old_json, old_relations, a))
            {
                rval = false;
                break;
            }
        }
    }

    if (rval)
    {
        StringSet removed_relations;
        StringSet added_relations;

        std::set_difference(old_relations.begin(),
                            old_relations.end(),
                            new_relations.begin(),
                            new_relations.end(),
                            std::inserter(removed_relations, removed_relations.begin()));

        std::set_difference(new_relations.begin(),
                            new_relations.end(),
                            old_relations.begin(),
                            old_relations.end(),
                            std::inserter(added_relations, added_relations.begin()));

        if (!unlink_target_from_objects(server->name(), removed_relations)
            || !link_target_to_objects(server->name(), added_relations))
        {
            rval = false;
        }
    }

    return rval;
}

bool is_valid_relationship_body(json_t* json)
{
    bool rval = true;

    json_t* obj = mxb::json_ptr(json, MXS_JSON_PTR_DATA);

    if (!obj)
    {
        MXB_ERROR("Field '%s' is not defined", MXS_JSON_PTR_DATA);
        rval = false;
    }
    else if (!json_is_array(obj) && !json_is_null(obj))
    {
        MXB_ERROR("Field '%s' is not an array", MXS_JSON_PTR_DATA);
        rval = false;
    }

    return rval;
}

/**
 * @brief Do coarse validation of the object JSON
 *
 * @param json          JSON to validate
 * @param paths         List of paths that must be string values
 * @param relationships List of JSON paths and validation functions to check
 *
 * @return True of the JSON is valid
 */
bool validate_object_json(json_t* json)
{
    auto err = mxs_is_valid_json_resource(json);

    if (!err.empty())
    {
        MXB_ERROR("%s", err.c_str());
    }

    return err.empty();
}

bool inject_server_relationship_as_parameter(json_t* params, json_t* json, Monitor* current)
{
    mxb_assert(params);
    StringVector relations;
    bool rval = true;

    // We need to check first if there's a value defined. If there isn't, we do nothing and treat the
    // relationships as the same.
    if (mxb::json_ptr(json, to_server_rel.first))
    {
        if (extract_ordered_relations(json, relations, to_server_rel))
        {
            std::set<std::string_view> values;

            for (const auto& rel : relations)
            {
                auto s = ServerManager::find_by_unique_name(rel);
                mxb_assert(s);      // Validated by extract_ordered_relations()

                if (Monitor* other = MonitorManager::server_is_monitored(s))
                {
                    if (current && current != other)
                    {
                        MXB_ERROR("Server '%s' is already monitored by '%s'.", rel.c_str(),
                                  other->name());
                        return false;
                    }
                }

                if (values.find(rel) != values.end())
                {
                    MXB_ERROR("Cannot add server '%s' to the same monitor twice.", rel.c_str());
                    return false;
                }

                values.insert(rel);
            }

            // The empty string parameter makes sure this work even if the relationship is being removed. This
            // currently includes setting the `data` field to null which is documented as not being supported
            // but has been for quite some time.
            json_object_set_new(params, CN_SERVERS, json_string(mxb::join(relations).c_str()));
        }
        else
        {
            rval = false;
        }
    }

    return rval;
}

bool unlink_object_from_targets(const std::string& target, StringSet& relations)
{
    bool rval = true;

    if (!runtime_unlink_target(relations, {target}))
    {
        rval = false;
    }

    return rval;
}

bool link_object_to_targets(const std::string& target, StringSet& relations)
{
    bool rval = true;

    if (!runtime_link_target(relations, {target}))
    {
        rval = false;
    }

    return rval;
}

bool update_object_relations(const std::string& target,
                             Relationship rel,
                             json_t* old_json,
                             json_t* new_json)
{
    if (mxb::json_ptr(new_json, rel.first) == NULL)
    {
        /** No change to relationships */
        return true;
    }

    StringSet old_relations;
    StringSet new_relations;
    bool rval = false;

    if (extract_relations(old_json, old_relations, rel) && extract_relations(new_json, new_relations, rel))
    {
        StringSet removed_relations;
        StringSet added_relations;

        std::set_difference(old_relations.begin(), old_relations.end(),
                            new_relations.begin(), new_relations.end(),
                            std::inserter(removed_relations, removed_relations.begin()));

        std::set_difference(new_relations.begin(), new_relations.end(),
                            old_relations.begin(), old_relations.end(),
                            std::inserter(added_relations, added_relations.begin()));

        if (unlink_object_from_targets(target, removed_relations)
            && link_object_to_targets(target, added_relations))
        {
            rval = true;
        }
    }

    return rval;
}

bool object_to_server_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_server_rel, old_json, new_json);

    if (!rval)
    {
        MXB_ERROR("Could not find all servers that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_service_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_service_rel, old_json, new_json);

    if (!rval)
    {
        MXB_ERROR("Could not find all services that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_filter_relations(Service* service, json_t* old_json, json_t* new_json)
{
    if (mxb::json_ptr(new_json, MXS_JSON_PTR_RELATIONSHIPS_FILTERS) == NULL)
    {
        // No relationships defined, nothing to change
        return true;
    }

    bool rval = false;
    StringVector old_relations;
    StringVector new_relations;

    if (extract_ordered_relations(old_json, old_relations, to_filter_rel)
        && extract_ordered_relations(new_json, new_relations, to_filter_rel))
    {
        if (old_relations == new_relations || service->set_filters(new_relations))
        {
            // Either no change in relationships took place or we successfully
            // updated the filter relationships
            rval = true;
        }
    }
    else
    {

        MXB_ERROR("Could not find all filters that '%s' relates to", service->name());
    }

    return rval;
}

bool service_to_monitor_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_monitor_rel, old_json, new_json);

    if (!rval)
    {
        MXB_ERROR("Could not find the monitor that '%s' relates to", target.c_str());
    }

    return rval;
}

bool monitor_to_service_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_service_rel, old_json, new_json);

    if (!rval)
    {
        MXB_ERROR("Could not find the service that '%s' relates to", target.c_str());
    }

    return rval;
}

bool validate_logs_json(json_t* json)
{
    json_t* param = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);
    bool rval = false;

    if (param && json_is_object(param))
    {
        rval = runtime_is_bool_or_null(param, "highprecision")
            && runtime_is_bool_or_null(param, "maxlog")
            && runtime_is_bool_or_null(param, "syslog")
            && runtime_is_bool_or_null(param, "log_info")
            && runtime_is_bool_or_null(param, "log_warning")
            && runtime_is_bool_or_null(param, "log_notice")
            && runtime_is_bool_or_null(param, "log_debug")
            && runtime_is_count_or_null(param, "throttling/count")
            && runtime_is_count_or_null(param, "throttling/suppress_ms")
            && runtime_is_count_or_null(param, "throttling/window_ms");
    }

    return rval;
}

bool validate_listener_json(json_t* json)
{
    bool rval = false;
    json_t* param;

    if (is_valid_string(json, MXS_JSON_PTR_ID))
    {

        if (!(param = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS)))
        {
            MXB_ERROR("Value not found: '%s'", MXS_JSON_PTR_PARAMETERS);
        }
        else if (!json_is_object(param))
        {
            MXB_ERROR("Value '%s' is not an object", MXS_JSON_PTR_PARAMETERS);
        }
        else if (runtime_is_count_or_null(param, CN_PORT)
                 && runtime_is_string_or_null(param, CN_ADDRESS)
                 && runtime_is_string_or_null(param, CN_AUTHENTICATOR)
                 && runtime_is_string_or_null(param, CN_AUTHENTICATOR_OPTIONS))
        {
            rval = true;
        }
    }

    return rval;
}

bool validate_user_json(json_t* json)
{
    bool rval = false;

    if (is_valid_string(json, MXS_JSON_PTR_ID)
        && is_valid_string(json, MXS_JSON_PTR_TYPE)
        && is_valid_string(json, MXS_JSON_PTR_PASSWORD)
        && is_valid_string(json, MXS_JSON_PTR_ACCOUNT))
    {
        json_t* account = mxb::json_ptr(json, MXS_JSON_PTR_ACCOUNT);

        if (json_to_account_type(account) == mxs::USER_ACCOUNT_UNKNOWN)
        {
            MXB_ERROR("The '%s' field is not a valid account value", MXS_JSON_PTR_ACCOUNT);
        }
        else
        {
            json_t* type  = mxb::json_ptr(json, MXS_JSON_PTR_TYPE);

            if (strcmp(json_string_value(type), CN_INET) == 0)
            {
                rval = true;
            }
            else if (strcmp(json_string_value(type), CN_UNIX) == 0)
            {
                rval = true;
            }
            else
            {
                MXB_ERROR("Invalid value for field '%s': %s", MXS_JSON_PTR_TYPE, json_string_value(type));
            }
        }
    }

    return rval;
}

bool validate_monitor_json(json_t* json)
{
    bool rval = validate_object_json(json);

    if (rval)
    {
        json_t* params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);

        for (auto a : {CN_USER, CN_PASSWORD})
        {
            if (!mxb::json_ptr(params, a))
            {
                MXB_ERROR("Mandatory parameter '%s' is not defined", a);
                rval = false;
                break;
            }
        }

        if (!mxb::json_is_type(json, MXS_JSON_PTR_MODULE, JSON_STRING))
        {
            MXB_ERROR("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
            rval = false;
        }
    }

    return rval;
}

bool validate_filter_json(json_t* json)
{
    bool rval = validate_object_json(json);

    if (rval)
    {
        if (!mxb::json_is_type(json, MXS_JSON_PTR_MODULE, JSON_STRING))
        {
            MXB_ERROR("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
            rval = false;
        }
    }

    return rval;
}

bool validate_service_json(json_t* json)
{
    bool rval = validate_object_json(json);

    if (rval)
    {
        auto servers = mxb::json_ptr(json, MXS_JSON_PTR_RELATIONSHIPS_SERVERS);
        auto services = mxb::json_ptr(json, MXS_JSON_PTR_RELATIONSHIPS_SERVICES);
        auto monitors = mxb::json_ptr(json, MXS_JSON_PTR_RELATIONSHIPS_MONITORS);

        if (json_array_size(monitors) && (json_array_size(servers) || json_array_size(services)))
        {
            MXB_ERROR("A service must use either servers and services or monitors, not both");
            rval = false;
        }
        else if (!mxb::json_is_type(json, MXS_JSON_PTR_ROUTER, JSON_STRING))
        {
            MXB_ERROR("Field '%s' is not a string", MXS_JSON_PTR_ROUTER);
            rval = false;
        }
    }

    return rval;
}

bool validate_create_service_json(json_t* json)
{
    return validate_service_json(json)
           && is_valid_string(json, MXS_JSON_PTR_ID)
           && is_valid_string(json, MXS_JSON_PTR_ROUTER);
}

bool ignored_core_parameters(const char* key)
{
    static const std::unordered_set<std::string> params =
    {
        CN_CACHEDIR,
        CN_CONNECTOR_PLUGINDIR,
        CN_DATADIR,
        CN_EXECDIR,
        CN_LANGUAGE,
        CN_LIBDIR,
        CN_LOGDIR,
        CN_MODULE_CONFIGDIR,
        CN_PERSISTDIR,
        CN_PIDDIR,
    };

    return params.count(key);
}

Service* get_service_from_listener_json(json_t* json)
{
    Service* rval = nullptr;
    const char* ptr = "/data/relationships/services/data/0/id";

    if (auto svc = mxb::json_ptr(json, ptr))
    {
        if (json_is_string(svc))
        {
            if (!(rval = Service::find(json_string_value(svc))))
            {
                MXB_ERROR("'%s' is not a valid service in MaxScale", json_string_value(svc));
            }
        }
        else
        {
            MXB_ERROR("Field '%s' is not a string", ptr);
        }
    }
    else
    {
        MXB_ERROR("Field '%s' is not defined", ptr);
    }

    return rval;
}

void prepare_for_destruction(Server* server)
{
    if (auto mon = MonitorManager::server_is_monitored(server))
    {
        unlink_monitor(mon, {server->name()});
    }

    for (auto service : service_server_in_use(server))
    {
        unlink_service(service, {server->name()});
    }
}

void prepare_for_destruction(const SFilterDef& filter)
{
    for (auto service : service_filter_in_use(filter))
    {
        service->remove_filter(filter);

        // Save the changes in the filters list
        save_config(service);
    }
}

void prepare_for_destruction(Service* service)
{
    if (!service->cluster())
    {
        StringSet names;

        for (mxs::Target* child : service->get_children())
        {
            names.insert(child->name());
        }

        unlink_service(service, names);
    }

    for (Service* s : service->get_parents())
    {
        unlink_service(s, {service->name()});
    }

    // Destroy listeners that point to the service. They are separate objects and are not managed by the
    // service which means we can't simply ignore them.
    for (const auto& l : mxs::Listener::find_by_service(service))
    {
        runtime_remove_config(l->name());
        mxs::Listener::destroy(l);
    }
}

void prepare_for_destruction(Monitor* monitor)
{
    for (auto svc : service_uses_monitor(monitor))
    {
        unlink_service(svc, {monitor->name()});
    }
}

Server* get_server_by_address(json_t* params)
{
    auto addr_js = json_object_get(params, CN_ADDRESS);
    auto port_js = json_object_get(params, CN_PORT);
    auto socket_js = json_object_get(params, CN_SOCKET);

    int port = json_integer_value(port_js);
    std::string addr = json_string_value(addr_js ? addr_js : socket_js);

    return ServerManager::find_by_address(addr, port);
}

/**
 * Combine `dest` and `src` into one object
 *
 * Removes JSON nulls and updates `dest` with the contents of `src`. Both objects are modified as a result of
 * this function call.
 *
 * @param dest JSON object where the combined result is stored
 * @param src  JSON object from where the values are copied
 */
void merge_json(json_t* dest, json_t* src)
{
    mxb::json_remove_nulls(dest);
    mxb::json_remove_nulls(src);
    json_object_update(dest, src);
}

}

void config_runtime_add_error(std::string_view error)
{
    std::lock_guard guard(this_unit.lock);
    this_unit.errors.push_back(std::string(error));
}

void runtime_add_warning(std::string_view warning)
{
    std::lock_guard guard(this_unit.lock);
    this_unit.warnings.push_back(std::string(warning));
}

std::string runtime_get_warnings()
{
    std::lock_guard guard(this_unit.lock);
    // We're really only expecting one warning per request
    auto rval = mxb::join(this_unit.warnings, ";");
    this_unit.warnings.clear();
    return rval;
}

json_t* runtime_get_json_error()
{
    std::lock_guard guard(this_unit.lock);
    json_t* obj = NULL;

    if (!this_unit.errors.empty())
    {
        // De-duplicate repeated errors
        auto it = std::unique(this_unit.errors.begin(), this_unit.errors.end());
        this_unit.errors.erase(it, this_unit.errors.end());

        obj = mxs_json_error(this_unit.errors);
        this_unit.errors.clear();
    }

    return obj;
}

bool runtime_create_volatile_server(const std::string& name, const std::string& address, int port,
                                    const mxs::ConfigParameters& extra)
{
    // This function can be called from a monitor thread in addition to the MainWorker.
    mxb_assert(!mxs::RoutingWorker::get_current());
    UnmaskPasswords unmask;

    bool rval = false;
    if (ServerManager::find_by_unique_name(name) == nullptr)
    {
        mxs::ConfigParameters parameters = extra;
        if (!address.empty())
        {
            auto param_name = address[0] == '/' ? CN_SOCKET : CN_ADDRESS;
            parameters.set(param_name, address);
        }
        parameters.set(CN_PORT, std::to_string(port));

        if (Server* server = ServerManager::create_volatile_server(name, parameters))
        {
            rval = true;
            MXB_NOTICE("Created volatile server '%s' at %s:%u", server->name(), server->address(),
                       server->port());
        }
        else
        {
            MXB_ERROR("Failed to create volatile server '%s', see error log for more details", name.c_str());
        }
    }
    else
    {
        MXB_ERROR("Failed to create volatile server '%s', server '%s' already exists",
                  name.c_str(), name.c_str());
    }

    return rval;
}

bool runtime_destroy_server(Server* server, bool force)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (force)
    {
        prepare_for_destruction(server);
    }

    std::vector<std::string> names;
    auto services = service_server_in_use(server);
    std::transform(services.begin(), services.end(), std::back_inserter(names),
                   std::mem_fn(&Service::name));

    auto filters = filter_depends_on_target(server);
    std::transform(filters.begin(), filters.end(), std::back_inserter(names),
                   std::mem_fn(&FilterDef::name));

    if (auto mon = MonitorManager::server_is_monitored(server))
    {
        names.push_back(mon->name());
    }

    if (!names.empty())
    {
        MXB_ERROR("Cannot destroy server '%s' as it is used by: %s",
                  server->name(), mxb::join(names, ", ").c_str());
    }
    else if (runtime_remove_config(server->name()))
    {
        MXB_NOTICE("Destroyed server '%s' at %s:%u", server->name(), server->address(), server->port());
        server->deactivate();
        rval = true;
    }

    return rval;
}

bool runtime_destroy_listener(const mxs::SListener& listener)
{
    UnmaskPasswords unmask;
    bool rval = false;
    std::string name = listener->name();
    std::string service = listener->service()->name();
    mxs::Listener::destroy(listener);

    if (runtime_remove_config(name.c_str()))
    {
        rval = true;
        MXB_NOTICE("Destroyed listener '%s' for service '%s'.", name.c_str(), service.c_str());
    }

    return rval;
}

bool runtime_destroy_filter(const SFilterDef& filter, bool force)
{
    UnmaskPasswords unmask;
    mxb_assert(filter);
    bool rval = false;

    if (force)
    {
        prepare_for_destruction(filter);
    }

    if (service_filter_in_use(filter).empty())
    {
        if (runtime_remove_config(filter->name()))
        {
            filter_destroy(filter);
            rval = true;
        }
    }
    else
    {
        MXB_ERROR("Filter '%s' cannot be destroyed: Remove it from all services first",
                  filter->name());
    }

    return rval;
}

bool runtime_destroy_service(Service* service, bool force)
{
    UnmaskPasswords unmask;
    bool rval = false;
    mxb_assert(service && service->active());

    if (force)
    {
        prepare_for_destruction(service);
    }

    if (force || service->can_be_destroyed())
    {
        if (runtime_remove_config(service->name()))
        {
            Service::destroy(service);
            rval = true;
        }
    }

    return rval;
}

bool runtime_destroy_monitor(Monitor* monitor, bool force)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (mxs::Config::get().config_sync_cluster == monitor->name())
    {
        MXB_ERROR("Cannot destroy monitor '%s', it is set as the configuration sync cluster.",
                  monitor->name());
        return false;
    }

    if (force)
    {
        prepare_for_destruction(monitor);
    }

    if (!monitor->configured_servers().empty() && !force)
    {
        MXB_ERROR("Cannot destroy monitor '%s', it is monitoring servers.", monitor->name());
    }
    else if (!service_uses_monitor(monitor).empty())
    {
        MXB_ERROR("Monitor '%s' cannot be destroyed as it is used by services.", monitor->name());
    }
    else if (runtime_remove_config(monitor->name()))
    {
        MonitorManager::deactivate_monitor(monitor);
        MXB_NOTICE("Destroyed monitor '%s'", monitor->name());
        rval = true;
    }

    return rval;
}

bool runtime_create_server_from_json(json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;
    StringSet relations;

    if (server_contains_required_fields(json)
        && extract_relations(json, relations, to_service_rel)
        && extract_relations(json, relations, to_monitor_rel))
    {
        json_t* params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);
        mxb::json_remove_nulls(params);
        const char* name = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_ID));
        mxb_assert(name);

        if (const char* other = mxs::Config::get_object_type(name))
        {
            MXB_ERROR("Can't create server '%s', a %s with that name already exists", name, other);
        }
        else if (Server* server = ServerManager::create_server(name, params))
        {
            if (link_target_to_objects(server->name(), relations))
            {
                rval = save_config(server);
            }
            else
            {
                runtime_destroy_server(server, false);
            }
        }
    }

    return rval;
}

bool runtime_alter_server_from_json(Server* server, json_t* new_json)
{
    UnmaskPasswords unmask;
    bool rval = false;
    std::unique_ptr<json_t> old_json(ServerManager::server_to_json_resource(server, ""));
    mxb_assert(old_json.get());

    if (is_valid_resource_body(new_json))
    {
        rval = true;

        auto& config = server->configuration();
        json_t* new_parameters = nullptr;

        if (json_t* parameters = mxb::json_ptr(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            rval = false;
            new_parameters = mxb::json_ptr(old_json.get(), MXS_JSON_PTR_PARAMETERS);
            json_object_update(new_parameters, parameters);
            mxb::json_remove_nulls(new_parameters);

            if (config.validate(new_parameters))
            {
                auto other = get_server_by_address(new_parameters);

                if (other && other != server)
                {
                    MXB_ERROR("Cannot update server '%s' to '[%s]:%d', server '%s' exists there already.",
                              server->name(), other->address(), other->port(), other->name());
                }
                else
                {
                    rval = true;
                }
            }
        }

        if (rval)
        {
            rval = server_to_object_relations(server, old_json.get(), new_json);
        }

        if (rval && new_parameters)
        {
            std::string old_addr = server->address();

            if ((rval = config.configure(new_parameters)))
            {
                rval = save_config(server);

                // Schedule the name resolution to take place immediately if the address changed
                if (server->address() != old_addr)
                {
                    server->start_addr_info_update();
                }

                // Restart the monitor that monitors this server to propagate the configuration changes
                // forward. This causes the monitor to pick up on new timeouts and addresses immediately.
                if (auto mon = MonitorManager::server_is_monitored(server))
                {
                    if (mon->is_running())
                    {
                        auto [stopped, errmsg] = mon->soft_stop();
                        if (stopped)
                        {
                            mon->start();
                        }
                        else
                        {
                            MXB_ERROR("Could not restart monitor '%s': %s Restart the monitor manually "
                                      "to ensure server settings are taken into use.",
                                      mon->name(), errmsg.c_str());
                        }
                    }
                }
            }
        }
    }

    return rval;
}

bool runtime_alter_server_relationships_from_json(Server* server, const char* type, json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;
    std::unique_ptr<json_t> old_json(ServerManager::server_to_json_resource(server, ""));
    mxb_assert(old_json.get());

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            type,
                                            "data",
                                            json_object_get(json, "data")));

        if (server_to_object_relations(server, old_json.get(), j.get()))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_create_monitor_from_json(json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_monitor_json(json))
    {
        const char* name = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_MODULE));

        if (const char* other = mxs::Config::get_object_type(name))
        {
            MXB_ERROR("Can't create monitor '%s', a %s with that name already exists", name, other);
        }
        else
        {
            json_t* params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);
            mxb::json_remove_nulls(params);
            mxb_assert_message(params, "Validation should guarantee that parameters exist");

            // Copy the module into the parameters to make sure it always appears in the parameters
            json_object_set(params, CN_MODULE, mxb::json_ptr(json, MXS_JSON_PTR_MODULE));

            if (inject_server_relationship_as_parameter(params, json, nullptr))
            {
                if (auto monitor = MonitorManager::create_monitor(name, module, params))
                {
                    if (save_config(monitor))
                    {
                        MXB_NOTICE("Created monitor '%s'", name);
                        MonitorManager::start_monitor(monitor);
                        rval = true;

                        // TODO: Do this with native types instead of JSON comparisons
                        mxb::Json old_json(monitor->to_json(""), mxb::Json::RefType::STEAL);
                        MXB_AT_DEBUG(bool rv = )
                        monitor_to_service_relations(monitor->name(), old_json.get_json(), json);
                        mxb_assert(rv);
                    }
                }
                else
                {
                    MXB_ERROR("Could not create monitor '%s' with module '%s'", name, module);
                }
            }
        }
    }

    return rval;
}

bool runtime_create_filter_from_json(json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_filter_json(json))
    {
        const char* name = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_MODULE));

        if (const char* other = mxs::Config::get_object_type(name))
        {
            MXB_ERROR("Can't create filter '%s', a %s with that name already exists", name, other);
        }
        else
        {
            json_t* parameters = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);

            // A filter is allowed to be constructed without parameters. To handle this gracefully, we
            // allocate an empty object. In addition, the module name is injected into it to make the
            // construction behave uniformly across all parameter types.
            parameters = parameters ? json_incref(parameters) : json_object();
            json_object_set_new(parameters, CN_MODULE, json_string(module));
            mxb::json_remove_nulls(parameters);

            if (auto filter = filter_alloc(name, parameters))
            {
                if (save_config(filter))
                {
                    MXB_NOTICE("Created filter '%s'", name);
                    rval = true;
                }
            }

            json_decref(parameters);
        }
    }

    return rval;
}

bool update_service_relationships(Service* service, json_t* json)
{
    // Construct only the relationship part of the resource. We don't need the rest and by doing this we avoid
    // any cross-worker communication (e.g. router diagnostics could cause it).
    auto old_json = json_pack("{s:{s: o}}", "data", "relationships", service->json_relationships(""));
    mxb_assert(old_json);

    bool rval = object_to_server_relations(service->name(), old_json, json)
        && service_to_service_relations(service->name(), old_json, json)
        && service_to_filter_relations(service, old_json, json)
        && service_to_monitor_relations(service->name(), old_json, json);

    json_decref(old_json);

    return rval;
}

void add_relationship_to_array(json_t* arr, const std::vector<mxb::Json>& relationships)
{
    for (const auto& rel : relationships)
    {
        json_array_append_new(arr, json_string(rel.get_string(CN_ID).c_str()));
    }
}

void remove_relationship_parameters(json_t* params)
{
    json_object_del(params, CN_SERVERS);
    json_object_del(params, CN_TARGETS);
    json_object_del(params, CN_CLUSTER);
    json_object_del(params, CN_FILTER);
}

void convert_relationships_to_parameters(json_t* params, json_t* json)
{
    remove_relationship_parameters(params);

    mxb::Json js(json, mxb::Json::RefType::COPY);
    auto cluster = js.at(MXS_JSON_PTR_RELATIONSHIPS_MONITORS).get_array_elems();
    auto services = js.at(MXS_JSON_PTR_RELATIONSHIPS_SERVICES).get_array_elems();
    auto servers = js.at(MXS_JSON_PTR_RELATIONSHIPS_SERVERS).get_array_elems();
    auto filters = js.at(MXS_JSON_PTR_RELATIONSHIPS_FILTERS).get_array_elems();

    if (!cluster.empty())
    {
        json_object_set_new(params, CN_CLUSTER, json_string(cluster[0].get_string(CN_ID).c_str()));
    }
    else if (!services.empty())
    {
        json_t* arr = json_array();
        add_relationship_to_array(arr, services);
        add_relationship_to_array(arr, servers);
        json_object_set_new(params, CN_TARGETS, arr);
    }
    else
    {
        json_t* arr = json_array();
        add_relationship_to_array(arr, servers);
        json_object_set_new(params, CN_SERVERS, arr);
    }

    if (!filters.empty())
    {
        json_t* arr = json_array();
        add_relationship_to_array(arr, filters);
        json_object_set_new(params, CN_FILTERS, arr);
    }
}

bool runtime_create_service_from_json(json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_create_service_json(json))
    {
        const char* name = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_ID));

        if (const char* other = mxs::Config::get_object_type(name))
        {
            MXB_ERROR("Can't create service '%s', a %s with that name already exists", name, other);
        }
        else if (json_t* params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS))
        {
            json_t* router = mxb::json_ptr(json, MXS_JSON_PTR_ROUTER);
            json_object_set(params, CN_ROUTER, router);
            mxb::json_remove_nulls(params);

            convert_relationships_to_parameters(params, json);

            if (auto service = Service::create(name, params))
            {
                if (save_config(service))
                {
                    MXB_NOTICE("Created service '%s'", name);
                    service->start();
                    rval = true;
                }
                else
                {
                    MXB_ERROR("Failed to serialize service '%s'", name);
                }
            }
            else
            {
                MXB_ERROR("Could not create service '%s' with module '%s'", name, json_string_value(router));
            }

            // Remove the parameters that were added. This keeps the original JSON object valid so that it can
            // be reused for altering the service.
            remove_relationship_parameters(params);
        }
    }

    return rval;
}

bool runtime_alter_monitor_from_json(Monitor* monitor, json_t* new_json)
{
    UnmaskPasswords unmask;
    bool success = false;
    mxb::Json old_json(MonitorManager::monitor_to_json(monitor, ""), mxb::Json::RefType::STEAL);
    mxb_assert(old_json.get_json());

    if (is_valid_resource_body(new_json)
        && monitor_to_service_relations(monitor->name(), old_json.get_json(), new_json))
    {
        json_t* params = monitor->parameters_to_json();
        json_t* new_params = mxb::json_ptr(new_json, MXS_JSON_PTR_PARAMETERS);

        if (new_params)
        {
            mxb::json_remove_nulls(new_params);
            json_object_update(params, new_params);
        }

        // Now inject the servers from the relationship endpoint
        if (inject_server_relationship_as_parameter(params, new_json, monitor))
        {
            // Make sure there are no null values left in the parameters, the configuration code
            // treats that as an error.
            mxb::json_remove_nulls(params);

            if (MonitorManager::reconfigure_monitor(monitor, params))
            {
                success = save_config(monitor);
            }
        }

        json_decref(params);
    }

    return success;
}

bool runtime_alter_monitor_relationships_from_json(Monitor* monitor, const char* type, json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;
    std::unique_ptr<json_t> old_json(MonitorManager::monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            type,
                                            "data",
                                            json_object_get(json, "data")));

        if (runtime_alter_monitor_from_json(monitor, j.get()))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_alter_service_relationships_from_json(Service* service, const char* type, json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            type,
                                            "data",
                                            json_object_get(json, "data")));

        if (runtime_alter_service_from_json(service, j.get()))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_alter_service_from_json(Service* service, json_t* new_json)
{
    UnmaskPasswords unmask;
    bool rval = validate_service_json(new_json);

    if (rval)
    {
        if (json_t* new_params = mxb::json_ptr(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            json_t* params = service->json_parameters();
            merge_json(params, new_params);
            rval = service->configure(params);
            json_decref(params);
        }

        if (rval)
        {
            // TODO: This should be done before configuration and reverted if the configuration change fails.
            rval = update_service_relationships(service, new_json);
        }

        if (rval)
        {
            save_config(service);
        }
    }

    return rval;
}

bool runtime_alter_filter_from_json(const SFilterDef& filter, json_t* new_json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_filter_json(new_json))
    {
        rval = true;
        auto& config = filter->configuration();

        if (json_t* new_params = mxb::json_ptr(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            rval = false;

            // The new parameters are merged with the old parameters to get a complete filter definition.
            json_t* params = config.to_json();
            merge_json(params, new_params);

            if (config.validate(params) && config.configure(params))
            {
                rval = save_config(filter);
            }

            json_decref(params);
        }
    }

    return rval;
}

bool runtime_create_listener_from_json(json_t* json, Service* service)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (!service && !(service = get_service_from_listener_json(json)))
    {
        return false;
    }

    if (validate_listener_json(json))
    {
        const char* name = get_string_or_null(json, MXS_JSON_PTR_ID);
        std::string reason;

        if (!config_is_valid_name(name, &reason))
        {
            MXB_ERROR("%s", reason.c_str());
        }
        else if (const char* other = mxs::Config::get_object_type(name))
        {
            MXB_ERROR("Can't create listener '%s', a %s with that name already exists", name, other);
        }
        else
        {
            json_t* params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);
            mxb::json_remove_nulls(params);

            // The service is expressed as a relationship instead of a parameter. Add it to the parameters so
            // that it's expressed in the same way regardless of the way the listener is created.
            json_object_set_new(params, "service", json_string(service->name()));

            if (auto listener = mxs::Listener::create(name, params))
            {
                if (listener->listen() && save_config(listener))
                {
                    MXB_NOTICE("Created listener '%s' at %s:%u for service '%s'",
                               name, listener->address(), listener->port(), service->name());

                    rval = true;
                }
                else
                {
                    mxs::Listener::destroy(listener);
                    mxb_assert(!mxs::Listener::find(name));
                }
            }
        }
    }

    return rval;
}

bool runtime_alter_listener_from_json(mxs::SListener listener, json_t* new_json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_service_json(new_json))
    {
        if (json_t* new_params = mxb::json_ptr(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            auto& config = listener->configuration();
            json_t* params = config.to_json();
            merge_json(params, new_params);

            if (config.validate(params) && config.configure(params))
            {
                // TODO: Configure the protocol module as well
                rval = save_config(listener);
            }

            json_decref(params);
        }
    }

    return rval;
}

bool runtime_create_user_from_json(json_t* json)
{
    bool rval = false;

    if (validate_user_json(json))
    {
        const char* user = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_ID));
        const char* password = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_PASSWORD));
        std::string strtype = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_TYPE));
        auto type = json_to_account_type(mxb::json_ptr(json, MXS_JSON_PTR_ACCOUNT));
        const char* err = NULL;

        if (strtype == CN_INET && (err = admin_add_inet_user(user, password, type)) == ADMIN_SUCCESS)
        {
            MXB_NOTICE("Create network user '%s'", user);
            rval = true;
        }
        else if (strtype == CN_UNIX)
        {
            MXB_ERROR("UNIX users are no longer supported.");
        }
        else if (err)
        {
            MXB_ERROR("Failed to add user '%s': %s", user, err);
        }
    }

    return rval;
}

bool runtime_remove_user(const char* id)
{
    bool rval = false;
    const char* err = admin_remove_inet_user(id);

    if (err == ADMIN_SUCCESS)
    {
        MXB_NOTICE("Deleted network user '%s'", id);
        rval = true;
    }
    else
    {
        MXB_ERROR("Failed to remove user '%s': %s", id, err);
    }

    return rval;
}

bool runtime_alter_user(const std::string& user, const std::string& type, json_t* json)
{
    bool rval = false;
    const char* password = json_string_value(mxb::json_ptr(json, MXS_JSON_PTR_PASSWORD));

    if (!password)
    {
        MXB_ERROR("No password provided");
    }
    else if (type != CN_INET)
    {
        MXB_ERROR("Users of type '%s' are not supported", type.c_str());
    }
    else if (const char* err = admin_alter_inet_user(user.c_str(), password))
    {
        MXB_ERROR("%s", err);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool runtime_alter_maxscale_from_json(json_t* json)
{
    UnmaskPasswords unmask;
    bool rval = false;

    if (validate_object_json(json))
    {
        json_t* new_params = mxb::json_ptr(json, MXS_JSON_PTR_PARAMETERS);
        json_t* params = mxs::Config::get().params_to_json();
        merge_json(params, new_params);
        auto& config = mxs::Config::get();

        // TODO: Don't strip out these parameters and define them in the core specification instead.
        const char* key;
        json_t* value;
        void* ptr;

        // TODO: This should not be needed anymore
        json_object_foreach_safe(params, ptr, key, value)
        {
            if (ignored_core_parameters(key))
            {
                json_object_del(params, key);
            }
        }

        if (config.validate(params) && config.configure(params))
        {
            rval = save_config(config);
        }

        json_decref(params);
    }

    return rval;
}

bool runtime_thread_rebalance(mxs::RoutingWorker& from,
                              const std::string& sessions,
                              const std::string& recipient)
{
    bool rv = false;

    int nSessions = std::numeric_limits<int>::max();

    if (sessions.empty() || mxb::get_int(sessions, &nSessions))
    {
        int windex_to = -1;

        if (!recipient.empty() && mxb::get_int(recipient, &windex_to))
        {
            mxs::RoutingWorker* pTo = mxs::RoutingWorker::get_by_index(windex_to);

            if (pTo)
            {
                // Execute() and not call(), so that we do not have to worry about
                // possible deadlocks.
                if (from.execute([pTo, nSessions]() {
                            auto* pFrom = mxs::RoutingWorker::get_current();

                            pFrom->rebalance(pTo, nSessions);
                        }, mxb::Worker::EXECUTE_QUEUED))
                {
                    rv = true;
                }
                else
                {
                    MXB_ERROR("Could not initiate rebalancing.");
                }
            }
            else
            {
                MXB_ERROR("The 'recipient' value '%s' does not refer to a worker.", recipient.c_str());
            }
        }
        else
        {
            MXB_ERROR("'recipient' argument not provided, or value is not a valid integer.");
        }
    }
    else
    {
        MXB_ERROR("'sessions' argument provided, but value '%s' is not a valid integer.", sessions.c_str());
    }

    return rv;
}

bool runtime_threads_rebalance(const std::string& arg_threshold)
{
    bool rv = true;

    int64_t threshold = -1;

    const auto& config = mxs::Config::get();

    if (!arg_threshold.empty())
    {
        std::string message;
        if (!config.rebalance_threshold.parameter().from_string(arg_threshold, &threshold, &message))
        {
            MXB_ERROR("%s", message.c_str());
            rv = false;
        }
    }
    else
    {
        threshold = config.rebalance_threshold.get();
    }

    if (rv)
    {
        mxb_assert(threshold > 0);

        auto* main_worker = mxs::MainWorker::get();
        main_worker->balance_workers(mxs::MainWorker::BALANCE_UNCONDITIONALLY, threshold);
    }

    return rv;
}

bool runtime_discard_config(const char* name, bool warn_about_static_objects)
{
    if (!mxs::Config::get().config_sync_cluster.empty())
    {
        return true;
    }

    bool rval = true;
    std::string filename = mxs::config_persistdir() + "/"s + name + ".cnf";

    if (unlink(filename.c_str()) == -1 && errno != ENOENT)
    {
        MXB_ERROR("Failed to remove persisted configuration '%s': %d, %s",
                  filename.c_str(), errno, mxb_strerror(errno));
        rval = false;
    }

    if (warn_about_static_objects && mxs::Config::is_static_object(name))
    {
        auto msg = mxb::string_printf("Object '%s' is defined in a static configuration file and "
                                      "cannot be permanently deleted. If MaxScale is restarted, "
                                      "the object will appear again.", name);
        runtime_add_warning(msg);
    }

    return rval;
}

bool runtime_save_config(const char* name, const std::string& config)
{
    bool rval = false;
    std::string filename = mxs::config_persistdir() + "/"s + name + ".cnf";
    bool new_file = access(filename.c_str(), F_OK) != 0 && errno == ENOENT;

    if (auto err = mxb::save_file(filename, config); !err.empty())
    {
        MXB_ERROR("Failed to save config: %s", err.c_str());
    }
    else
    {
        if (mxs::Config::get().load_persisted_configs)
        {
            mxs::Config::set_object_source_file(name, filename);

            if (mxs::Config::is_static_object(name))
            {
                auto msg = mxb::string_printf("Saving runtime modifications to '%s' in '%s'. "
                                              "The modified values will override the values found "
                                              "in the static configuration files.",
                                              name, filename.c_str());
                runtime_add_warning(msg);

                if (new_file)
                {
                    MXB_WARNING("%s", msg.c_str());
                }
            }
        }

        rval = true;
    }

    return rval;
}

bool runtime_link_service(Service* service, const std::set<std::string>& targets)
{
    return link_service(service, targets);
}

bool runtime_unlink_service(Service* service, const std::set<std::string>& targets)
{
    return unlink_service(service, targets);
}
