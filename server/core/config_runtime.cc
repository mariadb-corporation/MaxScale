/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>

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

#include <maxbase/atomic.h>
#include <maxscale/clock.h>
#include <maxscale/jansson.hh>
#include <maxscale/json_api.hh>
#include <maxscale/paths.hh>
#include <maxscale/router.hh>
#include <maxscale/users.hh>

#include "internal/adminusers.hh"
#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/listener.hh"
#include "internal/modules.hh"
#include "internal/monitor.hh"
#include "internal/monitormanager.hh"
#include "internal/servermanager.hh"

typedef std::set<std::string>    StringSet;
typedef std::vector<std::string> StringVector;

using std::tie;
using maxscale::Monitor;
using SListener = std::shared_ptr<Listener>;
using namespace std::literals::string_literals;

#define RUNTIME_ERRMSG_BUFSIZE 512
thread_local std::vector<std::string> runtime_errmsg;

typedef std::function<bool (const std::string&, const std::string&)> JsonValidator;
typedef std::pair<const char*, JsonValidator>                        Relationship;

namespace
{

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

const MXS_MODULE_PARAM* get_type_parameters(const char* type)
{
    if (strcmp(type, CN_SERVICE) == 0)
    {
        return common_service_params();
    }
    else if (strcmp(type, CN_MONITOR) == 0)
    {
        return common_monitor_params();
    }
    else if (strcmp(type, CN_FILTER) == 0)
    {
        return common_filter_params();
    }

    MXS_NOTICE("Module type with no default parameters used: %s", type);
    mxb_assert_message(!true, "Module type with no default parameters used");
    return NULL;
}

std::string get_module_param_name(const std::string& type)
{
    if (type == CN_SERVICE)
    {
        return CN_ROUTER;
    }
    else if (type == CN_LISTENER)
    {
        return CN_PROTOCOL;
    }
    else if (type == CN_MONITOR || type == CN_FILTER)
    {
        return CN_MODULE;
    }

    mxb_assert(!true);      // Should not be called for a server.
    return "";
}

/**
 * @brief Load module default parameters
 *
 * @param name        Name of the module to load
 * @param module_type Type of the module
 * @param object_type Type of the object (server, service, listener etc.)
 *
 * @return Whether loading succeeded and the list of default parameters
 */
std::pair<bool, mxs::ConfigParameters>
load_defaults(const char* name, mxs::ModuleType module_type, const char* object_type)
{
    bool rval = false;
    mxs::ConfigParameters params;

    if (const MXS_MODULE* mod = get_module(name, module_type))
    {
        config_add_defaults(&params, get_type_parameters(object_type));
        config_add_defaults(&params, mod->parameters);
        params.set(get_module_param_name(object_type), name);
        rval = true;
    }
    else
    {
        MXS_ERROR("Failed to load module '%s': %s",
                  name, errno ? mxs_strerror(errno) : "See MaxScale logs for details");
    }

    return {rval, params};
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
        MXS_ERROR("Service '%s' cannot use cluster '%s': %s", service->name(), monitor->name(), err.c_str());
    }

    return ok;
}

bool unlink_service_from_monitor(Service* service, mxs::Monitor* monitor)
{
    bool ok = service->remove_cluster(monitor);

    if (!ok)
    {
        MXS_ERROR("Service '%s' does not use monitor '%s'", service->name(), monitor->name());
    }

    return ok;
}

bool runtime_link_target(const std::string& subject, const std::string& target)
{
    bool rval = false;

    if (auto service = Service::find(target))
    {
        if (auto monitor = MonitorManager::find_monitor(subject.c_str()))
        {
            rval = link_service_to_monitor(service, monitor);
        }
        else if (auto cluster = service->cluster())
        {
            MXS_ERROR("The servers of the service '%s' are defined by the monitor '%s'. "
                      "Servers cannot explicitly be added to the service.",
                      service->name(), cluster->name());
        }
        else if (auto tgt = mxs::Target::find(subject))
        {
            if (service == tgt)
            {
                MXS_ERROR("Cannot link '%s' to itself", service->name());
            }
            else if (service->has_target(tgt))
            {
                MXS_ERROR("Service '%s' already uses target '%s'", service->name(), subject.c_str());
            }
            else
            {
                auto cycle = get_cycle_name(service, tgt);

                if (!cycle.empty())
                {
                    MXS_ERROR("Linking '%s' to '%s' would result in a circular configuration: %s",
                              subject.c_str(), service->name(), cycle.c_str());
                }
                else
                {
                    rval = true;
                    service->add_target(tgt);
                }
            }
        }
        else
        {
            MXS_ERROR("Could not find target with name '%s'", subject.c_str());
        }

        if (rval)
        {
            std::ostringstream ss;
            service->persist(ss);
            rval = runtime_save_config(service->name(), ss.str());
        }
    }
    else if (auto monitor = MonitorManager::find_monitor(target.c_str()))
    {
        if (auto srv = ServerManager::find_by_unique_name(subject))
        {
            std::string error_msg;
            if (MonitorManager::add_server_to_monitor(monitor, srv, &error_msg))
            {
                rval = true;
            }
            else
            {
                MXS_ERROR("%s", error_msg.c_str());
            }
        }
        else if (auto service = Service::find(subject))
        {
            rval = link_service_to_monitor(service, monitor);
        }
        else
        {
            MXS_ERROR("No server or service named '%s' found", subject.c_str());
        }

        if (rval)
        {
            std::ostringstream ss;
            MonitorManager::monitor_persist(monitor, ss);
            rval = runtime_save_config(monitor->name(), ss.str());
        }
    }
    else
    {
        MXS_ERROR("No monitor or service named '%s' found", target.c_str());
    }

    if (rval)
    {
        MXS_NOTICE("Added '%s' to '%s'", subject.c_str(), target.c_str());
    }

    return rval;
}

bool runtime_unlink_target(const std::string& subject, const std::string& target)
{
    bool rval = false;

    if (auto service = Service::find(target))
    {
        if (auto monitor = MonitorManager::find_monitor(subject.c_str()))
        {
            rval = unlink_service_from_monitor(service, monitor);
        }
        else if (auto cluster = service->cluster())
        {
            MXS_ERROR("The servers of the service '%s' are defined by the monitor '%s'. "
                      "Servers cannot explicitly be removed from the service.",
                      service->name(), cluster->name());
        }
        else if (auto tgt = mxs::Target::find(subject))
        {
            rval = true;
            service->remove_target(tgt);
        }
        else
        {
            MXS_ERROR("Target '%s' not found", subject.c_str());
        }

        if (rval)
        {
            std::ostringstream ss;
            service->persist(ss);
            rval = runtime_save_config(service->name(), ss.str());
        }
    }
    else if (auto monitor = MonitorManager::find_monitor(target.c_str()))
    {
        if (auto srv = ServerManager::find_by_unique_name(subject))
        {
            std::string error_msg;
            if (MonitorManager::remove_server_from_monitor(monitor, srv, &error_msg))
            {
                rval = true;
            }
            else
            {
                MXS_ERROR("%s", error_msg.c_str());
            }
        }
        else if (auto service = Service::find(subject))
        {
            rval = unlink_service_from_monitor(service, monitor);
        }
        else
        {
            MXS_ERROR("No server named '%s' found", subject.c_str());
        }

        if (rval)
        {
            std::ostringstream ss;
            MonitorManager::monitor_persist(monitor, ss);
            rval = runtime_save_config(monitor->name(), ss.str());
        }
    }
    else
    {
        MXS_ERROR("No monitor or service named '%s' found", target.c_str());
    }

    if (rval)
    {
        MXS_NOTICE("Removed '%s' from '%s'", subject.c_str(), target.c_str());
    }

    return rval;
}

/**
 * @brief Convert a string value to a positive integer
 *
 * If the value is not a positive integer, an error is printed to @c dcb.
 *
 * @param value String value
 * @return 0 on error, otherwise a positive integer
 */
int get_positive_int(const char* value)
{
    char* endptr;
    long ival = strtol(value, &endptr, 10);

    if (*endptr == '\0' && ival > 0 && ival < std::numeric_limits<int>::max())
    {
        return ival;
    }

    return 0;
}

bool is_valid_integer(const char* value)
{
    char* endptr;
    return strtol(value, &endptr, 10) >= 0 && *value && *endptr == '\0';
}

bool undefined_mandatory_parameter(const MXS_MODULE_PARAM* mod_params,
                                   const mxs::ConfigParameters* params)
{
    bool rval = false;
    mxb_assert(mod_params);

    for (int i = 0; mod_params[i].name; i++)
    {
        if ((mod_params[i].options & MXS_MODULE_OPT_REQUIRED) && !params->contains(mod_params[i].name))
        {
            MXS_ERROR("Mandatory parameter '%s' is not defined.", mod_params[i].name);
            rval = true;
        }
    }

    return rval;
}

bool validate_param(const MXS_MODULE_PARAM* basic,
                    const MXS_MODULE_PARAM* module,
                    const char* key,
                    const char* value)
{
    std::string error;
    bool rval = validate_param(basic, module, key, value, &error);
    if (!rval)
    {
        MXS_ERROR("%s", error.c_str());
    }
    return rval;
}

bool validate_param(const MXS_MODULE_PARAM* basic,
                    const MXS_MODULE_PARAM* module,
                    mxs::ConfigParameters* params)
{
    bool rval = std::all_of(params->begin(), params->end(),
                            [basic, module](const std::pair<std::string, std::string>& p) {
                                return validate_param(basic, module, p.first.c_str(), p.second.c_str());
                            });

    if (undefined_mandatory_parameter(basic, params) || undefined_mandatory_parameter(module, params))
    {
        rval = false;
    }

    return rval;
}

mxs::ConfigParameters extract_parameters(json_t* json)
{
    mxs::ConfigParameters rval;

    if (json_t* parameters = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        rval = mxs::ConfigParameters::from_json(parameters);
    }

    return rval;
}

bool extract_ordered_relations(json_t* json,
                               StringVector& relations,
                               Relationship rel)
{
    bool rval = true;
    json_t* arr = mxs_json_pointer(json, rel.first);

    if (arr && json_is_array(arr))
    {
        size_t size = json_array_size(arr);

        for (size_t j = 0; j < size; j++)
        {
            json_t* obj = json_array_get(arr, j);
            json_t* id = json_object_get(obj, CN_ID);
            json_t* type = mxs_json_pointer(obj, CN_TYPE);

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
                    rval = false;
                }
            }
            else
            {
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

    json_t* data = mxs_json_pointer(json, relation);
    json_t* base = mxs_json_pointer(json, str.c_str());

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
    json_t* value = mxs_json_pointer(json, path);

    if (value && json_is_string(value))
    {
        rval = json_string_value(value);
    }

    return rval;
}

bool runtime_is_string_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxs_json_pointer(json, path);

    if (value && !json_is_string(value) && !json_is_null(value))
    {
        MXS_ERROR("Parameter '%s' is not a string but %s", path, json_type_to_string(value));
        rval = false;
    }

    return rval;
}

bool runtime_is_bool_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxs_json_pointer(json, path);

    if (value && !json_is_boolean(value) && !json_is_null(value))
    {
        MXS_ERROR("Parameter '%s' is not a boolean but %s", path, json_type_to_string(value));
        rval = false;
    }

    return rval;
}

bool runtime_is_size_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxs_json_pointer(json, path);

    if (value)
    {
        if (!json_is_integer(value) && !json_is_string(value) && !json_is_null(value))
        {
            MXS_ERROR("Parameter '%s' is not an integer or a string but %s",
                      path, json_type_to_string(value));
            rval = false;
        }
        else if ((json_is_integer(value) && json_integer_value(value) < 0)
                 || (json_is_string(value) && !get_suffixed_size(json_string_value(value), nullptr)))
        {
            MXS_ERROR("Parameter '%s' is not a valid size", path);
            rval = false;
        }
    }

    return rval;
}

bool runtime_is_count_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxs_json_pointer(json, path);

    if (value)
    {
        if (!json_is_integer(value) && !json_is_null(value))
        {
            MXS_ERROR("Parameter '%s' is not an integer but %s", path, json_type_to_string(value));
            rval = false;
        }
        else if (json_is_integer(value) && json_integer_value(value) < 0)
        {
            MXS_ERROR("Parameter '%s' is a negative integer", path);
            rval = false;
        }
    }

    return rval;
}

/** Check that the body at least defies a data member */
bool is_valid_resource_body(json_t* json)
{
    bool rval = true;

    if (mxs_json_pointer(json, MXS_JSON_PTR_DATA) == NULL)
    {
        MXS_ERROR("No '%s' field defined", MXS_JSON_PTR_DATA);
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
            json_t* j = mxs_json_pointer(json, *it);

            if (j && !json_is_object(j))
            {
                MXS_ERROR("Relationship '%s' is not an object", *it);
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
        MXS_ERROR("%s", err.c_str());
    }
    else if (!mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        MXS_ERROR("Field '%s' is not defined", MXS_JSON_PTR_PARAMETERS);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool unlink_target_from_objects(const std::string& target, StringSet& relations)
{
    bool rval = true;

    for (const auto& rel : relations)
    {
        if (!runtime_unlink_target(target, rel))
        {
            rval = false;
        }
    }

    return rval;
}

bool link_target_to_objects(const std::string& target, StringSet& relations)
{
    bool rval = true;

    for (const auto& rel : relations)
    {
        if (!runtime_link_target(target, rel))
        {
            unlink_target_from_objects(target, relations);
            rval = false;
            break;
        }
    }

    return rval;
}

std::string json_int_to_string(json_t* json)
{
    char str[25];   // Enough to store any 64-bit integer value
    int64_t i = json_integer_value(json);
    snprintf(str, sizeof(str), "%ld", i);
    return std::string(str);
}

bool have_ssl_json(json_t* params)
{
    return mxs_json_pointer(params, CN_SSL_KEY)
           || mxs_json_pointer(params, CN_SSL_CERT)
           || mxs_json_pointer(params, CN_SSL_CA_CERT);
}

enum object_type
{
    OT_SERVER,
    OT_LISTENER
};

bool validate_ssl_json(json_t* params, object_type type)
{
    bool rval = true;

    if (runtime_is_string_or_null(params, CN_SSL_KEY)
        && runtime_is_string_or_null(params, CN_SSL_CERT)
        && runtime_is_string_or_null(params, CN_SSL_CA_CERT)
        && runtime_is_string_or_null(params, CN_SSL_VERSION)
        && runtime_is_count_or_null(params, CN_SSL_CERT_VERIFY_DEPTH))
    {
        json_t* key = mxs_json_pointer(params, CN_SSL_KEY);
        json_t* cert = mxs_json_pointer(params, CN_SSL_CERT);
        json_t* ca_cert = mxs_json_pointer(params, CN_SSL_CA_CERT);

        if (type == OT_LISTENER && !(key && cert && ca_cert))
        {
            MXS_ERROR("SSL configuration for listeners requires '%s', '%s' and '%s' parameters",
                      CN_SSL_KEY, CN_SSL_CERT, CN_SSL_CA_CERT);
            rval = false;
        }
        else if (type == OT_SERVER)
        {
            if (!ca_cert)
            {
                MXS_ERROR("SSL configuration for servers requires at least the '%s' parameter",
                          CN_SSL_CA_CERT);
                rval = false;
            }
            else if ((key == nullptr) != (cert == nullptr))
            {
                MXS_ERROR("Both '%s' and '%s' must be defined", CN_SSL_KEY, CN_SSL_CERT);
                rval = false;
            }
        }

        json_t* ssl_version = mxs_json_pointer(params, CN_SSL_VERSION);
        const char* ssl_version_str = ssl_version ? json_string_value(ssl_version) : NULL;

        if (ssl_version_str
            && mxb::ssl_version::from_string(ssl_version_str) == mxb::ssl_version::SSL_UNKNOWN)
        {
            MXS_ERROR("Invalid value for '%s': %s", CN_SSL_VERSION, ssl_version_str);
            rval = false;
        }
    }

    return rval;
}

bool server_to_object_relations(Server* server, json_t* old_json, json_t* new_json)
{
    if (mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS_SERVICES) == NULL
        && mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS_MONITORS) == NULL)
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
        if (is_null_relation(new_json, a.first) || mxs_json_pointer(new_json, a.first))
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

    json_t* obj = mxs_json_pointer(json, MXS_JSON_PTR_DATA);

    if (!obj)
    {
        MXS_ERROR("Field '%s' is not defined", MXS_JSON_PTR_DATA);
        rval = false;
    }
    else if (!json_is_array(obj) && !json_is_null(obj))
    {
        MXS_ERROR("Field '%s' is not an array", MXS_JSON_PTR_DATA);
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
        MXS_ERROR("%s", err.c_str());
    }

    return err.empty();
}

bool server_relationship_to_parameter(json_t* json, mxs::ConfigParameters* params)
{
    StringSet relations;
    bool rval = false;

    if (extract_relations(json, relations, to_server_rel))
    {
        rval = true;

        if (!relations.empty())
        {
            auto servers = std::accumulate(std::next(relations.begin()), relations.end(), *relations.begin(),
                                           [](std::string sum, std::string s) {
                                               return sum + ',' + s;
                                           });
            params->set(CN_SERVERS, servers);
        }
        else if (json_t* rel = mxs_json_pointer(json, MXS_JSON_PTR_RELATIONSHIPS_SERVERS))
        {
            if (json_is_array(rel) || json_is_null(rel))
            {
                mxb_assert(json_is_null(rel) || json_array_size(rel) == 0);
                // Empty relationship, remove the parameter
                params->remove(CN_SERVERS);
            }
        }
    }

    return rval;
}

bool unlink_object_from_targets(const std::string& target, StringSet& relations)
{
    bool rval = true;

    for (const auto& rel : relations)
    {
        if (!runtime_unlink_target(rel, target))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

bool link_object_to_targets(const std::string& target, StringSet& relations)
{
    bool rval = true;

    for (const auto& rel : relations)
    {
        if (!runtime_link_target(rel, target))
        {
            unlink_target_from_objects(rel, relations);
            rval = false;
            break;
        }
    }

    return rval;
}

std::pair<bool, mxs::ConfigParameters>
extract_and_validate_params(json_t* json, const char* module, mxs::ModuleType module_type,
                            const char* module_param_name)
{
    bool ok = false;
    mxs::ConfigParameters params;

    if (const MXS_MODULE* mod = get_module(module, module_type))
    {
        tie(ok, params) = load_defaults(module, module_type, module_param_name);
        mxb_assert(ok);

        params.set_multiple(extract_parameters(json));
        ok = validate_param(get_type_parameters(module_param_name), mod->parameters, &params);

        if (ok && mod->specification)
        {
            ok = mod->specification->validate(params);
        }
    }
    else
    {
        MXS_ERROR("Unknown module: %s", module);
    }

    return {ok, params};
}

bool update_object_relations(const std::string& target,
                             Relationship rel,
                             json_t* old_json,
                             json_t* new_json)
{
    if (mxs_json_pointer(new_json, rel.first) == NULL)
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
        MXS_ERROR("Could not find all servers that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_service_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_service_rel, old_json, new_json);

    if (!rval)
    {
        MXS_ERROR("Could not find all services that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_filter_relations(Service* service, json_t* old_json, json_t* new_json)
{
    if (mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS_FILTERS) == NULL)
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

        MXS_ERROR("Could not find all filters that '%s' relates to", service->name());
    }

    return rval;
}

bool service_to_monitor_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_monitor_rel, old_json, new_json);

    if (!rval)
    {
        MXS_ERROR("Could not find the monitor that '%s' relates to", target.c_str());
    }

    return rval;
}

bool monitor_to_service_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, to_service_rel, old_json, new_json);

    if (!rval)
    {
        MXS_ERROR("Could not find the service that '%s' relates to", target.c_str());
    }

    return rval;
}

/**
 * @brief Check if the service parameter can be altered at runtime
 *
 * @param key Parameter name
 * @return True if the parameter can be altered
 */
bool is_dynamic_param(const std::string& key)
{
    return key != CN_TYPE
           && key != CN_ROUTER
           && key != CN_SERVERS
           && key != CN_FILTERS;
}

bool validate_logs_json(json_t* json)
{
    json_t* param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
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

    if (!(param = mxs_json_pointer(json, MXS_JSON_PTR_ID)))
    {
        MXS_ERROR("Value not found: '%s'", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(param))
    {
        MXS_ERROR("Value '%s' is not a string", MXS_JSON_PTR_ID);
    }
    else if (!(param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS)))
    {
        MXS_ERROR("Value not found: '%s'", MXS_JSON_PTR_PARAMETERS);
    }
    else if (!json_is_object(param))
    {
        MXS_ERROR("Value '%s' is not an object", MXS_JSON_PTR_PARAMETERS);
    }
    else if (runtime_is_count_or_null(param, CN_PORT)
             && runtime_is_string_or_null(param, CN_ADDRESS)
             && runtime_is_string_or_null(param, CN_AUTHENTICATOR)
             && runtime_is_string_or_null(param, CN_AUTHENTICATOR_OPTIONS)
             && (!have_ssl_json(param) || validate_ssl_json(param, OT_LISTENER)))
    {
        rval = true;
    }

    return rval;
}

bool validate_user_json(json_t* json)
{
    bool rval = false;
    json_t* id = mxs_json_pointer(json, MXS_JSON_PTR_ID);
    json_t* type = mxs_json_pointer(json, MXS_JSON_PTR_TYPE);
    json_t* password = mxs_json_pointer(json, MXS_JSON_PTR_PASSWORD);
    json_t* account = mxs_json_pointer(json, MXS_JSON_PTR_ACCOUNT);

    if (!id)
    {
        MXS_ERROR("Request body does not define the '%s' field", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(id))
    {
        MXS_ERROR("The '%s' field is not a string", MXS_JSON_PTR_ID);
    }
    else if (!type)
    {
        MXS_ERROR("Request body does not define the '%s' field", MXS_JSON_PTR_TYPE);
    }
    else if (!json_is_string(type))
    {
        MXS_ERROR("The '%s' field is not a string", MXS_JSON_PTR_TYPE);
    }
    else if (!account)
    {
        MXS_ERROR("Request body does not define the '%s' field", MXS_JSON_PTR_ACCOUNT);
    }
    else if (!json_is_string(account))
    {
        MXS_ERROR("The '%s' field is not a string", MXS_JSON_PTR_ACCOUNT);
    }
    else if (json_to_account_type(account) == mxs::USER_ACCOUNT_UNKNOWN)
    {
        MXS_ERROR("The '%s' field is not a valid account value", MXS_JSON_PTR_ACCOUNT);
    }
    else
    {
        if (strcmp(json_string_value(type), CN_INET) == 0)
        {
            if (!password)
            {
                MXS_ERROR("Request body does not define the '%s' field", MXS_JSON_PTR_PASSWORD);
            }
            else if (!json_is_string(password))
            {
                MXS_ERROR("The '%s' field is not a string", MXS_JSON_PTR_PASSWORD);
            }
            else
            {
                rval = true;
            }
        }
        else if (strcmp(json_string_value(type), CN_UNIX) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Invalid value for field '%s': %s", MXS_JSON_PTR_TYPE, json_string_value(type));
        }
    }

    return rval;
}

bool validate_monitor_json(json_t* json)
{
    bool rval = validate_object_json(json);

    if (rval)
    {
        json_t* params = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);

        for (auto a : {CN_USER, CN_PASSWORD})
        {
            if (!mxs_json_pointer(params, a))
            {
                MXS_ERROR("Mandatory parameter '%s' is not defined", a);
                rval = false;
                break;
            }
        }

        if (!mxs_json_is_type(json, MXS_JSON_PTR_MODULE, JSON_STRING))
        {
            MXS_ERROR("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
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
        if (!mxs_json_is_type(json, MXS_JSON_PTR_MODULE, JSON_STRING))
        {
            MXS_ERROR("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
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
        auto servers = mxs_json_pointer(json, MXS_JSON_PTR_RELATIONSHIPS_SERVERS);
        auto services = mxs_json_pointer(json, MXS_JSON_PTR_RELATIONSHIPS_SERVICES);
        auto monitors = mxs_json_pointer(json, MXS_JSON_PTR_RELATIONSHIPS_MONITORS);

        if (json_array_size(monitors) && (json_array_size(servers) || json_array_size(services)))
        {
            MXS_ERROR("A service must use either servers and services or monitors, not both");
            rval = false;
        }
        else if (!mxs_json_is_type(json, MXS_JSON_PTR_ROUTER, JSON_STRING))
        {
            MXS_ERROR("Field '%s' is not a string", MXS_JSON_PTR_ROUTER);
            rval = false;
        }
    }

    return rval;
}

bool validate_create_service_json(json_t* json)
{
    return validate_service_json(json)
           && mxs_json_pointer(json, MXS_JSON_PTR_ID)
           && mxs_json_pointer(json, MXS_JSON_PTR_ROUTER);
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
        "thread_stack_size",
    };

    return params.count(key);
}

Service* get_service_from_listener_json(json_t* json)
{
    Service* rval = nullptr;
    const char* ptr = "/data/relationships/services/data/0/id";

    if (auto svc = mxs_json_pointer(json, ptr))
    {
        if (json_is_string(svc))
        {
            if (!(rval = Service::find(json_string_value(svc))))
            {
                MXS_ERROR("'%s' is not a valid service in MaxScale", json_string_value(svc));
            }
        }
        else
        {
            MXS_ERROR("Field '%s' is not a string", ptr);
        }
    }
    else
    {
        MXS_ERROR("Field '%s' is not defined", ptr);
    }

    return rval;
}

void prepare_for_destruction(Server* server)
{
    if (auto mon = MonitorManager::server_is_monitored(server))
    {
        runtime_unlink_target(server->name(), mon->name());
    }

    for (auto service : service_server_in_use(server))
    {
        service->remove_target(server);
    }
}

void prepare_for_destruction(const SFilterDef& filter)
{
    for (auto service : service_filter_in_use(filter))
    {
        service->remove_filter(filter);

        // Save the changes in the filters list
        std::ostringstream ss;
        service->persist(ss);
        runtime_save_config(service->name(), ss.str());
    }
}

void prepare_for_destruction(Service* service)
{
    // Destroy listeners that point to the service. They are separate objects and are not managed by the
    // service which means we can't simply ignore them.
    for (const auto& l : listener_find_by_service(service))
    {
        runtime_remove_config(l->name());
        Listener::destroy(l);
    }
}

void prepare_for_destruction(Monitor* monitor)
{
    for (auto svc : service_uses_monitor(monitor))
    {
        runtime_unlink_target(monitor->name(), svc->name());
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

bool can_modify_params(mxs::config::Configuration* cnf, json_t* json)
{
    bool rval = true;
    const char* key;
    json_t* value;

    json_object_foreach(json, key, value)
    {
        if (auto pValue = cnf->find_value(key))
        {
            if (!pValue->parameter().is_modifiable_at_runtime())
            {
                json_t* old_value = pValue->to_json();

                if (!json_equal(old_value, value))
                {
                    MXS_ERROR("%s: Parameter '%s' cannot be modified at runtime",
                              cnf->specification().module().c_str(), key);
                    rval = false;
                }

                json_decref(old_value);
            }
        }
    }

    return rval;
}
}

void config_runtime_add_error(const std::string& error)
{
    runtime_errmsg.push_back(error);
}

json_t* runtime_get_json_error()
{
    json_t* obj = NULL;

    if (!runtime_errmsg.empty())
    {
        obj = mxs_json_error(runtime_errmsg);
        runtime_errmsg.clear();
    }

    return obj;
}

bool runtime_create_volatile_server(const std::string& name, const std::string& address, int port)
{
    bool rval = false;
    if (ServerManager::find_by_unique_name(name) == nullptr)
    {
        mxs::ConfigParameters parameters;
        if (!address.empty())
        {
            auto param_name = address[0] == '/' ? CN_SOCKET : CN_ADDRESS;
            parameters.set(param_name, address);
        }
        parameters.set(CN_PORT, std::to_string(port));

        if (Server* server = ServerManager::create_server(name.c_str(), parameters))
        {
            rval = true;
            MXS_NOTICE("Created server '%s' at %s:%u", server->name(), server->address(), server->port());
        }
        else
        {
            MXS_ERROR("Failed to create server '%s', see error log for more details", name.c_str());
        }
    }
    else
    {
        MXS_ERROR("Server '%s' already exists", name.c_str());
    }

    return rval;
}

bool runtime_destroy_server(Server* server, bool force)
{
    bool rval = false;

    if (force)
    {
        prepare_for_destruction(server);
    }

    if (!service_server_in_use(server).empty() || MonitorManager::server_is_monitored(server))
    {
        MXS_ERROR("Cannot destroy server '%s' as it is used by at least one service or monitor",
                  server->name());
    }
    else if (runtime_remove_config(server->name()))
    {
        MXS_NOTICE("Destroyed server '%s' at %s:%u", server->name(), server->address(), server->port());
        server->deactivate();
        rval = true;
    }

    return rval;
}

bool runtime_destroy_listener(Service* service, const char* name)
{
    bool rval = false;

    if (!service_remove_listener(service, name))
    {
        MXS_ERROR("Failed to destroy listener '%s' for service '%s'", name, service->name());
    }
    else if (runtime_remove_config(name))
    {
        rval = true;
        MXS_NOTICE("Destroyed listener '%s' for service '%s'.", name, service->name());
    }

    return rval;
}

bool runtime_destroy_filter(const SFilterDef& filter, bool force)
{
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
        MXS_ERROR("Filter '%s' cannot be destroyed: Remove it from all services first",
                  filter->name());
    }

    return rval;
}

bool runtime_destroy_service(Service* service, bool force)
{
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
    else
    {
        MXS_ERROR("Service '%s' cannot be destroyed: Remove all servers and destroy all listeners first",
                  service->name());
    }

    return rval;
}

bool runtime_destroy_monitor(Monitor* monitor, bool force)
{
    bool rval = false;

    if (force)
    {
        prepare_for_destruction(monitor);
    }

    if (!monitor->servers().empty() && !force)
    {
        MXS_ERROR("Cannot destroy monitor '%s', it is monitoring servers.", monitor->name());
    }
    else if (!service_uses_monitor(monitor).empty())
    {
        MXS_ERROR("Monitor '%s' cannot be destroyed as it is used by services.", monitor->name());
    }
    else if (runtime_remove_config(monitor->name()))
    {
        MonitorManager::deactivate_monitor(monitor);
        MXS_NOTICE("Destroyed monitor '%s'", monitor->name());
        rval = true;
    }

    return rval;
}

bool runtime_create_server_from_json(json_t* json)
{
    bool rval = false;
    StringSet relations;

    if (server_contains_required_fields(json)
        && extract_relations(json, relations, to_service_rel)
        && extract_relations(json, relations, to_monitor_rel))
    {
        json_t* params = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
        mxs::json_remove_nulls(params);
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        mxb_assert(name);

        if (ServerManager::find_by_unique_name(name))
        {
            MXS_ERROR("Server '%s' already exists", name);
        }
        else if (Server* server = ServerManager::create_server(name, params))
        {
            if (link_target_to_objects(server->name(), relations))
            {
                std::ostringstream ss;
                server->persist(ss);
                rval = runtime_save_config(server->name(), ss.str());
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
    bool rval = false;
    std::unique_ptr<json_t> old_json(ServerManager::server_to_json_resource(server, ""));
    mxb_assert(old_json.get());

    if (is_valid_resource_body(new_json))
    {
        rval = true;
        json_t* new_parameters = nullptr;

        if (json_t* parameters = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            rval = false;
            new_parameters = mxs_json_pointer(old_json.get(), MXS_JSON_PTR_PARAMETERS);
            json_object_update(new_parameters, parameters);
            mxs::json_remove_nulls(new_parameters);

            if (Server::specification().validate(new_parameters))
            {
                auto other = get_server_by_address(new_parameters);

                if (other && other != server)
                {
                    MXS_ERROR("Cannot update server '%s' to '[%s]:%d', server '%s' exists there already.",
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
            if ((rval = server->configure(new_parameters)))
            {
                std::ostringstream ss;
                server->persist(ss);
                rval = runtime_save_config(server->name(), ss.str());

                // Restart the monitor that monitors this server to propagate the configuration changes
                // forward. This causes the monitor to pick up on new timeouts and addresses immediately.
                if (auto mon = MonitorManager::server_is_monitored(server))
                {
                    if (mon->is_running())
                    {
                        mon->stop();
                        mon->start();
                    }
                }
            }
        }
    }

    return rval;
}

bool runtime_alter_server_relationships_from_json(Server* server, const char* type, json_t* json)
{
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
    bool rval = false;

    if (validate_monitor_json(json))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_MODULE));

        if (MonitorManager::find_monitor(name))
        {
            MXS_ERROR("Can't create monitor '%s', it already exists", name);
        }
        else
        {
            mxs::ConfigParameters params;
            bool ok;
            tie(ok, params) = extract_and_validate_params(json, module, mxs::ModuleType::MONITOR, CN_MONITOR);

            if (ok && server_relationship_to_parameter(json, &params))
            {
                if (auto monitor = MonitorManager::create_monitor(name, module, &params))
                {
                    std::ostringstream ss;
                    MonitorManager::monitor_persist(monitor, ss);

                    if (runtime_save_config(monitor->name(), ss.str()))
                    {
                        MXS_NOTICE("Created monitor '%s'", name);
                        MonitorManager::start_monitor(monitor);
                        rval = true;

                        // TODO: Do this with native types instead of JSON comparisons
                        std::unique_ptr<json_t> old_json(monitor->to_json(""));
                        MXB_AT_DEBUG(bool rv = )
                        monitor_to_service_relations(monitor->name(), old_json.get(), json);
                        mxb_assert(rv);
                    }
                }
                else
                {
                    MXS_ERROR("Could not create monitor '%s' with module '%s'", name, module);
                }
            }
        }
    }

    return rval;
}

bool runtime_create_filter_from_json(json_t* json)
{
    bool rval = false;

    if (validate_filter_json(json))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_MODULE));

        if (!filter_find(name))
        {
            mxs::ConfigParameters parameters;
            bool ok;
            tie(ok, parameters) = load_defaults(module, mxs::ModuleType::FILTER, CN_FILTER);
            parameters.set_multiple(extract_parameters(json));

            if (ok)
            {
                if (auto filter = filter_alloc(name, module, &parameters))
                {
                    std::ostringstream ss;
                    filter->persist(ss);

                    if (runtime_save_config(filter->name(), ss.str()))
                    {
                        MXS_NOTICE("Created filter '%s'", name);
                        rval = true;
                    }
                }
            }
        }
        else
        {
            MXS_ERROR("Can't create filter '%s', it already exists", name);
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

bool runtime_create_service_from_json(json_t* json)
{
    bool rval = false;

    if (validate_create_service_json(json))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));

        if (!Service::find(name))
        {
            const char* router = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ROUTER));
            bool ok;
            mxs::ConfigParameters params;
            tie(ok, params) = extract_and_validate_params(json, router, mxs::ModuleType::ROUTER, CN_SERVICE);

            if (ok)
            {
                if (auto service = Service::create(name, router, &params))
                {
                    if (update_service_relationships(service, json))
                    {
                        std::ostringstream ss;
                        service->persist(ss);

                        if (runtime_save_config(name, ss.str()))
                        {
                            MXS_NOTICE("Created service '%s'", name);
                            serviceStart(service);
                            rval = true;
                        }
                        else
                        {
                            MXS_ERROR("Failed to serialize service '%s'", name);
                        }
                    }
                }
                else
                {
                    MXS_ERROR("Could not create service '%s' with module '%s'", name, router);
                }
            }
        }
        else
        {
            MXS_ERROR("Can't create service '%s', it already exists", name);
        }
    }

    return rval;
}

bool runtime_alter_monitor_from_json(Monitor* monitor, json_t* new_json)
{
    bool success = false;
    std::unique_ptr<json_t> old_json(MonitorManager::monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());
    const MXS_MODULE* mod = get_module(monitor->m_module, mxs::ModuleType::MONITOR);

    auto params = monitor->parameters();
    params.set_multiple(extract_parameters(new_json));

    if (is_valid_resource_body(new_json)
        && validate_param(common_monitor_params(), mod->parameters, &params)
        && server_relationship_to_parameter(new_json, &params)
        && monitor_to_service_relations(monitor->name(), old_json.get(), new_json))
    {
        if (MonitorManager::reconfigure_monitor(monitor, params))
        {
            std::ostringstream ss;
            MonitorManager::monitor_persist(monitor, ss);
            success = runtime_save_config(monitor->name(), ss.str());
        }
    }

    return success;
}

bool runtime_alter_monitor_relationships_from_json(Monitor* monitor, const char* type, json_t* json)
{
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

bool can_modify_service_params(Service* service, const mxs::ConfigParameters& params)
{
    bool rval = true;

    const MXS_MODULE* mod = get_module(service->router_name(), mxs::ModuleType::ROUTER);
    StringSet routerparams;

    for (int i = 0; mod->parameters[i].name; i++)
    {
        routerparams.insert(mod->parameters[i].name);
    }

    if (mod->specification)
    {
        for (const auto& p : *mod->specification)
        {
            routerparams.insert(p.second->name());
        }
    }

    // Get new or updated parameters by comparing all given parameters to the ones in the service. This
    // prevents unnecessary modifications of parameters and any log messages that the operations would
    // generate.
    std::vector<std::pair<std::string, std::string>> newparams;
    std::set_difference(params.begin(), params.end(), service->params().begin(), service->params().end(),
                        std::back_inserter(newparams));

    for (const auto& a : newparams)
    {
        if (routerparams.count(a.first))
        {
            if ((service->capabilities() & RCAP_TYPE_RUNTIME_CONFIG) == 0)
            {
                MXS_ERROR("Router '%s' does not support reconfiguration.", service->router_name());
                rval = false;
                break;
            }
        }
        else if (!is_dynamic_param(a.first))
        {
            MXS_ERROR("Runtime modifications to static service parameters is not supported: %s=%s",
                      a.first.c_str(), a.second.c_str());
            rval = false;
        }
    }

    return rval;
}

bool runtime_alter_service_from_json(Service* service, json_t* new_json)
{
    bool rval = validate_service_json(new_json);

    if (rval)
    {
        if (json_t* new_params = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            auto params = service->params();
            params.set_multiple(extract_parameters(new_json));
            const MXS_MODULE* mod = get_module(service->router_name(), mxs::ModuleType::ROUTER);

            bool params_valid = mod->specification ?
                mod->specification->validate(new_params) :
                validate_param(common_service_params(), mod->parameters, &params);

            if (params_valid && can_modify_service_params(service, params))
            {
                service->update_basic_parameters(params);

                if (auto cnf = service->router()->getConfiguration())
                {
                    // Merge the new parameters with the old ones to create a complete definition.
                    json_t* combined_params = mxs::json_merge(cnf->to_json(), new_params);
                    rval = cnf->configure(combined_params);
                    json_decref(combined_params);
                }
                else if (service->capabilities() & RCAP_TYPE_RUNTIME_CONFIG)
                {
                    if (!service->router()->configure(&params))
                    {
                        rval = false;
                        MXS_ERROR("Reconfiguration of service '%s' failed. See log file for more details.",
                                  service->name());
                    }
                }
            }
            else
            {
                rval = false;
            }
        }

        if (rval)
        {
            rval = update_service_relationships(service, new_json);
        }

        if (rval)
        {
            std::ostringstream ss;
            service->persist(ss);
            runtime_save_config(service->name(), ss.str());
        }
    }

    return rval;
}

bool runtime_alter_filter_from_json(const SFilterDef& filter, json_t* new_json)
{
    bool rval = false;

    if (validate_filter_json(new_json))
    {
        if (auto config = filter->configuration())
        {
            if (json_t* new_params = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS))
            {
                // The new parameters are merged with the old parameters to get a complete filter definition.
                json_t* params = mxs::json_merge(config->to_json(), new_params);

                if (config->specification().validate(params)
                    && can_modify_params(config, params)
                    && config->configure(params))
                {
                    std::ostringstream ss;
                    filter->persist(ss);
                    rval = runtime_save_config(filter->name(), ss.str());
                }

                json_decref(params);
            }
            else
            {
                MXS_ERROR("No parameters defined");
            }
        }
        else
        {
            MXS_ERROR("Filter '%s' does not support reconfiguration.", filter->module());
        }
    }

    return rval;
}

bool runtime_create_listener_from_json(json_t* json, Service* service)
{
    bool rval = false;

    if (!service && !(service = get_service_from_listener_json(json)))
    {
        return false;
    }

    if (validate_listener_json(json))
    {
        const char* name = get_string_or_null(json, MXS_JSON_PTR_ID);
        std::string reason;

        if (config_is_valid_name(name, &reason))
        {
            json_t* params = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
            mxs::json_remove_nulls(params);

            // The service is expressed as a relationship instead of a parameter. Add it to the parameters so
            // that it's expressed in the same way regardless of the way the listener is created.
            json_object_set_new(params, "service", json_string(service->name()));

            if (auto listener = Listener::create(name, params))
            {
                std::ostringstream ss;
                listener->persist(ss);

                if (runtime_save_config(listener->name(), ss.str()) && listener->listen())
                {
                    MXS_NOTICE("Created listener '%s' at %s:%u for service '%s'",
                               name, listener->address(), listener->port(), service->name());

                    rval = true;
                }
                else
                {
                    MXS_ERROR("Listener '%s' was created but failed to start it.", name);
                    Listener::destroy(listener);
                    mxb_assert(!listener_find(name));
                }
            }
        }
        else
        {
            MXS_ERROR("%s", reason.c_str());
        }
    }

    return rval;
}

bool runtime_alter_listener_from_json(SListener listener, json_t* new_json)
{
    bool rval = false;

    if (validate_service_json(new_json))
    {
        if (json_t* new_params = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS))
        {
            auto* cnf = listener->config();
            json_t* combined_params = mxs::json_merge(cnf->to_json(), new_params);

            if (cnf->specification().validate(combined_params) && cnf->configure(combined_params))
            {
                // TODO: Configure the protocol module as well

                std::ostringstream ss;
                listener->persist(ss);
                rval = runtime_save_config(listener->name(), ss.str());
            }

            json_decref(combined_params);
        }
    }

    return rval;
}

bool runtime_create_user_from_json(json_t* json)
{
    bool rval = false;

    if (validate_user_json(json))
    {
        const char* user = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* password = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_PASSWORD));
        std::string strtype = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_TYPE));
        auto type = json_to_account_type(mxs_json_pointer(json, MXS_JSON_PTR_ACCOUNT));
        const char* err = NULL;

        if (strtype == CN_INET && (err = admin_add_inet_user(user, password, type)) == ADMIN_SUCCESS)
        {
            MXS_NOTICE("Create network user '%s'", user);
            rval = true;
        }
        else if (strtype == CN_UNIX)
        {
            MXS_ERROR("UNIX users are no longer supported.");
        }
        else if (err)
        {
            MXS_ERROR("Failed to add user '%s': %s", user, err);
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
        MXS_NOTICE("Deleted network user '%s'", id);
        rval = true;
    }
    else
    {
        MXS_ERROR("Failed to remove user '%s': %s", id, err);
    }

    return rval;
}

bool runtime_alter_user(const std::string& user, const std::string& type, json_t* json)
{
    bool rval = false;
    const char* password = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_PASSWORD));

    if (!password)
    {
        MXS_ERROR("No password provided");
    }
    else if (type != CN_INET)
    {
        MXS_ERROR("Users of type '%s' are not supported", type.c_str());
    }
    else if (const char* err = admin_alter_inet_user(user.c_str(), password))
    {
        MXS_ERROR("%s", err);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool runtime_alter_maxscale_from_json(json_t* json)
{
    bool rval = false;

    if (validate_object_json(json))
    {
        json_t* params = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
        json_t* new_params = mxs::json_merge(mxs::Config::get().to_json(), params);
        auto& cfg = mxs::Config::get();

        // TODO: Don't strip out these parameters and define them in the core specification instead.
        const char* key;
        json_t* value;
        void* ptr;

        json_object_foreach_safe(new_params, ptr, key, value)
        {
            if (ignored_core_parameters(key))
            {
                json_object_del(new_params, key);
            }
        }

        if (cfg.specification().validate(new_params) && cfg.configure(new_params))
        {
            std::ostringstream ss;
            mxs::Config::get().persist(ss);
            rval = runtime_save_config("maxscale", ss.str());
        }

        json_decref(new_params);
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
        int wid_to = -1;

        if (!recipient.empty() && mxb::get_int(recipient, &wid_to))
        {
            mxs::RoutingWorker* pTo = mxs::RoutingWorker::get(wid_to);

            if (pTo)
            {
                from.rebalance(pTo, nSessions);
                rv = true;
            }
            else
            {
                MXS_ERROR("The 'recipient' value '%s' does not refer to a worker.", recipient.c_str());
            }
        }
        else
        {
            MXS_ERROR("'recipient' argument not provided, or value is not a valid integer.");
        }
    }
    else
    {
        MXS_ERROR("'sessions' argument provided, but value '%s' is not a valid integer.", sessions.c_str());
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
            MXS_ERROR("%s", message.c_str());
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

bool runtime_remove_config(const char* name)
{
    bool rval = true;
    std::string filename = mxs::config_persistdir() + "/"s + name + ".cnf";

    if (unlink(filename.c_str()) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove persisted configuration '%s': %d, %s",
                  filename.c_str(), errno, mxs_strerror(errno));
        rval = false;
    }

    return rval;
}

bool runtime_save_config(const char* name, const std::string& config)
{
    bool rval = false;
    std::string filename = mxs::config_persistdir() + "/"s + name + ".cnf.tmp";

    if (unlink(filename.c_str()) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary configuration at '%s': %d, %s",
                  filename.c_str(), errno, mxs_strerror(errno));
        return false;
    }

    int fd = open(filename.c_str(), O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (fd == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing '%s': %d, %s",
                  filename.c_str(), name, errno, mxs_strerror(errno));
        return false;
    }

    if (write(fd, config.c_str(), config.size()) == -1)
    {
        MXS_ERROR("Failed to serialize file '%s': %d, %s", filename.c_str(), errno, mxs_strerror(errno));
    }
    else
    {
        // Remove the .tmp suffix
        auto final_filename = filename.substr(0, filename.size() - 4);

        if (rename(filename.c_str(), final_filename.c_str()) == -1)
        {
            MXS_ERROR("Failed to rename temporary configuration at '%s': %d, %s",
                      filename.c_str(), errno, mxs_strerror(errno));
        }
        else
        {
            rval = true;
        }
    }

    close(fd);

    return rval;
}
