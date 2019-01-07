/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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
#include <maxscale/json_api.h>
#include <maxscale/paths.h>
#include <maxscale/router.hh>
#include <maxscale/users.h>

#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/modules.hh"
#include "internal/monitor.hh"
#include "internal/query_classifier.hh"
#include "internal/server.hh"

typedef std::set<std::string>    StringSet;
typedef std::vector<std::string> StringVector;

using std::tie;

static std::mutex crt_lock;

#define RUNTIME_ERRMSG_BUFSIZE 512
thread_local char runtime_errmsg[RUNTIME_ERRMSG_BUFSIZE];

typedef std::function<bool (const std::string&, const std::string&)> JsonValidator;
typedef std::pair<const char*, JsonValidator>                        Relationship;

void config_runtime_error(const char* fmt, ...)
{
    va_list list;
    va_start(list, fmt);
    vsnprintf(runtime_errmsg, sizeof(runtime_errmsg), fmt, list);
    va_end(list);
}

static std::string runtime_get_error()
{
    std::string rval(runtime_errmsg);
    runtime_errmsg[0] = '\0';
    return rval;
}

static const MXS_MODULE_PARAM* get_type_parameters(const char* type)
{
    if (strcmp(type, CN_SERVICE) == 0)
    {
        return config_service_params;
    }
    else if (strcmp(type, CN_LISTENER) == 0)
    {
        return config_listener_params;
    }
    else if (strcmp(type, CN_MONITOR) == 0)
    {
        return config_monitor_params;
    }
    else if (strcmp(type, CN_FILTER) == 0)
    {
        return config_filter_params;
    }
    else if (strcmp(type, CN_SERVER) == 0)
    {
        return config_server_params;
    }

    MXS_NOTICE("Module type with no default parameters used: %s", type);
    mxb_assert_message(!true, "Module type with no default parameters used");
    return NULL;
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
static std::pair<bool, MXS_CONFIG_PARAMETER*> load_defaults(const char* name,
                                                            const char* module_type,
                                                            const char* object_type)
{
    bool rval;
    MXS_CONFIG_PARAMETER* params = NULL;
    CONFIG_CONTEXT ctx = {(char*)""};

    if (const MXS_MODULE* mod = get_module(name, module_type))
    {
        config_add_defaults(&ctx, get_type_parameters(object_type));
        config_add_defaults(&ctx, mod->parameters);
        params = ctx.parameters;
        rval = true;
    }
    else
    {
        config_runtime_error("Failed to load module '%s': %s",
                             name,
                             errno ? mxs_strerror(errno) : "See MaxScale logs for details");
    }

    return {rval, params};
}

bool runtime_link_server(Server* server, const char* target)
{
    std::lock_guard<std::mutex> guard(crt_lock);

    bool rval = false;
    Service* service = service_internal_find(target);
    MXS_MONITOR* monitor = service ? NULL : monitor_find(target);

    if (service)
    {
        if (serviceAddBackend(service, server))
        {
            service_serialize(service);
            rval = true;
        }
        else
        {
            config_runtime_error("Service '%s' already uses server '%s'",
                                 service->name,
                                 server->name());
        }
    }
    else if (monitor)
    {
        if (monitor_add_server(monitor, server))
        {
            monitor_serialize(monitor);
            rval = true;
        }
        else
        {
            config_runtime_error("Server '%s' is already monitored", server->name());
        }
    }

    if (rval)
    {
        const char* type = service ? "service" : "monitor";
        MXS_NOTICE("Added server '%s' to %s '%s'", server->name(), type, target);
    }

    return rval;
}

bool runtime_unlink_server(Server* server, const char* target)
{
    std::lock_guard<std::mutex> guard(crt_lock);

    bool rval = false;
    Service* service = service_internal_find(target);
    MXS_MONITOR* monitor = service ? NULL : monitor_find(target);

    if (service || monitor)
    {
        rval = true;

        if (service)
        {
            serviceRemoveBackend(service, server);
            service_serialize(service);
        }
        else if (monitor)
        {
            monitor_remove_server(monitor, server);
            monitor_serialize(monitor);
        }

        const char* type = service ? "service" : "monitor";
        MXS_NOTICE("Removed server '%s' from %s '%s'", server->name(), type, target);
    }

    return rval;
}

bool runtime_create_server(const char* name,
                           const char* address,
                           const char* port,
                           const char* protocol,
                           const char* authenticator)
{
    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = false;

    if (server_find_by_unique_name(name) == NULL)
    {
        if (protocol == NULL)
        {
            protocol = "mariadbbackend";
        }

        CONFIG_CONTEXT ctx {(char*)""};
        bool ok;
        tie(ok, ctx.parameters) = load_defaults(protocol, MODULE_PROTOCOL, CN_SERVER);

        if (ok)
        {
            config_replace_param(&ctx, CN_PROTOCOL, protocol);

            if (address)
            {
                config_replace_param(&ctx, "address", address);
            }
            if (port)
            {
                config_replace_param(&ctx, "port", port);
            }
            if (authenticator)
            {
                config_replace_param(&ctx, "authenticator", authenticator);
            }

            Server* server = Server::server_alloc(name, ctx.parameters);

            if (server && server->serialize())
            {
                rval = true;
                MXS_NOTICE("Created server '%s' at %s:%u",
                           server->name(),
                           server->address,
                           server->port);
            }
            else
            {
                config_runtime_error("Failed to create server '%s', see error log for more details", name);
            }

            config_parameter_free(ctx.parameters);
        }
        else
        {
            config_runtime_error("Server creation failed when loading protocol module '%s'", protocol);
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
    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = false;

    if (service_server_in_use(server) || monitor_server_in_use(server))
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

static SSL_LISTENER* create_ssl(const char* name,
                                const char* key,
                                const char* cert,
                                const char* ca,
                                const char* version,
                                const char* depth,
                                const char* verify)
{
    SSL_LISTENER* rval = NULL;
    CONFIG_CONTEXT* obj = config_context_create(name);

    if (obj)
    {
        if (config_add_param(obj, CN_SSL, CN_REQUIRED)
            && (!key || config_add_param(obj, CN_SSL_KEY, key))
            && (!cert || config_add_param(obj, CN_SSL_CERT, cert))
            && config_add_param(obj, CN_SSL_CA_CERT, ca)
            && (!version || config_add_param(obj, CN_SSL_VERSION, version))
            && (!depth || config_add_param(obj, CN_SSL_CERT_VERIFY_DEPTH, depth))
            && (!verify || config_add_param(obj, CN_SSL_VERIFY_PEER_CERTIFICATE, verify)))
        {
            config_create_ssl(name, obj->parameters, true, &rval);
        }

        config_context_free(obj);
    }

    return rval;
}

bool runtime_enable_server_ssl(Server* server,
                               const char* key,
                               const char* cert,
                               const char* ca,
                               const char* version,
                               const char* depth,
                               const char* verify)
{
    bool rval = false;

    if (key && cert && ca)
    {
        std::lock_guard<std::mutex> guard(crt_lock);
        SSL_LISTENER* ssl = create_ssl(server->name(), key, cert, ca, version, depth, verify);

        if (ssl)
        {
            /** TODO: Properly discard old SSL configurations.This could cause the
             * loss of a pointer if two update operations are done at the same time.*/
            ssl->next = server->server_ssl;

            /** Sync to prevent reads on partially initialized server_ssl */
            atomic_synchronize();
            server->server_ssl = ssl;

            if (server->serialize())
            {
                MXS_NOTICE("Enabled SSL for server '%s'", server->name());
                rval = true;
            }
        }
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
static long get_positive_int(const char* value)
{
    char* endptr;
    long ival = strtol(value, &endptr, 10);

    if (*endptr == '\0' && ival > 0)
    {
        return ival;
    }

    return 0;
}

static inline bool is_valid_integer(const char* value)
{
    char* endptr;
    return strtol(value, &endptr, 10) >= 0 && *value && *endptr == '\0';
}

bool param_is_known(const MXS_MODULE_PARAM* basic,
                    const MXS_MODULE_PARAM* module,
                    const char* key)
{
    std::unordered_set<std::string> names;

    for (auto param : {basic, module})
    {
        for (int i = 0; param[i].name; i++)
        {
            names.insert(param[i].name);
        }
    }

    return names.count(key);
}

bool param_is_valid(const MXS_MODULE_PARAM* basic,
                    const MXS_MODULE_PARAM* module,
                    const char* key,
                    const char* value)
{
    return config_param_is_valid(basic, key, value, NULL)
           || config_param_is_valid(module, key, value, NULL);
}

bool runtime_alter_server(Server* server, const char* key, const char* value)
{
    if (!value[0])
    {
        config_runtime_error("Empty value for parameter: %s", key);
        return false;
    }

    const MXS_MODULE* mod = get_module(server->protocol().c_str(), MODULE_PROTOCOL);

    // As servers allow unknown parameters, we must only validate known parameters
    if (param_is_known(config_server_params, mod->parameters, key)
        && !param_is_valid(config_server_params, mod->parameters, key, value))
    {
        config_runtime_error("Invalid value for parameter '%s': %s", key, value);
        return false;
    }

    if (strcmp(key, CN_DISK_SPACE_THRESHOLD) == 0)
    {
        // This cannot be safely modified during runtime since the monitor thread could be reading the
        // value. TODO: To enable this, the threshold value needs to be moved to the private Server-class
        // so that locking can be enforced on any access.
        config_runtime_error("The server parameter '%s' cannot be modified during runtime.",
                             CN_DISK_SPACE_THRESHOLD);
        return false;
    }

    std::lock_guard<std::mutex> guard(crt_lock);
    server->set_parameter(key, value);

    if (strcmp(key, CN_ADDRESS) == 0)
    {
        server_update_address(server, value);
    }
    else if (strcmp(key, CN_PORT) == 0)
    {
        if (long ival = get_positive_int(value))
        {
            server->update_port(ival);
        }
    }
    else if (strcmp(key, CN_EXTRA_PORT) == 0)
    {
        server->update_extra_port(atoi(value));
    }
    else if (strcmp(key, CN_MONITORUSER) == 0)
    {
        server_update_credentials(server, value, server->monpw);
    }
    else if (strcmp(key, CN_MONITORPW) == 0)
    {
        server_update_credentials(server, server->monuser, value);
    }
    else if (strcmp(key, CN_PERSISTPOOLMAX) == 0)
    {
        if (is_valid_integer(value))
        {
            server->set_persistpoolmax(atoi(value));
        }
    }
    else if (strcmp(key, CN_PERSISTMAXTIME) == 0)
    {
        if (is_valid_integer(value))
        {
            server->set_persistmaxtime(atoi(value));
        }
    }
    else
    {
        /**
         * It's likely that this parameter is used as a weighting parameter.
         * We need to update the weights of services that use this.
         */
        service_update_weights();
    }

    server->serialize();
    MXS_NOTICE("Updated server '%s': %s=%s", server->name(), key, value);

    return true;
}

bool validate_param(const MXS_MODULE_PARAM* basic,
                    const MXS_MODULE_PARAM* module,
                    const char* key,
                    const char* value)
{
    bool rval = false;

    if (!param_is_known(basic, module, key))
    {
        config_runtime_error("Unknown parameter: %s", key);
    }
    else if (!value[0])
    {
        config_runtime_error("Empty value for parameter: %s", key);
    }
    else if (!param_is_valid(basic, module, key, value))
    {
        config_runtime_error("Invalid parameter value for '%s': %s", key, value);
    }
    else
    {
        rval = true;
    }

    return rval;
}

bool do_alter_monitor(MXS_MONITOR* monitor, const char* key, const char* value)
{
    mxb_assert(monitor->state == MONITOR_STATE_STOPPED);
    const MXS_MODULE* mod = get_module(monitor->module_name, MODULE_MONITOR);

    if (!validate_param(config_monitor_params, mod->parameters, key, value))
    {
        return false;
    }
    else if (strcmp(key, "servers") == 0)
    {
        config_runtime_error("Parameter '%s' cannot be altered via this method", key);
        return false;
    }

    std::lock_guard<std::mutex> guard(crt_lock);

    monitor_set_parameter(monitor, key, value);
    bool success = true;
    if (strcmp(key, CN_USER) == 0)
    {
        monitor_add_user(monitor, value, monitor->password);
    }
    else if (strcmp(key, CN_PASSWORD) == 0)
    {
        monitor_add_user(monitor, monitor->user, value);
    }
    else if (strcmp(key, CN_MONITOR_INTERVAL) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_interval(monitor, ival);
        }
    }
    else if (strcmp(key, CN_BACKEND_CONNECT_TIMEOUT) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_network_timeout(monitor,
                                        MONITOR_CONNECT_TIMEOUT,
                                        ival,
                                        CN_BACKEND_CONNECT_TIMEOUT);
        }
    }
    else if (strcmp(key, CN_BACKEND_WRITE_TIMEOUT) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_network_timeout(monitor,
                                        MONITOR_WRITE_TIMEOUT,
                                        ival,
                                        CN_BACKEND_WRITE_TIMEOUT);
        }
    }
    else if (strcmp(key, CN_BACKEND_READ_TIMEOUT) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_network_timeout(monitor,
                                        MONITOR_READ_TIMEOUT,
                                        ival,
                                        CN_BACKEND_READ_TIMEOUT);
        }
    }
    else if (strcmp(key, CN_BACKEND_CONNECT_ATTEMPTS) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_network_timeout(monitor,
                                        MONITOR_CONNECT_ATTEMPTS,
                                        ival,
                                        CN_BACKEND_CONNECT_ATTEMPTS);
        }
    }
    else if (strcmp(key, CN_JOURNAL_MAX_AGE) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_journal_max_age(monitor, ival);
        }
    }
    else if (strcmp(key, CN_SCRIPT_TIMEOUT) == 0)
    {
        if (auto ival = get_positive_int(value))
        {
            monitor_set_script_timeout(monitor, ival);
        }
    }
    else if (strcmp(key, CN_DISK_SPACE_THRESHOLD) == 0)
    {
        success = monitor_set_disk_space_threshold(monitor, value);
    }
    else
    {
        // This should be a module specific parameter
        mxb_assert(config_param_is_valid(mod->parameters, key, value, NULL));
    }

    if (success)
    {
        MXS_NOTICE("Updated monitor '%s': %s=%s", monitor->name, key, value);
    }
    return success;
}

bool runtime_alter_monitor(MXS_MONITOR* monitor, const char* key, const char* value)
{
    // If the monitor is already stopped, don't stop/start it.
    bool was_running = (monitor->state == MONITOR_STATE_RUNNING);
    if (was_running)
    {
        monitor_stop(monitor);
    }
    bool success = do_alter_monitor(monitor, key, value);
    if (success)
    {
        monitor_serialize(monitor);
    }
    if (was_running)
    {
        monitor_start(monitor, monitor->parameters);
    }
    return success;
}

bool runtime_alter_service(Service* service, const char* zKey, const char* zValue)
{
    const MXS_MODULE* mod = get_module(service->routerModule, MODULE_ROUTER);
    std::string key(zKey);
    std::string value(zValue);

    if (!validate_param(config_service_params, mod->parameters, zKey, zValue))
    {
        return false;
    }
    else if (key == "filters" || key == "servers")
    {
        config_runtime_error("Parameter '%s' cannot be altered via this method", zKey);
        return false;
    }

    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = true;

    if (service->is_basic_parameter(key))
    {
        service_replace_parameter(service, zKey, zValue);
        service->update_basic_parameter(key, value);
    }
    else
    {
        if (service->router->configureInstance && service->capabilities & RCAP_TYPE_RUNTIME_CONFIG)
        {
            // Stash the old value in case the reconfiguration fails.
            std::string old_value = config_get_string(service->svc_config_param, key.c_str());
            service_replace_parameter(service, key.c_str(), value.c_str());

            if (!service->router->configureInstance(service->router_instance, service->svc_config_param))
            {
                // Reconfiguration failed, restore the old value of the parameter
                if (old_value.empty())
                {
                    service_remove_parameter(service, key.c_str());
                }
                else
                {
                    service_replace_parameter(service, key.c_str(), old_value.c_str());
                }

                rval = false;
                config_runtime_error("Reconfiguration of service '%s' failed. See log "
                                     "file for more details.",
                                     service->name);
            }
        }
        else
        {
            rval = false;
            config_runtime_error("Router '%s' does not support reconfiguration.",
                                 service->routerModule);
        }
    }

    if (rval)
    {
        service_serialize(service);
        MXS_NOTICE("Updated service '%s': %s=%s", service->name, key.c_str(), value.c_str());
    }

    return rval;
}

bool runtime_alter_maxscale(const char* name, const char* value)
{
    MXS_CONFIG& cnf = *config_get_global_options();
    std::string key = name;
    bool rval = false;

    std::lock_guard<std::mutex> guard(crt_lock);

    if (key == CN_AUTH_CONNECT_TIMEOUT)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            MXS_NOTICE("Updated '%s' from %d to %d",
                       CN_AUTH_CONNECT_TIMEOUT,
                       cnf.auth_conn_timeout,
                       intval);
            cnf.auth_conn_timeout = intval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid timeout value for '%s': %s", CN_AUTH_CONNECT_TIMEOUT, value);
        }
    }
    else if (key == CN_AUTH_READ_TIMEOUT)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            MXS_NOTICE("Updated '%s' from %d to %d",
                       CN_AUTH_READ_TIMEOUT,
                       cnf.auth_read_timeout,
                       intval);
            cnf.auth_read_timeout = intval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid timeout value for '%s': %s", CN_AUTH_READ_TIMEOUT, value);
        }
    }
    else if (key == CN_AUTH_WRITE_TIMEOUT)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            MXS_NOTICE("Updated '%s' from %d to %d",
                       CN_AUTH_WRITE_TIMEOUT,
                       cnf.auth_write_timeout,
                       intval);
            cnf.auth_write_timeout = intval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid timeout value for '%s': %s", CN_AUTH_WRITE_TIMEOUT, value);
        }
    }
    else if (key == CN_ADMIN_AUTH)
    {
        int boolval = config_truth_value(value);

        if (boolval != -1)
        {
            MXS_NOTICE("Updated '%s' from '%s' to '%s'",
                       CN_ADMIN_AUTH,
                       cnf.admin_auth ? "true" : "false",
                       boolval ? "true" : "false");
            cnf.admin_auth = boolval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid boolean value for '%s': %s", CN_ADMIN_AUTH, value);
        }
    }
    else if (key == CN_ADMIN_LOG_AUTH_FAILURES)
    {
        int boolval = config_truth_value(value);

        if (boolval != -1)
        {
            MXS_NOTICE("Updated '%s' from '%s' to '%s'",
                       CN_ADMIN_LOG_AUTH_FAILURES,
                       cnf.admin_log_auth_failures ? "true" : "false",
                       boolval ? "true" : "false");
            cnf.admin_log_auth_failures = boolval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid boolean value for '%s': %s", CN_ADMIN_LOG_AUTH_FAILURES, value);
        }
    }
    else if (key == CN_PASSIVE)
    {
        int boolval = config_truth_value(value);

        if (boolval != -1)
        {
            MXS_NOTICE("Updated '%s' from '%s' to '%s'",
                       CN_PASSIVE,
                       cnf.passive ? "true" : "false",
                       boolval ? "true" : "false");

            if (cnf.passive && !boolval)
            {
                // This MaxScale is being promoted to the active instance
                cnf.promoted_at = mxs_clock();
            }

            cnf.passive = boolval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid boolean value for '%s': %s", CN_PASSIVE, value);
        }
    }
    else if (key == CN_QUERY_CLASSIFIER_CACHE_SIZE)
    {
        uint64_t max_size;

        if (get_suffixed_size(value, &max_size))
        {
            decltype(QC_CACHE_PROPERTIES::max_size)new_size = max_size;

            if (new_size >= 0)
            {
                MXS_NOTICE("Updated '%s' from %" PRIi64 " to %lu",
                           CN_QUERY_CLASSIFIER_CACHE_SIZE,
                           cnf.qc_cache_properties.max_size,
                           max_size);

                cnf.qc_cache_properties.max_size = new_size;
                qc_set_cache_properties(&cnf.qc_cache_properties);
                rval = true;
            }
            else
            {
                config_runtime_error("Value too large for '%s': %s", CN_QUERY_CLASSIFIER_CACHE_SIZE, value);
            }
        }
        else
        {
            config_runtime_error("Invalid size value for '%s': %s", CN_QUERY_CLASSIFIER_CACHE_SIZE, value);
        }
    }
    else if (key == CN_WRITEQ_HIGH_WATER)
    {
        uint64_t size = 0;

        if (!get_suffixed_size(value, &size))
        {
            config_runtime_error("Invalid value for %s: %s", CN_WRITEQ_HIGH_WATER, value);
        }
        else if (size < MIN_WRITEQ_HIGH_WATER)
        {
            config_runtime_error("The specified '%s' is smaller than the minimum allowed size %lu.",
                                 CN_WRITEQ_HIGH_WATER, MIN_WRITEQ_HIGH_WATER);
        }
        else
        {
            rval = true;
            config_set_writeq_high_water(size);
            MXS_NOTICE("'%s' set to: %lu", CN_WRITEQ_HIGH_WATER, size);
        }
    }
    else if (key == CN_WRITEQ_LOW_WATER)
    {
        uint64_t size = 0;

        if (!get_suffixed_size(value, &size))
        {
            config_runtime_error("Invalid value for '%s': %s", CN_WRITEQ_LOW_WATER, value);
        }
        else if (size < MIN_WRITEQ_LOW_WATER)
        {
            config_runtime_error("The specified '%s' is smaller than the minimum allowed size %lu.",
                                 CN_WRITEQ_LOW_WATER, MIN_WRITEQ_LOW_WATER);
        }
        else
        {
            rval = true;
            config_set_writeq_low_water(size);
            MXS_NOTICE("'%s' set to: %lu", CN_WRITEQ_LOW_WATER, size);
        }
    }
    else if (key == CN_MS_TIMESTAMP)
    {
        mxs_log_set_highprecision_enabled(config_truth_value(value));
        rval = true;
    }
    else if (key == CN_SKIP_PERMISSION_CHECKS)
    {
        cnf.skip_permission_checks = config_truth_value(value);
        rval = true;
    }
    else if (key == CN_QUERY_RETRIES)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            cnf.query_retries = intval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid timeout value for '%s': %s", CN_QUERY_RETRIES, value);
        }
    }
    else if (key == CN_QUERY_RETRY_TIMEOUT)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            cnf.query_retry_timeout = intval;
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid timeout value for '%s': %s", CN_QUERY_RETRY_TIMEOUT, value);
        }
    }
    else if (key == CN_RETAIN_LAST_STATEMENTS)
    {
        int intval = get_positive_int(value);
        if (intval)
        {
            session_set_retain_last_statements(intval);
            rval = true;
        }
        else
        {
            config_runtime_error("Invalid value for '%s': %s", CN_RETAIN_LAST_STATEMENTS, value);
        }
    }
    else if (key == CN_DUMP_LAST_STATEMENTS)
    {
        rval = true;
        if (strcmp(value, "on_close") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_ON_CLOSE);
        }
        else if (strcmp(value, "on_error") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_ON_ERROR);
        }
        else if (strcmp(value, "never") == 0)
        {
            session_set_dump_statements(SESSION_DUMP_STATEMENTS_NEVER);
        }
        else
        {
            rval = false;
            config_runtime_error("%s can have the values 'never', 'on_close' or 'on_error'.",
                                 CN_DUMP_LAST_STATEMENTS);
        }
    }
    else if (config_can_modify_at_runtime(key.c_str()))
    {
        config_runtime_error("Global parameter '%s' cannot be modified at runtime", name);
    }
    else
    {
        config_runtime_error("Unknown global parameter: %s=%s", name, value);
    }

    if (rval)
    {
        config_global_serialize();
    }

    return rval;
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

    if (addr == NULL || strcasecmp(addr, CN_DEFAULT) == 0)
    {
        addr = "::";
    }
    if (port == NULL || strcasecmp(port, CN_DEFAULT) == 0)
    {
        port = "3306";
    }
    if (proto == NULL || strcasecmp(proto, CN_DEFAULT) == 0)
    {
        proto = "mariadbclient";
    }

    if (!auth || strcasecmp(auth, CN_DEFAULT) == 0)
    {
        /** Use protocol default authenticator*/
        auth = "";
    }

    if (!auth_opt || strcasecmp(auth_opt, CN_DEFAULT) == 0)
    {
        /** Don't pass options to the authenticator */
        auth_opt = "";
    }

    unsigned short u_port = atoi(port);
    bool rval = false;

    std::lock_guard<std::mutex> guard(crt_lock);

    if (listener_find(name))
    {
        config_runtime_error("Listener '%s' already exists", name);
    }
    else if (SListener l = service_find_listener(service, "", addr, u_port))
    {
        config_runtime_error("Listener '%s' already listens on [%s]:%u", l->name(), addr, u_port);
    }
    else
    {
        SSL_LISTENER* ssl = NULL;

        if (ssl_key && ssl_cert && ssl_ca
            && (ssl =
                    create_ssl(name,
                               ssl_key,
                               ssl_cert,
                               ssl_ca,
                               ssl_version,
                               ssl_depth,
                               verify_ssl)) == NULL)
        {
            MXS_ERROR("SSL initialization for listener '%s' failed.", name);
            config_runtime_error("SSL initialization for listener '%s' failed.", name);
        }
        else
        {
            const char* print_addr = addr ? addr : "::";
            auto listener = Listener::create(service, name, proto, addr, u_port, auth, auth_opt, ssl);

            if (listener && listener_serialize(listener))
            {
                MXS_NOTICE("Created %slistener '%s' at %s:%s for service '%s'",
                           ssl ? "TLS encrypted " : "", name, print_addr, port, service->name);

                if (listener->listen())
                {
                    rval = true;
                }
                else
                {
                    MXS_ERROR("Listener '%s' was created but failed to start it.", name);
                    config_runtime_error("Listener '%s' was created but failed to start it.", name);
                    Listener::destroy(listener);
                    mxb_assert(!listener_find(name));
                }
            }
            else
            {
                MXS_ERROR("Failed to create listener '%s' at %s:%s.", name, print_addr, port);
                config_runtime_error("Failed to create listener '%s' at %s:%s.", name, print_addr, port);
            }
        }
    }

    return rval;
}

bool runtime_destroy_listener(Service* service, const char* name)
{
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), name);

    std::lock_guard<std::mutex> guard(crt_lock);

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
        MXS_ERROR("Failed to destroy listener '%s' for service '%s'", name, service->name);
        config_runtime_error("Failed to destroy listener '%s' for service '%s'", name, service->name);
    }
    else
    {
        rval = true;
        MXS_NOTICE("Destroyed listener '%s' for service '%s'. The listener "
                   "will be removed after the next restart of MaxScale or "
                   "when the associated service is destroyed.",
                   name,
                   service->name);
    }

    return rval;
}

bool runtime_create_monitor(const char* name, const char* module)
{
    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = false;

    if (monitor_find(name) == NULL)
    {

        MXS_MONITOR* monitor = monitor_repurpose_destroyed(name, module);

        if (monitor)
        {
            MXS_DEBUG("Repurposed monitor '%s'", name);
        }
        else
        {
            MXS_CONFIG_PARAMETER* params;
            bool ok;
            tie(ok, params) = load_defaults(module, MODULE_MONITOR, CN_MONITOR);

            if (ok)
            {
                if ((monitor = monitor_create(name, module, params)) == NULL)
                {
                    config_runtime_error("Could not create monitor '%s' with module '%s'", name, module);
                }

                config_parameter_free(params);
            }
        }

        if (monitor)
        {
            if (monitor_serialize(monitor))
            {
                MXS_NOTICE("Created monitor '%s'", name);
                rval = true;
            }
            else
            {
                config_runtime_error("Failed to serialize monitor '%s'", name);
            }
        }
    }
    else
    {
        config_runtime_error("Can't create monitor '%s', it already exists", name);
    }

    return rval;
}

bool runtime_create_filter(const char* name, const char* module, MXS_CONFIG_PARAMETER* params)
{
    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = false;

    if (!filter_find(name))
    {
        SFilterDef filter;
        CONFIG_CONTEXT ctx {(char*)""};
        bool ok;
        tie(ok, ctx.parameters) = load_defaults(module, MODULE_FILTER, CN_FILTER);

        if (ok)
        {
            for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
            {
                config_replace_param(&ctx, p->name, p->value);
            }

            if (!(filter = filter_alloc(name, module, ctx.parameters)))
            {
                config_runtime_error("Could not create filter '%s' with module '%s'", name, module);
            }

            config_parameter_free(ctx.parameters);
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

bool runtime_destroy_filter(const SFilterDef& filter)
{
    mxb_assert(filter);
    bool rval = false;
    std::lock_guard<std::mutex> guard(crt_lock);

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

static bool runtime_create_service(const char* name, const char* router, MXS_CONFIG_PARAMETER* params)
{
    std::lock_guard<std::mutex> guard(crt_lock);
    bool rval = false;

    if (service_internal_find(name) == NULL)
    {
        Service* service = NULL;
        CONFIG_CONTEXT ctx {(char*)""};
        bool ok;
        tie(ok, ctx.parameters) = load_defaults(router, MODULE_ROUTER, CN_SERVICE);

        if (ok)
        {
            for (MXS_CONFIG_PARAMETER* p = params; p; p = p->next)
            {
                config_replace_param(&ctx, p->name, p->value);
            }

            if ((service = service_alloc(name, router, ctx.parameters)) == NULL)
            {
                config_runtime_error("Could not create service '%s' with module '%s'", name, router);
            }

            config_parameter_free(ctx.parameters);
        }

        if (service)
        {
            if (service_serialize(service))
            {
                MXS_NOTICE("Created service '%s'", name);
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
        config_runtime_error("Can't create service '%s', it already exists", name);
    }

    return rval;
}

bool runtime_destroy_service(Service* service)
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(crt_lock);
    mxb_assert(service && service->active);

    if (service_can_be_destroyed(service))
    {
        service_destroy(service);
        rval = true;
    }
    else
    {
        config_runtime_error("Service '%s' cannot be destroyed: Remove all servers and "
                             "destroy all listeners first",
                             service->name);
    }

    return rval;
}

bool runtime_destroy_monitor(MXS_MONITOR* monitor)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), monitor->name);

    std::lock_guard<std::mutex> guard(crt_lock);

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

    if (rval)
    {
        monitor_stop(monitor);

        while (monitor->monitored_servers)
        {
            monitor_remove_server(monitor, monitor->monitored_servers->server);
        }
        monitor_deactivate(monitor);
        MXS_NOTICE("Destroyed monitor '%s'", monitor->name);
    }

    return rval;
}

static MXS_CONFIG_PARAMETER* extract_parameters_from_json(json_t* json)
{
    CONFIG_CONTEXT ctx {(char*)""};

    if (json_t* parameters = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS))
    {
        const char* key;
        json_t* value;

        json_object_foreach(parameters, key, value)
        {
            config_add_param(&ctx, key, json_string_value(value));
        }
    }

    return ctx.parameters;
}

static bool extract_ordered_relations(json_t* json,
                                      StringVector& relations,
                                      const char*   relation_type,
                                      JsonValidator relation_check)
{
    bool rval = true;
    json_t* arr = mxs_json_pointer(json, relation_type);

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

                if (relation_check(type_value, id_value))
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

static bool extract_relations(json_t* json,
                              StringSet& relations,
                              const char* relation_type,
                              JsonValidator relation_check)
{
    StringVector values;
    bool rval = extract_ordered_relations(json, values, relation_type, relation_check);
    relations.insert(values.begin(), values.end());
    return rval;
}

static inline bool is_null_relation(json_t* json, const char* relation)
{
    std::string str(relation);
    size_t pos = str.rfind("/data");

    mxb_assert(pos != std::string::npos);
    str = str.substr(0, pos);

    json_t* data = mxs_json_pointer(json, relation);
    json_t* base = mxs_json_pointer(json, str.c_str());

    return (data && json_is_null(data)) || (base && json_is_null(base));
}

static const char* json_type_to_string(const json_t* json)
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

static inline const char* get_string_or_null(json_t* json, const char* path)
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
static bool is_valid_resource_body(json_t* json)
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

static bool server_contains_required_fields(json_t* json)
{
    json_t* id = mxs_json_pointer(json, MXS_JSON_PTR_ID);
    json_t* port = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_PORT);
    json_t* address = mxs_json_pointer(json, MXS_JSON_PTR_PARAM_ADDRESS);
    bool rval = false;

    if (!id)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_ID);
    }
    else if (!json_is_string(id))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_ID);
    }
    else if (!address)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (!json_is_string(address))
    {
        config_runtime_error("The '%s' field is not a string", MXS_JSON_PTR_PARAM_ADDRESS);
    }
    else if (!port)
    {
        config_runtime_error("Request body does not define the '%s' field", MXS_JSON_PTR_PARAM_PORT);
    }
    else if (!json_is_integer(port))
    {
        config_runtime_error("The '%s' field is not an integer", MXS_JSON_PTR_PARAM_PORT);
    }
    else
    {
        rval = true;
    }

    return rval;
}

static bool server_relation_is_valid(const std::string& type, const std::string& value)
{
    return (type == CN_SERVICES && service_internal_find(value.c_str()))
           || (type == CN_MONITORS && monitor_find(value.c_str()));
}

static bool filter_to_service_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVICES && service_internal_find(value.c_str());
}

static bool service_to_filter_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_FILTERS && filter_find(value.c_str());
}

static bool unlink_server_from_objects(Server* server, StringSet& relations)
{
    bool rval = true;

    for (StringSet::iterator it = relations.begin(); it != relations.end(); it++)
    {
        if (!runtime_unlink_server(server, it->c_str()))
        {
            rval = false;
        }
    }

    return rval;
}

static bool link_server_to_objects(Server* server, StringSet& relations)
{
    bool rval = true;

    for (StringSet::iterator it = relations.begin(); it != relations.end(); it++)
    {
        if (!runtime_link_server(server, it->c_str()))
        {
            unlink_server_from_objects(server, relations);
            rval = false;
            break;
        }
    }

    return rval;
}

static std::string json_int_to_string(json_t* json)
{
    char str[25];   // Enough to store any 64-bit integer value
    int64_t i = json_integer_value(json);
    snprintf(str, sizeof(str), "%ld", i);
    return std::string(str);
}

static inline bool have_ssl_json(json_t* params)
{
    return mxs_json_pointer(params, CN_SSL_KEY)
           || mxs_json_pointer(params, CN_SSL_CERT)
           || mxs_json_pointer(params, CN_SSL_CA_CERT);
}

namespace
{

enum object_type
{
    OT_SERVER,
    OT_LISTENER
};
}

static bool validate_ssl_json(json_t* params, object_type type)
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

static bool process_ssl_parameters(Server* server, json_t* params)
{
    mxb_assert(server->server_ssl == NULL);
    bool rval = true;

    if (have_ssl_json(params))
    {
        if (validate_ssl_json(params, OT_SERVER))
        {
            char buf[20];       // Enough to hold the string form of the ssl_cert_verify_depth
            char buf_verify[20];// Enough to hold the string form of the ssl_verify_peer_certificate
            const char* key = json_string_value(mxs_json_pointer(params, CN_SSL_KEY));
            const char* cert = json_string_value(mxs_json_pointer(params, CN_SSL_CERT));
            const char* ca = json_string_value(mxs_json_pointer(params, CN_SSL_CA_CERT));
            const char* version = json_string_value(mxs_json_pointer(params, CN_SSL_VERSION));
            const char* depth = NULL;
            json_t* depth_json = mxs_json_pointer(params, CN_SSL_CERT_VERIFY_DEPTH);

            if (depth_json)
            {
                snprintf(buf, sizeof(buf), "%lld", json_integer_value(depth_json));
                depth = buf;
            }

            const char* verify = NULL;
            json_t* verify_json = mxs_json_pointer(params, CN_SSL_VERIFY_PEER_CERTIFICATE);

            if (verify_json)
            {
                snprintf(buf_verify, sizeof(buf), "%s", json_boolean_value(verify_json) ? "true" : "false");
                verify = buf_verify;
            }

            if (!runtime_enable_server_ssl(server, key, cert, ca, version, depth, verify))
            {
                config_runtime_error("Failed to initialize SSL for server '%s'. See "
                                     "error log for more details.",
                                     server->name());
                rval = false;
            }
        }
        else
        {
            rval = false;
        }
    }

    return rval;
}

Server* runtime_create_server_from_json(json_t* json)
{
    Server* rval = NULL;

    if (is_valid_resource_body(json)
        && server_contains_required_fields(json))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* address = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_PARAM_ADDRESS));

        /** The port needs to be in string format */
        std::string port = json_int_to_string(mxs_json_pointer(json, MXS_JSON_PTR_PARAM_PORT));

        /** Optional parameters */
        const char* protocol = get_string_or_null(json, MXS_JSON_PTR_PARAM_PROTOCOL);
        const char* authenticator = get_string_or_null(json, MXS_JSON_PTR_PARAM_AUTHENTICATOR);

        StringSet relations;

        if (extract_relations(json, relations, MXS_JSON_PTR_RELATIONSHIPS_SERVICES, server_relation_is_valid)
            && extract_relations(json,
                                 relations,
                                 MXS_JSON_PTR_RELATIONSHIPS_MONITORS,
                                 server_relation_is_valid))
        {
            if (runtime_create_server(name, address, port.c_str(), protocol, authenticator))
            {
                rval = Server::find_by_unique_name(name);
                mxb_assert(rval);
                json_t* param = mxs_json_pointer(json, MXS_JSON_PTR_PARAMETERS);

                if (!process_ssl_parameters(rval, param) || !link_server_to_objects(rval, relations))
                {
                    runtime_destroy_server(rval);
                    rval = NULL;
                }
            }
        }
        else
        {
            config_runtime_error("Invalid relationships in request JSON");
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

    const char* server_relation_types[] =
    {
        MXS_JSON_PTR_RELATIONSHIPS_SERVICES,
        MXS_JSON_PTR_RELATIONSHIPS_MONITORS,
        NULL
    };

    bool rval = true;
    StringSet old_relations;
    StringSet new_relations;

    for (int i = 0; server_relation_types[i]; i++)
    {
        // Extract only changed or deleted relationships
        if (is_null_relation(new_json, server_relation_types[i])
            || mxs_json_pointer(new_json, server_relation_types[i]))
        {
            if (!extract_relations(new_json,
                                   new_relations,
                                   server_relation_types[i],
                                   server_relation_is_valid)
                || !extract_relations(old_json,
                                      old_relations,
                                      server_relation_types[i],
                                      server_relation_is_valid))
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

        if (!unlink_server_from_objects(server, removed_relations)
            || !link_server_to_objects(server, added_relations))
        {
            rval = false;
        }
    }

    return rval;
}

bool runtime_alter_server_from_json(Server* server, json_t* new_json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(server_to_json(server, ""));
    mxb_assert(old_json.get());

    if (is_valid_resource_body(new_json)
        && server_to_object_relations(server, old_json.get(), new_json))
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

static bool is_valid_relationship_body(json_t* json)
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

bool runtime_alter_server_relationships_from_json(Server* server, const char* type, json_t* json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(server_to_json(server, ""));
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

static bool object_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_SERVERS && server_find_by_unique_name(value.c_str());
}

static bool filter_relation_is_valid(const std::string& type, const std::string& value)
{
    return type == CN_FILTERS && filter_find(value.c_str());
}

//
// Constants for validate_object_json
//
const Relationship object_to_server
{
    MXS_JSON_PTR_RELATIONSHIPS_SERVERS,
    object_relation_is_valid
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

/**
 * @brief Do a coarse validation of the monitor JSON
 *
 * @param json          JSON to validate
 * @param paths         List of paths that must be string values
 * @param relationships List of JSON paths and validation functions to check
 *
 * @return True of the JSON is valid
 */
static bool validate_object_json(json_t* json,
                                 std::vector<std::string> paths,
                                 std::vector<Relationship> relationships)
{
    bool rval = false;
    json_t* value;

    if (is_valid_resource_body(json))
    {
        if (!(value = mxs_json_pointer(json, MXS_JSON_PTR_ID)))
        {
            config_runtime_error("Value not found: '%s'", MXS_JSON_PTR_ID);
        }
        else if (!json_is_string(value))
        {
            config_runtime_error("Value '%s' is not a string", MXS_JSON_PTR_ID);
        }
        else
        {
            for (const auto& a : paths)
            {
                if (!(value = mxs_json_pointer(json, a.c_str())))
                {
                    config_runtime_error("Invalid value for '%s'", a.c_str());
                }
                else if (!json_is_string(value))
                {
                    config_runtime_error("Value '%s' is not a string", a.c_str());
                }
            }

            for (const auto& a : relationships)
            {
                StringSet relations;
                if (extract_relations(json, relations, a.first, a.second))
                {
                    rval = true;
                }
            }
        }
    }

    return rval;
}

static bool unlink_object_from_servers(const char* target, StringSet& relations)
{
    bool rval = true;

    for (StringSet::iterator it = relations.begin(); it != relations.end(); it++)
    {
        auto server = Server::find_by_unique_name(*it);

        if (!server || !runtime_unlink_server(server, target))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

static bool link_object_to_servers(const char* target, StringSet& relations)
{
    bool rval = true;

    for (StringSet::iterator it = relations.begin(); it != relations.end(); it++)
    {
        auto server = Server::find_by_unique_name(*it);

        if (!server || !runtime_link_server(server, target))
        {
            unlink_server_from_objects(server, relations);
            rval = false;
            break;
        }
    }

    return rval;
}

static bool validate_monitor_json(json_t* json)
{
    bool rval = true;
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

    return rval;
}

MXS_MONITOR* runtime_create_monitor_from_json(json_t* json)
{
    MXS_MONITOR* rval = NULL;

    if (validate_object_json(json, {MXS_JSON_PTR_MODULE}, {object_to_server})
        && validate_monitor_json(json))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_MODULE));

        if (runtime_create_monitor(name, module))
        {
            rval = monitor_find(name);
            mxb_assert(rval);

            if (!runtime_alter_monitor_from_json(rval, json))
            {
                runtime_destroy_monitor(rval);
                rval = NULL;
            }
            else
            {
                monitor_start(rval, rval->parameters);
            }
        }
    }

    return rval;
}

bool runtime_create_filter_from_json(json_t* json)
{
    bool rval = false;

    if (validate_object_json(json, {MXS_JSON_PTR_MODULE}, {filter_to_service}))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* module = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_MODULE));
        MXS_CONFIG_PARAMETER* params = extract_parameters_from_json(json);

        rval = runtime_create_filter(name, module, params);

        config_parameter_free(params);
    }

    return rval;
}

Service* runtime_create_service_from_json(json_t* json)
{
    Service* rval = NULL;

    if (validate_object_json(json, {MXS_JSON_PTR_ROUTER}, {service_to_filter, object_to_server}))
    {
        const char* name = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* router = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ROUTER));
        MXS_CONFIG_PARAMETER* params = extract_parameters_from_json(json);

        if (runtime_create_service(name, router, params))
        {
            rval = service_internal_find(name);
            mxb_assert(rval);

            // Performing an alter right after creation takes care of server relationships
            if (!runtime_alter_service_from_json(rval, json))
            {
                runtime_destroy_service(rval);
                rval = NULL;
            }
            else
            {
                // This function sets the service in the correct state
                serviceStart(rval);
            }
        }

        config_parameter_free(params);
    }

    return rval;
}

bool object_to_server_relations(const char* target, json_t* old_json, json_t* new_json)
{
    if (mxs_json_pointer(new_json, MXS_JSON_PTR_RELATIONSHIPS_SERVERS) == NULL)
    {
        /** No change to relationships */
        return true;
    }

    bool rval = false;
    StringSet old_relations;
    StringSet new_relations;
    const char* object_relation = MXS_JSON_PTR_RELATIONSHIPS_SERVERS;

    if (extract_relations(old_json, old_relations, object_relation, object_relation_is_valid)
        && extract_relations(new_json, new_relations, object_relation, object_relation_is_valid))
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

        if (unlink_object_from_servers(target, removed_relations)
            && link_object_to_servers(target, added_relations))
        {
            rval = true;
        }
    }
    else
    {
        config_runtime_error("Invalid object relations for '%s'", target);
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

    if (extract_ordered_relations(old_json, old_relations, filter_relation, filter_relation_is_valid)
        && extract_ordered_relations(new_json, new_relations, filter_relation, filter_relation_is_valid))
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
        config_runtime_error("Invalid object relations for '%s'", service->name);
    }

    return rval;
}

bool runtime_alter_monitor_from_json(MXS_MONITOR* monitor, json_t* new_json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());

    if (is_valid_resource_body(new_json)
        && object_to_server_relations(monitor->name, old_json.get(), new_json))
    {
        rval = true;
        bool changed = false;
        json_t* parameters = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS);
        json_t* old_parameters = mxs_json_pointer(old_json.get(), MXS_JSON_PTR_PARAMETERS);

        mxb_assert(old_parameters);

        if (parameters)
        {
            bool restart = monitor->state != MONITOR_STATE_STOPPED;
            monitor_stop(monitor);
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
                else if (do_alter_monitor(monitor, key, mxs::json_to_string(value).c_str()))
                {
                    changed = true;
                }
                else
                {
                    rval = false;
                    break;
                }
            }

            if (rval && changed)
            {
                monitor_serialize(monitor);
            }

            if (restart)
            {
                monitor_start(monitor, monitor->parameters);
            }
        }
    }

    return rval;
}

bool runtime_alter_monitor_relationships_from_json(MXS_MONITOR* monitor, json_t* json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(monitor_to_json(monitor, ""));
    mxb_assert(old_json.get());

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            "servers",
                                            "data",
                                            json_object_get(json, "data")));

        if (object_to_server_relations(monitor->name, old_json.get(), j.get()))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_alter_service_relationships_from_json(Service* service, const char* type, json_t* json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(service_to_json(service, ""));
    mxb_assert(old_json.get());

    if (is_valid_relationship_body(json))
    {
        std::unique_ptr<json_t> j(json_pack("{s: {s: {s: {s: O}}}}",
                                            "data",
                                            "relationships",
                                            type,
                                            "data",
                                            json_object_get(json, "data")));

        if (strcmp(type, CN_SERVERS) == 0)
        {
            rval = object_to_server_relations(service->name, old_json.get(), j.get());
        }
        else
        {
            mxb_assert(strcmp(type, CN_FILTERS) == 0);
            rval = service_to_filter_relations(service, old_json.get(), j.get());
        }
    }

    return rval;
}

/**
 * @brief Check if the service parameter can be altered at runtime
 *
 * @param key Parameter name
 * @return True if the parameter can be altered
 */
static bool is_dynamic_param(const std::string& key)
{
    return key != CN_TYPE
           && key != CN_ROUTER
           && key != CN_SERVERS;
}

bool runtime_alter_service_from_json(Service* service, json_t* new_json)
{
    bool rval = false;
    std::unique_ptr<json_t> old_json(service_to_json(service, ""));
    mxb_assert(old_json.get());

    if (is_valid_resource_body(new_json)
        && object_to_server_relations(service->name, old_json.get(), new_json)
        && service_to_filter_relations(service, old_json.get(), new_json))
    {
        rval = true;
        json_t* parameters = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS);
        json_t* old_parameters = mxs_json_pointer(old_json.get(), MXS_JSON_PTR_PARAMETERS);

        mxb_assert(old_parameters);

        if (parameters)
        {
            /** Create a set of accepted service parameters */
            StringSet paramset;
            for (int i = 0; config_service_params[i].name; i++)
            {
                if (is_dynamic_param(config_service_params[i].name))
                {
                    paramset.insert(config_service_params[i].name);
                }
            }

            const MXS_MODULE* mod = get_module(service->routerModule, MODULE_ROUTER);

            for (int i = 0; mod->parameters[i].name; i++)
            {
                paramset.insert(mod->parameters[i].name);
            }

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
                else if (paramset.find(key) != paramset.end())
                {
                    /** Parameter can be altered */
                    if (!runtime_alter_service(service, key, mxs::json_to_string(value).c_str()))
                    {
                        rval = false;
                    }
                }
                else
                {
                    std::string v = mxs::json_to_string(value);

                    if (!is_dynamic_param(key))
                    {
                        config_runtime_error("Runtime modifications to static service "
                                             "parameters is not supported: %s=%s",
                                             key,
                                             v.c_str());
                    }
                    else
                    {
                        config_runtime_error("Parameter '%s' cannot be modified at runtime", key);
                    }

                    rval = false;
                }
            }
        }
    }

    return rval;
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

static bool validate_listener_json(json_t* json)
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

json_t* runtime_get_json_error()
{
    json_t* obj = NULL;
    std::string errmsg = runtime_get_error();

    if (errmsg.length())
    {
        obj = mxs_json_error(errmsg.c_str());
    }

    return obj;
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
    else if (json_to_account_type(account) == USER_ACCOUNT_UNKNOWN)
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

bool runtime_create_user_from_json(json_t* json)
{
    bool rval = false;

    if (validate_user_json(json))
    {
        const char* user = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_ID));
        const char* password = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_PASSWORD));
        std::string strtype = json_string_value(mxs_json_pointer(json, MXS_JSON_PTR_TYPE));
        user_account_type type = json_to_account_type(mxs_json_pointer(json, MXS_JSON_PTR_ACCOUNT));
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
            && runtime_is_size_or_null(param, CN_QUERY_CLASSIFIER_CACHE_SIZE);
    }

    return rval;
}

bool ignored_core_parameters(const char* key)
{
    static const char* params[] =
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
        NULL
    };

    for (int i = 0; params[i]; i++)
    {
        if (strcmp(key, params[i]) == 0)
        {
            return true;
        }
    }

    return false;
}

bool runtime_alter_maxscale_from_json(json_t* new_json)
{
    bool rval = false;

    if (validate_maxscale_json(new_json))
    {
        rval = true;
        json_t* old_json = config_maxscale_to_json("");
        mxb_assert(old_json);

        json_t* new_param = mxs_json_pointer(new_json, MXS_JSON_PTR_PARAMETERS);
        json_t* old_param = mxs_json_pointer(old_json, MXS_JSON_PTR_PARAMETERS);

        const char* key;
        json_t* value;

        json_object_foreach(new_param, key, value)
        {
            json_t* new_val = json_object_get(new_param, key);
            json_t* old_val = json_object_get(old_param, key);

            if (old_val && new_val && mxs::json_to_string(new_val) == mxs::json_to_string(old_val))
            {
                /** No change in values */
            }
            else if (ignored_core_parameters(key))
            {
                /** We can't change these at runtime */
                MXS_INFO("Ignoring runtime change to '%s': Cannot be altered at runtime", key);
            }
            else if (!runtime_alter_maxscale(key, mxs::json_to_string(value).c_str()))
            {
                rval = false;
            }
        }
    }

    return rval;
}

bool runtime_alter_qc_from_json(json_t* json)
{
    return qc_alter_from_json(json);
}
