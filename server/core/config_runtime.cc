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

#include <maxscale/ccdefs.hh>

#include "internal/config_runtime.hh"

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <strings.h>
#include <tuple>
#include <vector>

#include <maxbase/atomic.h>
#include <maxscale/clock.h>
#include <maxscale/jansson.hh>
#include <maxscale/json_api.hh>
#include <maxscale/paths.h>
#include <maxscale/router.hh>
#include <maxscale/users.hh>

#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/listener.hh"
#include "internal/modules.hh"
#include "internal/monitor.hh"
#include "internal/monitormanager.hh"
#include "internal/query_classifier.hh"
#include "internal/servermanager.hh"

typedef std::set<std::string>    StringSet;
typedef std::vector<std::string> StringVector;

using std::tie;
using maxscale::Monitor;
using SListener = std::shared_ptr<Listener>;

#define RUNTIME_ERRMSG_BUFSIZE 512
thread_local std::vector<std::string> runtime_errmsg;

typedef std::function<bool (const std::string&, const std::string&)> JsonValidator;
typedef std::pair<const char*, JsonValidator>                        Relationship;

namespace
{

bool object_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVERS && ServerManager::find_by_unique_name(value);
}

bool service_to_service_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVICES && Service::find(value);
}

bool server_to_service_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVICES && Service::find(value.c_str());
}

bool server_to_monitor_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_MONITORS && MonitorManager::find_monitor(value.c_str());
}

bool filter_to_service_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVICES && Service::find(value.c_str());
}

bool service_to_filter_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_FILTERS && filter_find(value.c_str());
}

//
// Constants for relationship validation
//
const Relationship object_to_server
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVERS,
    object_relation_is_valid
};

const Relationship server_to_service
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVICES,
    server_to_service_relation_is_valid
};

const Relationship server_to_monitor
{
    MXS_JSON_PTR_RELATIONSHIPS_MONITORS,
    server_to_monitor_relation_is_valid
};

const Relationship filter_to_service
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVICES,
    filter_to_service_relation_is_valid
};

const Relationship service_to_filter
{
    MXS_JSON_PTR_RELATIONSHIPS_FILTERS,
    service_to_filter_relation_is_valid
};

const Relationship service_to_service
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVICES,
    service_to_service_relation_is_valid
};

const MXS_MODULE_PARAM* get_type_parameters(const char* type)
{
    if (strcmp(type, CN_SERVICE) == 0)
    {
        return common_service_params();
    }
    else if (strcmp(type, CN_LISTENER) == 0)
    {
        return common_listener_params();
    }
    else if (strcmp(type, CN_MONITOR) == 0)
    {
        return common_monitor_params();
    }
    else if (strcmp(type, CN_FILTER) == 0)
    {
        return config_filter_params;
    }
    else if (strcmp(type, CN_SERVER) == 0)
    {
        return common_server_params();
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
 * @param module_type Type of the module (MODULE_ROUTER, MODULE_PROTOCOL etc.)
 * @param object_type Type of the object (server, service, listener etc.)
 *
 * @return Whether loading succeeded and the list of default parameters
 */
std::pair<bool, mxs::ConfigParameters> load_defaults(const char* name,
                                                     const char* module_type,
                                                     const char* object_type)
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
        config_runtime_error("Failed to load module '%s': %s",
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

bool runtime_link_target(const std::string& subject, const std::string& target)
{
    bool rval = false;

    if (auto service = Service::find(target))
    {
        if (service->uses_cluster())
        {
            config_runtime_error("The servers of the service '%s' are defined by the monitor '%s'. "
                                 "Servers cannot explicitly be added to the service.",
                                 service->name(), service->m_monitor->name());
        }
        else if (auto tgt = mxs::Target::find(subject))
        {
            if (service == tgt)
            {
                config_runtime_error("Cannot link '%s' to itself", service->name());
            }
            else if (service->has_target(tgt))
            {
                config_runtime_error("Service '%s' already uses target '%s'",
                                     service->name(), subject.c_str());
            }
            else
            {
                auto cycle = get_cycle_name(service, tgt);

                if (!cycle.empty())
                {
                    config_runtime_error("Linking '%s' to '%s' would result in a circular configuration: %s",
                                         subject.c_str(), service->name(), cycle.c_str());
                }
                else
                {
                    service->add_target(tgt);
                    service->serialize();
                    rval = true;
                }
            }
        }
        else
        {
            config_runtime_error("Could not find target with name '%s'", subject.c_str());
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
                config_runtime_error("%s", error_msg.c_str());
            }
        }
        else
        {
            config_runtime_error("No server named '%s' found", subject.c_str());
        }
    }
    else
    {
        config_runtime_error("No monitor or service named '%s' found", target.c_str());
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
        if (service->uses_cluster())
        {
            config_runtime_error("The servers of the service '%s' are defined by the monitor '%s'. "
                                 "Servers cannot explicitly be removed from the service.",
                                 service->name(), service->m_monitor->name());
        }
        else if (auto tgt = mxs::Target::find(subject))
        {

            service->remove_target(tgt);
            service->serialize();
            rval = true;
        }
        else
        {
            config_runtime_error("Target '%s' not found", subject.c_str());
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
                config_runtime_error("%s", error_msg.c_str());
            }
        }
        else
        {
            config_runtime_error("No server named '%s' found", subject.c_str());
        }
    }
    else
    {
        config_runtime_error("No monitor or service named '%s' found", target.c_str());
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

bool runtime_alter_server(Server* server, const char* key, const char* value)
{
    if (!value[0])
    {
        config_runtime_error("Empty value for parameter: %s", key);
        return false;
    }

    bool is_normal_parameter = !server->is_custom_parameter(key);

    // Only validate known parameters as custom parameters cannot be validated.
    if (is_normal_parameter)
    {
        if (!param_is_valid(common_server_params(), nullptr, key, value))
        {
            config_runtime_error("Invalid value for parameter '%s': %s", key, value);
            return false;
        }
    }

    bool setting_changed = false;
    if (is_normal_parameter)
    {
        // Only some normal parameters can be changed runtime. The key/value-combination has already
        // been checked to be valid.
        if (strcmp(key, CN_ADDRESS) == 0 || strcmp(key, CN_SOCKET) == 0)
        {
            server->server_update_address(value);
            setting_changed = true;
        }
        else if (strcmp(key, CN_PORT) == 0)
        {
            if (int ival = get_positive_int(value))
            {
                server->update_port(ival);
                setting_changed = true;
            }
        }
        else if (strcmp(key, CN_EXTRA_PORT) == 0)
        {
            server->update_extra_port(atoi(value));
            setting_changed = true;
        }
        else if (strcmp(key, CN_MONITORUSER) == 0)
        {
            server->set_monitor_user(value);
            setting_changed = true;
        }
        else if (strcmp(key, CN_MONITORPW) == 0)
        {
            server->set_monitor_password(value);
            setting_changed = true;
        }
        else if (strcmp(key, CN_PERSISTPOOLMAX) == 0)
        {
            if (is_valid_integer(value))
            {
                server->set_persistpoolmax(atoi(value));
                setting_changed = true;
            }
        }
        else if (strcmp(key, CN_PERSISTMAXTIME) == 0)
        {
            if (is_valid_integer(value))
            {
                server->set_persistmaxtime(atoi(value));
                setting_changed = true;
            }
        }
        else if (strcmp(key, CN_RANK) == 0)
        {
            auto v = config_enum_to_value(value, rank_values);
            if (v != MXS_UNKNOWN_ENUM_VALUE)
            {
                server->set_rank(v);
                setting_changed = true;
            }
            else
            {
                config_runtime_error("Invalid value for '%s': %s", CN_RANK, value);
            }
        }
        else
        {
            // Was a recognized parameter but runtime modification is not supported.
            config_runtime_error("Server parameter '%s' cannot be modified during runtime. A similar "
                                 "effect can be produced by destroying the server and recreating it "
                                 "with the new settings.", key);
        }

        if (setting_changed)
        {
            // Successful modification of a normal parameter, write to text storage.
            server->set_normal_parameter(key, value);
        }
    }
    else
    {
        // This is a custom parameter and may be used for weighting. Update the weights of services.
        server->set_custom_parameter(key, value);
        setting_changed = true;
    }

    if (setting_changed)
    {
        server->serialize();
        MXS_NOTICE("Updated server '%s': %s=%s", server->name(), key, value);
    }
    return setting_changed;
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
            config_runtime_error("Mandatory parameter '%s' is not defined.", mod_params[i].name);
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
        config_runtime_error("%s", error.c_str());
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

// Helper for runtime_create_listener
void set_if_not_null(mxs::ConfigParameters& params, const char* name,
                     const char* value, const char* dflt = nullptr)
{
    if ((!value || strcasecmp(value, CN_DEFAULT) == 0) && dflt)
    {
        params.set(name, dflt);
    }
    else if (value)
    {
        params.set(name, value);
    }
}

bool runtime_create_listener(Service* service,
                             const char* name,
                             const char* addr,
                             const char* port,
                             const char* proto,
                             const char* auth,
                             const char* auth_opt,
                             const char* ssl_key,
                             const char* ssl_cert,
                             const char* ssl_ca,
                             const char* ssl_version,
                             const char* ssl_depth,
                             const char* verify_ssl)
{
    if (proto == NULL || strcasecmp(proto, CN_DEFAULT) == 0)
    {
        proto = "mariadbclient";
    }

    if (auth && strcasecmp(auth, CN_DEFAULT) == 0)
    {
        // The protocol default authenticator is used
        auth = nullptr;
    }
    if (auth_opt && strcasecmp(auth_opt, CN_DEFAULT) == 0)
    {
        auth_opt = nullptr;
    }

    mxs::ConfigParameters params;
    bool ok;
    tie(ok, params) = load_defaults(proto, MODULE_PROTOCOL, CN_LISTENER);
    params.set(CN_SERVICE, service->name());
    set_if_not_null(params, CN_AUTHENTICATOR, auth);
    set_if_not_null(params, CN_AUTHENTICATOR_OPTIONS, auth_opt);
    bool use_socket = (addr && *addr == '/');

    if (use_socket)
    {
        params.set(CN_SOCKET, addr);
    }
    else
    {
        set_if_not_null(params, CN_PORT, port, "3306");
        set_if_not_null(params, CN_ADDRESS, addr, "::");
    }

    if (ssl_key || ssl_cert || ssl_ca)
    {
        params.set(CN_SSL, CN_REQUIRED);
        set_if_not_null(params, CN_SSL_KEY, ssl_key);
        set_if_not_null(params, CN_SSL_CERT, ssl_cert);
        set_if_not_null(params, CN_SSL_CA_CERT, ssl_ca);
        set_if_not_null(params, CN_SSL_VERSION, ssl_version);
        set_if_not_null(params, CN_SSL_CERT_VERIFY_DEPTH, ssl_depth);
        set_if_not_null(params, CN_SSL_VERIFY_PEER_CERTIFICATE, verify_ssl);
    }

    unsigned short u_port = atoi(port);
    bool rval = false;

    std::string reason;

    SListener old_listener = use_socket ?
        listener_find_by_address(params.get_string(CN_ADDRESS), params.get_integer(CN_PORT)) :
        listener_find_by_socket(params.get_string(CN_SOCKET));

    if (!ok)
    {
        config_runtime_error("Failed to load module '%s'", proto);
    }
    else if (listener_find(name))
    {
        config_runtime_error("Listener '%s' already exists", name);
    }
    else if (old_listener)
    {
        config_runtime_error("Listener '%s' already listens on [%s]:%u", old_listener->name(),
                             old_listener->address(), old_listener->port());
    }
    else if (config_is_valid_name(name, &reason))
    {
        auto listener = Listener::create(name, proto, params);

        if (listener && listener_serialize(listener))
        {
            if (listener->listen())
            {
                MXS_NOTICE("Created listener '%s' at %s:%u for service '%s'",
                           name, listener->address(), listener->port(), service->name());

                rval = true;
            }
            else
            {
                config_runtime_error("Listener '%s' was created but failed to start it.", name);
                Listener::destroy(listener);
                mxb_assert(!listener_find(name));
            }
        }
        else
        {
            config_runtime_error("Failed to create listener '%s'. Read the MaxScale error log "
                                 "for more details.", name);
        }
    }
    else
    {
        config_runtime_error("%s", reason.c_str());
    }

    return rval;
}

bool runtime_create_filter(const char* name, const char* module, mxs::ConfigParameters* params)
{
    bool rval = false;

    if (!filter_find(name))
    {
        SFilterDef filter;
        mxs::ConfigParameters parameters;
        bool ok;
        tie(ok, parameters) = load_defaults(module, MODULE_FILTER, CN_FILTER);

        if (ok)
        {
            if (params)
            {
                parameters.set_multiple(*params);
            }

            if (!(filter = filter_alloc(name, module, &parameters)))
            {
                config_runtime_error("Could not create filter '%s' with module '%s'", name, module);
            }
        }

        if (filter)
        {
            if (filter_serialize(filter))
            {
                MXS_NOTICE("Created filter '%s'", name);
                rval = true;
            }
            else
            {
                config_runtime_error("Failed to serialize filter '%s'", name);
            }
        }
    }
    else
    {
        config_runtime_error("Can't create filter '%s', it already exists", name);
    }

    return rval;
}

mxs::ConfigParameters extract_parameters(json_t* json)
{
    mxs::ConfigParameters rval;

    if (json_t* parameters = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        const char* key;
        json_t* value;

        json_object_foreach(parameters, key, value)
        {
            if (!json_is_null(value) && !json_is_array(value) && !json_is_object(value))
            {
                auto strval = mxs::json_to_string(value);

                if (!strval.empty())
                {
                    rval.set(key, strval);
                }
                else
                {
                    mxb_assert_message(json_is_string(value), "Only strings can be empty (%s)", key);
                }
            }
        }
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

    if (value && !json_is_string(value))
    {
        config_runtime_error("Parameter '%s' is not a string but %s", path, json_type_to_string(value));
        rval = false;
    }

    return rval;
}

bool runtime_is_bool_or_null(json_t* json, const char* path)
{
    bool rval = true;
    json_t* value = mxs_json_pointer(json, path);

    if (value && !json_is_boolean(value))
    {
        config_runtime_error("Parameter '%s' is not a boolean but %s", path, json_type_to_string(value));
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
        if (!json_is_integer(value) && !json_is_string(value))
        {
            config_runtime_error("Parameter '%s' is not an integer or a string but %s",
                                 path,
                                 json_type_to_string(value));
            rval = false;
        }
        else if ((json_is_integer(value) && json_integer_value(value) < 0)
                 || (json_is_string(value) && !get_suffixed_size(json_string_value(value), nullptr)))
        {
            config_runtime_error("Parameter '%s' is not a valid size", path);
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
        if (!json_is_integer(value))
        {
            config_runtime_error("Parameter '%s' is not an integer but %s", path, json_type_to_string(value));
            rval = false;
        }
        else if (json_integer_value(value) < 0)
        {
            config_runtime_error("Parameter '%s' is a negative integer", path);
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
        config_runtime_error("No '%s' field defined", MXS_JSON_PTR_DATA);
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
                config_runtime_error("Relationship '%s' is not an object", *it);
                rval = false;
            }
        }
    }

    return rval;
}

bool server_alter_fields_are_valid(json_t* json)
{
    bool rval = false;
    json_t* address = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_ADDRESS);
    json_t* socket = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_SOCKET);
    json_t* port = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_PORT);

    if (address && socket)
    {
        config_runtime_error("Request body defines both of the '%s' and '%s' fields",
                             MXS_JSON_PTR_PARAM_ADDRESS, MXS_JSON_PTR_PARAM_SOCKET);
    }
    else if (address && !json_is_string(address))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (address && json_is_string(address) && json_string_value(address)[0] == '/')
    {
        config_runtime_error("The '%s' field is not a valid address", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (socket && !json_is_string(socket))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_SOCKET);
    }
    else if (port && !json_is_integer(port))
    {
        config_runtime_error("The '%s' field is not an integer", MXS_JSON_PTR_PARAM_PORT);
    }
    else if (socket && !json_is_string(socket))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_SOCKET);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool server_contains_required_fields(json_t* json)
{
    json_t* id = mxs_json_pointer(json, MXS_JSON_PTR_ID);
    json_t* port = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_PORT);
    json_t* address = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_ADDRESS);
    json_t* socket = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_SOCKET);
    bool rval = false;

    if (!id)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(id))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_ID);
    }
    else if (!address && !socket)
    {
        config_runtime_error("Request body does not define '%s' or '%s'",
                             MXS_JSON_PTR_PARAM_ADDRESS, MXS_JSON_PTR_PARAM_SOCKET);
    }
    else if (address && socket)
    {
        config_runtime_error("Request body defines both of the '%s' and '%s' fields",
                             MXS_JSON_PTR_PARAM_ADDRESS, MXS_JSON_PTR_PARAM_SOCKET);
    }
    else if (address && !json_is_string(address))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (address && json_is_string(address) && json_string_value(address)[0] == '/')
    {
        config_runtime_error("The '%s' field is not a valid address", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (socket && !json_is_string(socket))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_SOCKET);
    }
    else if (!address && port)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_PARAM_PORT);
    }
    else if (port && !json_is_integer(port))
    {
        config_runtime_error("The '%s' field is not an integer", MXS_JSON_PTR_PARAM_PORT);
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
            config_runtime_error("SSL configuration for listeners requires "
                                 "'%s', '%s' and '%s' parameters",
                                 CN_SSL_KEY,
                                 CN_SSL_CERT,
                                 CN_SSL_CA_CERT);
            rval = false;
        }
        else if (type == OT_SERVER)
        {
            if (!ca_cert)
            {
                config_runtime_error("SSL configuration for servers requires "
                                     "at least the '%s' parameter",
                                     CN_SSL_CA_CERT);
                rval = false;
            }
            else if ((key == nullptr) != (cert == nullptr))
            {
                config_runtime_error("Both '%s' and '%s' must be defined", CN_SSL_KEY, CN_SSL_CERT);
                rval = false;
            }
        }

        json_t* ssl_version = mxs_json_pointer(params, CN_SSL_VERSION);
        const char* ssl_version_str = ssl_version ? json_string_value(ssl_version) : NULL;

        if (ssl_version_str && string_to_ssl_method_type(ssl_version_str) == SERVICE_SSL_UNKNOWN)
        {
            config_runtime_error("Invalid value for '%s': %s", CN_SSL_VERSION, ssl_version_str);
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

    for (const auto& a : {server_to_service, server_to_monitor})
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
        config_runtime_error("Field '%s' is not defined", MXS_JSON_PTR_DATA);
        rval = false;
    }
    else if (!json_is_array(obj) && !json_is_null(obj))
    {
        config_runtime_error("Field '%s' is not an array", MXS_JSON_PTR_DATA);
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
        config_runtime_error("%s", err.c_str());
    }

    return err.empty();
}

bool server_relationship_to_parameter(json_t* json, mxs::ConfigParameters* params)
{
    StringSet relations;
    bool rval = false;

    if (extract_relations(json, relations, object_to_server))
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

std::pair<bool, mxs::ConfigParameters> extract_and_validate_params(json_t* json,
                                                                   const char* module,
                                                                   const char* module_type,
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
    }
    else
    {
        config_runtime_error("Unknown module: %s", module);
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
    bool rval = update_object_relations(target, object_to_server, old_json, new_json);

    if (!rval)
    {
        config_runtime_error("Could not find all servers that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_service_relations(const std::string& target, json_t* old_json, json_t* new_json)
{
    bool rval = update_object_relations(target, service_to_service, old_json, new_json);

    if (!rval)
    {
        config_runtime_error("Could not find all services that '%s' relates to", target.c_str());
    }

    return rval;
}

bool service_to_filter_relations(Service* service, json_t* old_json, json_t* new_json)
{
    if (mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS) == NULL)
    {
        // No relationships defined, nothing to change
        return true;
    }

    bool rval = false;
    StringVector old_relations;
    StringVector new_relations;
    const char* filter_relation = MXS_JSON_PTR_RELATIONSHIPS_FILTERS;

    if (extract_ordered_relations(old_json, old_relations, service_to_filter)
        && extract_ordered_relations(new_json, new_relations, service_to_filter))
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

        config_runtime_error("Could not find all filters that '%s' relates to", service->name());
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
           && key != CN_SERVERS;
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
        config_runtime_error("Value not found: '%s'", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(param))
    {
        config_runtime_error("Value '%s' is not a string", MXS_JSON_PTR_ID);
    }
    else if (!(param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS)))
    {
        config_runtime_error("Value not found: '%s'", MXS_JSON_PTR_PARAMETERS);
    }
    else if (!json_is_object(param))
    {
        config_runtime_error("Value '%s' is not an object", MXS_JSON_PTR_PARAMETERS);
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
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(id))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_ID);
    }
    else if (!type)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_TYPE);
    }
    else if (!json_is_string(type))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_TYPE);
    }
    else if (!account)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_ACCOUNT);
    }
    else if (!json_is_string(account))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_ACCOUNT);
    }
    else if (json_to_account_type(account) == mxs::USER_ACCOUNT_UNKNOWN)
    {
        config_runtime_error("The '%s' field is not a valid account value", MXS_JSON_PTR_ACCOUNT);
    }
    else
    {
        if (strcmp(json_string_value(type), CN_INET) == 0)
        {
            if (!password)
            {
                config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_PASSWORD);
            }
            else if (!json_is_string(password))
            {
                config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PASSWORD);
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
            config_runtime_error("Invalid value for field '%s': %s",
                                 MXS_JSON_PTR_TYPE,
                                 json_string_value(type));
        }
    }

    return rval;
}

bool validate_maxscale_json(json_t* json)
{
    bool rval = false;
    json_t* param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);

    if (param)
    {
        rval = runtime_is_count_or_null(param, CN_AUTH_CONNECT_TIMEOUT)
            && runtime_is_count_or_null(param, CN_AUTH_READ_TIMEOUT)
            && runtime_is_count_or_null(param, CN_AUTH_WRITE_TIMEOUT)
            && runtime_is_bool_or_null(param, CN_ADMIN_AUTH)
            && runtime_is_bool_or_null(param, CN_ADMIN_LOG_AUTH_FAILURES)
            && runtime_is_size_or_null(param, CN_QUERY_CLASSIFIER_CACHE_SIZE)
            && runtime_is_count_or_null(param, CN_REBALANCE_THRESHOLD);
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
                config_runtime_error("Mandatory parameter '%s' is not defined", a);
                rval = false;
                break;
            }
        }

        if (!mxs_json_is_type(json, MXS_JSON_PTR_MODULE, JSON_STRING))
        {
            config_runtime_error("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
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
            config_runtime_error("Field '%s' is not a string", MXS_JSON_PTR_MODULE);
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
            config_runtime_error("A service must use either servers and services or monitors, not both");
            rval = false;
        }
        else if (!mxs_json_is_type(json, MXS_JSON_PTR_ROUTER, JSON_STRING))
        {
            config_runtime_error("Field '%s' is not a string", MXS_JSON_PTR_ROUTER);
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
        "libdir",
        "datadir",
        "process_datadir",
        "cachedir",
        "configdir",
        "config_persistdir",
        "module_configdir",
        "piddir",
        "logdir",
        "langdir",
        "execdir",
        "connector_plugindir",
        "thread_stack_size",
    };

    return params.count(key);
}
}

void config_runtime_error(const char* fmt, ...)
{
    char errmsg[RUNTIME_ERRMSG_BUFSIZE];
    va_list list;
    va_start(list, fmt);
    vsnprintf(errmsg, sizeof(errmsg), fmt, list);
    va_end(list);
    runtime_errmsg.push_back(errmsg);
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

bool runtime_create_server(const char* name, const char* address, const char* port, bool external)
{
    bool rval = false;

    if (ServerManager::find_by_unique_name(name) == NULL)
    {
        std::string reason;
        if (!external || config_is_valid_name(name, &reason))
        {
            mxs::ConfigParameters parameters;
            config_add_defaults(&parameters, common_server_params());
            if (address)
            {
                auto param_name = *address == '/' ? CN_SOCKET : CN_ADDRESS;
                parameters.set(param_name, address);
            }
            if (port)
            {
                parameters.set(CN_PORT, port);
            }

            Server* server = ServerManager::create_server(name, parameters);

            if (server && (!external || server->serialize()))
            {
                rval = true;
                MXS_NOTICE("Created server '%s' at %s:%u",
                           server->name(),
                           server->address,
                           server->port);
            }
            else
            {
                config_runtime_error("Failed to create server '%s', see error log for more details",
                                     name);
            }
        }
        else
        {
            config_runtime_error("%s", reason.c_str());
        }
    }
    else
    {
        config_runtime_error("Server '%s' already exists", name);
    }

    return rval;
}

bool runtime_destroy_server(Server* server)
{
    bool rval = false;

    if (service_server_in_use(server) || MonitorManager::server_is_monitored(server))
    {
        const char* err = "Cannot destroy server '%s' as it is used by at least "
                          "one service or monitor";
        config_runtime_error(err, server->name());
        MXS_ERROR(err, server->name());
    }
    else
    {
        char filename[PATH_MAX];
        snprintf(filename,
                 sizeof(filename),
                 "%s/%s.cnf",
                 get_config_persistdir(),
                 server->name());

        if (unlink(filename) == -1)
        {
            if (errno != ENOENT)
            {
                MXS_ERROR("Failed to remove persisted server configuration '%s': %d, %s",
                          filename,
                          errno,
                          mxs_strerror(errno));
            }
            else
            {
                rval = true;
                MXS_WARNING("Server '%s' was not created at runtime. Remove the "
                            "server manually from the correct configuration file.",
                            server->name());
            }
        }
        else
        {
            rval = true;
        }

        if (rval)
        {
            MXS_NOTICE("Destroyed server '%s' at %s:%u",
                       server->name(),
                       server->address,
                       server->port);
            server->is_active = false;
        }
    }

    return rval;
}

bool runtime_destroy_listener(Service* service, const char* name)
{
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), name);


    if (unlink(filename) == -1)
    {
        if (errno != ENOENT)
        {
            MXS_ERROR("Failed to remove persisted listener configuration '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
            return false;
        }
        else
        {
            MXS_WARNING("Persisted configuration file for listener '%s' was not found. This means that the "
                        "listener was not created at runtime. Remove the listener manually from the correct "
                        "configuration file to permanently destroy the listener.", name);
        }
    }

    bool rval = false;

    if (!service_remove_listener(service, name))
    {
        MXS_ERROR("Failed to destroy listener '%s' for service '%s'", name, service->name());
        config_runtime_error("Failed to destroy listener '%s' for service '%s'", name, service->name());
    }
    else
    {
        rval = true;
        MXS_NOTICE("Destroyed listener '%s' for service '%s'. The listener "
                   "will be removed after the next restart of MaxScale or "
                   "when the associated service is destroyed.",
                   name,
                   service->name());
    }

    return rval;
}

bool runtime_destroy_filter(const SFilterDef& filter)
{
    mxb_assert(filter);
    bool rval = false;

    if (filter_can_be_destroyed(filter))
    {
        filter_destroy(filter);
        rval = true;
    }
    else
    {
        config_runtime_error("Filter '%s' cannot be destroyed: Remove it from all services "
                             "first",
                             filter->name.c_str());
    }

    return rval;
}

bool runtime_destroy_service(Service* service)
{
    bool rval = false;
    mxb_assert(service && service->active());

    if (service->can_be_destroyed())
    {
        Service::destroy(service);
        rval = true;
    }
    else
    {
        config_runtime_error("Service '%s' cannot be destroyed: Remove all servers and "
                             "destroy all listeners first",
                             service->name());
    }

    return rval;
}

bool runtime_destroy_monitor(Monitor* monitor)
{
    bool rval = false;

    if (Service* s = service_uses_monitor(monitor))
    {
        config_runtime_error("Monitor '%s' cannot be destroyed as it is used by service '%s'",
                             monitor->name(), s->name());
    }
    else
    {
        char filename[PATH_MAX];
        snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), monitor->name());

        if (unlink(filename) == -1 && errno != ENOENT)
        {
            MXS_ERROR("Failed to remove persisted monitor configuration '%s': %d, %s",
                      filename,
                      errno,
                      mxs_strerror(errno));
        }
        else
        {
            rval = true;
        }
    }

    if (rval)
    {
        MonitorManager::deactivate_monitor(monitor);
        MXS_NOTICE("Destroyed monitor '%s'", monitor->name());
    }

    return rval;
}

bool runtime_create_server_from_json(json_t* json)
{
    bool rval = false;
    StringSet relations;

    if (is_valid_resource_body(json) && server_contains_required_fields(json)
        && extract_relations(json, relations, server_to_service)
        && extract_relations(json, relations, server_to_monitor))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        mxb_assert(name);

        if (ServerManager::find_by_unique_name(name))
        {
            config_runtime_error("Server '%s' already exists", name);
        }
        else
        {
            mxs::ConfigParameters params;
            config_add_defaults(&params, common_server_params());
            params.set_multiple(extract_parameters(json));

            if (params.contains_any({CN_SSL_KEY, CN_SSL_CERT, CN_SSL_CA_CERT}))
            {
                params.set(CN_SSL, "true");
            }

            if (Server* server = ServerManager::create_server(name, params))
            {
                if (link_target_to_objects(server->name(), relations) && server->serialize())
                {
                    rval = true;
                }
                else
                {
                    runtime_destroy_server(server);
                }
            }

            if (!rval)
            {
                config_runtime_error("Failed to create server '%s', see error log for more details", name);
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

    if (is_valid_resource_body(new_json)
        && server_to_object_relations(server, old_json.get(), new_json)
        && server_alter_fields_are_valid(new_json))
    {
        rval = true;
        json_t* parameters = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS);
        json_t* old_parameters = mxs_json_pointer(old_json.get(), MXS_JSON_PTR_PARAMETERS);

        mxb_assert(old_parameters);

        if (parameters)
        {
            const char* key;
            json_t* value;

            json_object_foreach(parameters, key, value)
            {
                json_t* new_val = json_object_get(parameters, key);
                json_t* old_val = json_object_get(old_parameters, key);

                if (old_val && new_val && mxs::json_to_string(new_val) == mxs::json_to_string(old_val))
                {
                    /** No change in values */
                }
                else if (!runtime_alter_server(server, key, mxs::json_to_string(value).c_str()))
                {
                    rval = false;
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
            config_runtime_error("Can't create monitor '%s', it already exists", name);
        }
        else
        {
            mxs::ConfigParameters params;
            bool ok;
            tie(ok, params) = extract_and_validate_params(json, module, MODULE_MONITOR, CN_MONITOR);

            if (ok && server_relationship_to_parameter(json, &params))
            {
                Monitor* monitor = MonitorManager::create_monitor(name, module, &params);

                if (!monitor)
                {
                    config_runtime_error("Could not create monitor '%s' with module '%s'", name, module);
                }
                else if (!MonitorManager::monitor_serialize(monitor))
                {
                    config_runtime_error("Failed to serialize monitor '%s'", name);
                }
                else
                {
                    MXS_NOTICE("Created monitor '%s'", name);
                    MonitorManager::start_monitor(monitor);
                    rval = true;
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
        auto params = extract_parameters(json);

        rval = runtime_create_filter(name, module, &params);
    }

    return rval;
}

bool update_service_relationships(Service* service, json_t* json)
{
    auto old_json = service_to_json(service, "");
    mxb_assert(old_json);

    bool rval = object_to_server_relations(service->name(), old_json, json)
        && service_to_service_relations(service->name(), old_json, json)
        && service_to_filter_relations(service, old_json, json);

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
            tie(ok, params) = extract_and_validate_params(json, router, MODULE_ROUTER, CN_SERVICE);

            if (ok)
            {
                if (auto service = Service::create(name, router, &params))
                {
                    if (update_service_relationships(service, json))
                    {
                        if (service->serialize())
                        {
                            MXS_NOTICE("Created service '%s'", name);
                            serviceStart(service);
                            rval = true;
                        }
                        else
                        {
                            config_runtime_error("Failed to serialize service '%s'", name);
                        }
                    }
                }
                else
                {
                    config_runtime_error("Could not create service '%s' with module '%s'", name, router);
                }
            }
        }
        else
        {
            config_runtime_error("Can't create service '%s', it already exists", name);
        }
    }

    return rval;
}

bool runtime_alter_monitor_from_json(Monitor* monitor, json_t* new_json)
{
    bool success = false;
    std::unique_ptr<json_t> old_json(MonitorManager::monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());
    const MXS_MODULE* mod = get_module(monitor->m_module.c_str(), MODULE_MONITOR);

    auto params = monitor->parameters();
    params.set_multiple(extract_parameters(new_json));

    if (is_valid_resource_body(new_json)
        && validate_param(common_monitor_params(), mod->parameters, &params)
        && server_relationship_to_parameter(new_json, &params))
    {
        success = MonitorManager::reconfigure_monitor(monitor, params);
    }

    return success;
}

bool runtime_alter_monitor_relationships_from_json(Monitor* monitor, json_t* json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(MonitorManager::monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            "servers",
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
    auto new_json = service_to_json(service, "");
    mxb_assert(new_json);

    if (is_valid_relationship_body(json))
    {
        auto rel = mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS);
        json_object_set(rel, type, json);
        rval = update_service_relationships(service, new_json);
    }

    json_decref(new_json);
    return rval;
}

bool can_modify_service_params(Service* service, const mxs::ConfigParameters& params)
{
    bool rval = true;

    const MXS_MODULE* mod = get_module(service->router_name(), MODULE_ROUTER);
    StringSet routerparams;

    for (int i = 0; mod->parameters[i].name; i++)
    {
        routerparams.insert(mod->parameters[i].name);
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
            if (!service->router->configureInstance
                || (service->capabilities() & RCAP_TYPE_RUNTIME_CONFIG) == 0)
            {
                config_runtime_error("Router '%s' does not support reconfiguration.",
                                     service->router_name());
                rval = false;
                break;
            }
        }
        else if (!is_dynamic_param(a.first))
        {
            config_runtime_error("Runtime modifications to static service "
                                 "parameters is not supported: %s=%s",
                                 a.first.c_str(), a.second.c_str());
            rval = false;
        }
    }

    return rval;
}

bool runtime_alter_service_from_json(Service* service, json_t* new_json)
{
    bool rval = false;

    if (validate_service_json(new_json))
    {
        auto params = service->params();
        params.set_multiple(extract_parameters(new_json));
        const MXS_MODULE* mod = get_module(service->router_name(), MODULE_ROUTER);

        if (validate_param(common_service_params(), mod->parameters, &params)
            && can_modify_service_params(service, params)
            && update_service_relationships(service, new_json))
        {
            rval = true;
            service->update_basic_parameters(params);

            if (service->router->configureInstance && service->capabilities() & RCAP_TYPE_RUNTIME_CONFIG)
            {
                if (!service->router->configureInstance(service->router_instance, &params))
                {
                    rval = false;
                    config_runtime_error("Reconfiguration of service '%s' failed. See log "
                                         "file for more details.", service->name());
                }
            }

            if (rval)
            {
                service->serialize();
            }
        }
    }

    return rval;
}

bool runtime_alter_logs_from_json(json_t* json)
{
    bool rval = false;

    if (validate_logs_json(json))
    {
        json_t* param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
        json_t* value;
        rval = true;

        if ((value = mxs_json_pointer(param, "highprecision")))
        {
            mxs_log_set_highprecision_enabled(json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "maxlog")))
        {
            mxs_log_set_maxlog_enabled(json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "syslog")))
        {
            mxs_log_set_syslog_enabled(json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "log_info")))
        {
            mxs_log_set_priority_enabled(LOG_INFO, json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "log_warning")))
        {
            mxs_log_set_priority_enabled(LOG_WARNING, json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "log_notice")))
        {
            mxs_log_set_priority_enabled(LOG_NOTICE, json_boolean_value(value));
        }

        if ((value = mxs_json_pointer(param, "log_debug")))
        {
            mxs_log_set_priority_enabled(LOG_DEBUG, json_boolean_value(value));
        }

        if ((param = mxs_json_pointer(param, "throttling")) && json_is_object(param))
        {
            MXS_LOG_THROTTLING throttle;
            mxs_log_get_throttling(&throttle);

            if ((value = mxs_json_pointer(param, "count")))
            {
                throttle.count = json_integer_value(value);
            }

            if ((value = mxs_json_pointer(param, "suppress_ms")))
            {
                throttle.suppress_ms = json_integer_value(value);
            }

            if ((value = mxs_json_pointer(param, "window_ms")))
            {
                throttle.window_ms = json_integer_value(value);
            }

            mxs_log_set_throttling(&throttle);
        }
    }

    return rval;
}

bool runtime_create_listener_from_json(Service* service, json_t* json)
{
    bool rval = false;

    if (validate_listener_json(json))
    {
        std::string port = json_int_to_string(mxs_json_pointer(json, MXS_JSON_PTR_PARAM_PORT));

        const char* id = get_string_or_null(json, MXS_JSON_PTR_ID);
        const char* address = get_string_or_null(json, MXS_JSON_PTR_PARAM_ADDRESS);
        const char* protocol = get_string_or_null(json, MXS_JSON_PTR_PARAM_PROTOCOL);
        const char* authenticator = get_string_or_null(json, MXS_JSON_PTR_PARAM_AUTHENTICATOR);
        const char* authenticator_options =
            get_string_or_null(json, MXS_JSON_PTR_PARAM_AUTHENTICATOR_OPTIONS);
        const char* ssl_key = get_string_or_null(json, MXS_JSON_PTR_PARAM_SSL_KEY);
        const char* ssl_cert = get_string_or_null(json, MXS_JSON_PTR_PARAM_SSL_CERT);
        const char* ssl_ca_cert = get_string_or_null(json, MXS_JSON_PTR_PARAM_SSL_CA_CERT);
        const char* ssl_version = get_string_or_null(json, MXS_JSON_PTR_PARAM_SSL_VERSION);
        const char* ssl_cert_verify_depth =
            get_string_or_null(json, MXS_JSON_PTR_PARAM_SSL_CERT_VERIFY_DEPTH);
        const char* ssl_verify_peer_certificate = get_string_or_null(json,
                                                                     MXS_JSON_PTR_PARAM_SSL_VERIFY_PEER_CERT);

        // TODO: Create the listener directly from the JSON and pass ssl_verify_peer_host to it.
        rval = runtime_create_listener(service,
                                       id,
                                       address,
                                       port.c_str(),
                                       protocol,
                                       authenticator,
                                       authenticator_options,
                                       ssl_key,
                                       ssl_cert,
                                       ssl_ca_cert,
                                       ssl_version,
                                       ssl_cert_verify_depth,
                                       ssl_verify_peer_certificate);
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
        else if (strtype == CN_UNIX && (err = admin_enable_linux_account(user, type)) == ADMIN_SUCCESS)
        {
            MXS_NOTICE("Enabled account '%s'", user);
            rval = true;
        }
        else if (err)
        {
            config_runtime_error("Failed to add user '%s': %s", user, err);
        }
    }

    return rval;
}

bool runtime_remove_user(const char* id, enum user_type type)
{
    bool rval = false;
    const char* err = type == USER_TYPE_INET ?
        admin_remove_inet_user(id) :
        admin_disable_linux_account(id);

    if (err == ADMIN_SUCCESS)
    {
        MXS_NOTICE("%s '%s'",
                   type == USER_TYPE_INET ?
                   "Deleted network user" : "Disabled account",
                   id);
        rval = true;
    }
    else
    {
        config_runtime_error("Failed to remove user '%s': %s", id, err);
    }

    return rval;
}

bool runtime_alter_user(const std::string& user, const std::string& type, json_t* json)
{
    bool rval = false;
    const char* password = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_PASSWORD));

    if (!password)
    {
        config_runtime_error("No password provided");
    }
    else if (type != CN_INET)
    {
        config_runtime_error("Users of type '%s' cannot be altered", type.c_str());
    }
    else if (const char* err = admin_alter_inet_user(user.c_str(), password))
    {
        config_runtime_error("%s", err);
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

    if (validate_maxscale_json(json))
    {
        rval = true;
        json_t* params = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);
        const char* key;
        json_t* new_val;

        json_object_foreach(params, key, new_val)
        {
            if (ignored_core_parameters(key))
            {
                /** We can't change these at runtime */
                MXS_INFO("Ignoring runtime change to '%s': Cannot be altered at runtime", key);
            }
            else
            {
                mxs::Config& cnf = mxs::Config::get();

                if (auto item = cnf.find_value(key))
                {
                    std::unique_ptr<json_t> old_val(item->to_json());

                    if (!json_equal(old_val.get(), new_val))
                    {
                        if (item->parameter().is_modifiable_at_runtime())
                        {
                            std::string message;

                            if (item->set_from_json(new_val, &message))
                            {
                                MXS_NOTICE("Value of %s changed to %s", key, item->to_string().c_str());
                            }
                            else
                            {
                                config_runtime_error("Invalid value for '%s': %s",
                                                     key, mxs::json_dump(new_val).c_str());
                                rval = false;
                            }
                        }
                        else
                        {
                            config_runtime_error("Global parameter '%s' cannot be modified at runtime", key);
                            rval = false;
                        }
                    }
                }
                else
                {
                    config_runtime_error("Unknown global parameter: %s", key);
                    rval = false;
                }
            }
        }

        if (rval)
        {
            config_global_serialize();
        }
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
                config_runtime_error("The 'recipient' value '%s' does not refer to a worker.",
                                     recipient.c_str());
            }
        }
        else
        {
            config_runtime_error("'recipient' argument not provided, or value is not a valid integer.");
        }
    }
    else
    {
        config_runtime_error("'sessions' argument provided, but value '%s' is not a valid integer.",
                             sessions.c_str());
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
            config_runtime_error("%s", message.c_str());
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
