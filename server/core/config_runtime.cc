/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include "maxscale/config_runtime.h"

#include <strings.h>
#include <string>
#include <set>
#include <iterator>
#include <algorithm>

#include <maxscale/atomic.h>
#include <maxscale/paths.h>
#include <maxscale/spinlock.h>
#include <maxscale/jansson.hh>

#include "maxscale/config.h"
#include "maxscale/monitor.h"
#include "maxscale/modules.h"
#include "maxscale/service.h"

using std::string;
using std::set;
using mxs::Closer;

static SPINLOCK crt_lock = SPINLOCK_INIT;

bool runtime_link_server(SERVER *server, const char *target)
{
    spinlock_acquire(&crt_lock);

    bool rval = false;
    SERVICE *service = service_find(target);
    MXS_MONITOR *monitor = service ? NULL : monitor_find(target);

    if (service)
    {
        if (serviceAddBackend(service, server))
        {
            service_serialize_servers(service);
            rval = true;
        }
    }
    else if (monitor)
    {
        if (monitorAddServer(monitor, server))
        {
            monitor_serialize_servers(monitor);
            rval = true;
        }
    }

    if (rval)
    {
        const char *type = service ? "service" : "monitor";
        MXS_NOTICE("Added server '%s' to %s '%s'", server->unique_name, type, target);
    }

    spinlock_release(&crt_lock);
    return rval;
}

bool runtime_unlink_server(SERVER *server, const char *target)
{
    spinlock_acquire(&crt_lock);

    bool rval = false;
    SERVICE *service = service_find(target);
    MXS_MONITOR *monitor = service ? NULL : monitor_find(target);

    if (service || monitor)
    {
        rval = true;

        if (service)
        {
            serviceRemoveBackend(service, server);
            service_serialize_servers(service);
        }
        else if (monitor)
        {
            monitorRemoveServer(monitor, server);
            monitor_serialize_servers(monitor);
        }

        const char *type = service ? "service" : "monitor";
        MXS_NOTICE("Removed server '%s' from %s '%s'", server->unique_name, type, target);
    }

    spinlock_release(&crt_lock);
    return rval;
}


bool runtime_create_server(const char *name, const char *address, const char *port,
                           const char *protocol, const char *authenticator,
                           const char *authenticator_options)
{
    spinlock_acquire(&crt_lock);
    bool rval = false;

    if (server_find_by_unique_name(name) == NULL)
    {
        // TODO: Get default values from the protocol module
        if (port == NULL)
        {
            port = "3306";
        }
        if (protocol == NULL)
        {
            protocol = "MySQLBackend";
        }
        if (authenticator == NULL && (authenticator = get_default_authenticator(protocol)) == NULL)
        {
            MXS_ERROR("No authenticator defined for server '%s' and no default "
                      "authenticator for protocol '%s'.", name, protocol);
            spinlock_release(&crt_lock);
            return false;
        }

        /** First check if this service has been created before */
        SERVER *server = server_find_destroyed(name, protocol, authenticator,
                                               authenticator_options);

        if (server)
        {
            /** Found old server, replace network details with new ones and
             * reactivate it */
            snprintf(server->name, sizeof(server->name), "%s", address);
            server->port = atoi(port);
            server->is_active = true;
            rval = true;
        }
        else
        {
            /**
             * server_alloc will add the server to the global list of
             * servers so we don't need to manually add it.
             */
            server = server_alloc(name, address, atoi(port), protocol,
                                  authenticator, authenticator_options);
        }

        if (server && server_serialize(server))
        {
            /** Mark that the server was created after startup */
            server->created_online = true;

            MXS_NOTICE("Created server '%s' at %s:%u", server->unique_name,
                       server->name, server->port);
            rval = true;
        }
    }

    spinlock_release(&crt_lock);
    return rval;
}

bool runtime_destroy_server(SERVER *server)
{
    spinlock_acquire(&crt_lock);
    bool rval = false;

    if (service_server_in_use(server) || monitor_server_in_use(server))
    {
        MXS_ERROR("Cannot destroy server '%s' as it is used by at least one "
                  "service or monitor", server->unique_name);
    }
    else
    {
        char filename[PATH_MAX];
        snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(),
                 server->unique_name);

        if (unlink(filename) == -1)
        {
            if (errno != ENOENT)
            {
                MXS_ERROR("Failed to remove persisted server configuration '%s': %d, %s",
                          filename, errno, mxs_strerror(errno));
            }
            else
            {
                rval = true;
                MXS_WARNING("Server '%s' was not created at runtime. Remove the "
                            "server manually from the correct configuration file.",
                            server->unique_name);
            }
        }
        else
        {
            rval = true;
        }

        if (rval)
        {
            MXS_NOTICE("Destroyed server '%s' at %s:%u", server->unique_name,
                       server->name, server->port);
            server->is_active = false;
        }
    }

    spinlock_release(&crt_lock);
    return rval;
}

static SSL_LISTENER* create_ssl(const char *name, const char *key, const char *cert,
                                const char *ca, const char *version, const char *depth)
{
    SSL_LISTENER *rval = NULL;
    CONFIG_CONTEXT *obj = config_context_create(name);

    if (obj)
    {
        if (config_add_param(obj, CN_SSL, CN_REQUIRED) &&
            config_add_param(obj, CN_SSL_KEY, key) &&
            config_add_param(obj, CN_SSL_CERT, cert) &&
            config_add_param(obj, CN_SSL_CA_CERT, ca) &&
            (!version || config_add_param(obj, CN_SSL_VERSION, version)) &&
            (!depth || config_add_param(obj, CN_SSL_CERT_VERIFY_DEPTH, depth)))
        {
            int err = 0;
            SSL_LISTENER *ssl = make_ssl_structure(obj, true, &err);

            if (err == 0 && ssl && listener_init_SSL(ssl) == 0)
            {
                rval = ssl;
            }
        }

        config_context_free(obj);
    }

    return rval;
}

bool runtime_enable_server_ssl(SERVER *server, const char *key, const char *cert,
                               const char *ca, const char *version, const char *depth)
{
    bool rval = false;

    if (key && cert && ca)
    {
        spinlock_acquire(&crt_lock);
        SSL_LISTENER *ssl = create_ssl(server->unique_name, key, cert, ca, version, depth);

        if (ssl)
        {
            /** TODO: Properly discard old SSL configurations.This could cause the
             * loss of a pointer if two update operations are done at the same time.*/
            ssl->next = server->server_ssl;

            /** Sync to prevent reads on partially initialized server_ssl */
            atomic_synchronize();
            server->server_ssl = ssl;

            if (server_serialize(server))
            {
                MXS_NOTICE("Enabled SSL for server '%s'", server->unique_name);
                rval = true;
            }
        }
        spinlock_release(&crt_lock);
    }

    return rval;
}

bool runtime_alter_server(SERVER *server, const char *key, const char *value)
{
    spinlock_acquire(&crt_lock);
    bool valid = true;

    if (strcmp(key, CN_ADDRESS) == 0)
    {
        server_update_address(server, value);
    }
    else if (strcmp(key, CN_PORT) == 0)
    {
        server_update_port(server, atoi(value));
    }
    else if (strcmp(key, CN_MONITORUSER) == 0)
    {
        server_update_credentials(server, value, server->monpw);
    }
    else if (strcmp(key, CN_MONITORPW) == 0)
    {
        server_update_credentials(server, server->monuser, value);
    }
    else
    {
        if (!server_remove_parameter(server, key) && !value[0])
        {
            valid = false;
        }
        else if (value[0])
        {
            server_add_parameter(server, key, value);

            /**
             * It's likely that this parameter is used as a weighting parameter.
             * We need to update the weights of services that use this.
             */
            service_update_weights();
        }
    }

    if (valid)
    {
        if (server->created_online)
        {
            server_serialize(server);
        }
        MXS_NOTICE("Updated server '%s': %s=%s", server->unique_name, key, value);
    }

    spinlock_release(&crt_lock);
    return valid;
}

/**
 * @brief Convert a string value to a positive integer
 *
 * If the value is not a positive integer, an error is printed to @c dcb.
 *
 * @param value String value
 * @return 0 on error, otherwise a positive integer
 */
static long get_positive_int(const char *value)
{
    char *endptr;
    long ival = strtol(value, &endptr, 10);

    if (*endptr == '\0' && ival > 0)
    {
        return ival;
    }

    return 0;
}

/**
 * @brief Add default parameters to a monitor
 *
 * @param monitor Monitor to modify
 */
static void add_monitor_defaults(MXS_MONITOR *monitor)
{
    /** Inject the default module parameters in case we only deleted
     * a parameter */
    CONFIG_CONTEXT ctx = {};
    ctx.object = (char*)"";
    const MXS_MODULE *mod = get_module(monitor->module_name, MODULE_MONITOR);

    if (mod)
    {
        config_add_defaults(&ctx, mod->parameters);
        monitorAddParameters(monitor, ctx.parameters);
        config_parameter_free(ctx.parameters);
    }
    else
    {
        MXS_ERROR("Failed to load module '%s'. See previous error messages for more details.",
                  monitor->module_name);
    }
}

bool runtime_alter_monitor(MXS_MONITOR *monitor, const char *key, const char *value)
{
    spinlock_acquire(&crt_lock);
    bool valid = false;

    if (strcmp(key, CN_USER) == 0)
    {
        valid = true;
        monitorAddUser(monitor, value, monitor->password);
    }
    else if (strcmp(key, CN_PASSWORD) == 0)
    {
        valid = true;
        monitorAddUser(monitor, monitor->user, value);
    }
    else if (strcmp(key, CN_MONITOR_INTERVAL) == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetInterval(monitor, ival);
        }
    }
    else if (strcmp(key, CN_BACKEND_CONNECT_TIMEOUT) == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_CONNECT_TIMEOUT, ival);
        }
    }
    else if (strcmp(key, CN_BACKEND_WRITE_TIMEOUT) == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_WRITE_TIMEOUT, ival);
        }
    }
    else if (strcmp(key, CN_BACKEND_READ_TIMEOUT) == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_READ_TIMEOUT, ival);
        }
    }
    else if (strcmp(key, CN_BACKEND_CONNECT_ATTEMPTS) == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_CONNECT_ATTEMPTS, ival);
        }
    }
    else
    {
        /** We're modifying module specific parameters and we need to stop the monitor */
        monitorStop(monitor);

        if (monitorRemoveParameter(monitor, key) || value[0])
        {
            /** Either we're removing an existing parameter or adding a new one */
            valid = true;

            if (value[0])
            {
                MXS_CONFIG_PARAMETER p = {};
                p.name = const_cast<char*>(key);
                p.value = const_cast<char*>(value);
                monitorAddParameters(monitor, &p);
            }

            add_monitor_defaults(monitor);
        }

        monitorStart(monitor, monitor->parameters);
    }

    if (valid)
    {
        if (monitor->created_online)
        {
            monitor_serialize(monitor);
        }

        MXS_NOTICE("Updated monitor '%s': %s=%s", monitor->name, key, value);
    }

    spinlock_release(&crt_lock);
    return valid;
}

bool runtime_create_listener(SERVICE *service, const char *name, const char *addr,
                             const char *port, const char *proto, const char *auth,
                             const char *auth_opt, const char *ssl_key,
                             const char *ssl_cert, const char *ssl_ca,
                             const char *ssl_version, const char *ssl_depth)
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
        proto = "MySQLClient";
    }

    if (auth && strcasecmp(auth, CN_DEFAULT) == 0)
    {
        /** Set auth to NULL so the protocol default authenticator is used */
        auth = NULL;
    }

    if (auth_opt && strcasecmp(auth_opt, CN_DEFAULT) == 0)
    {
        /** Don't pass options to the authenticator */
        auth_opt = NULL;
    }

    unsigned short u_port = atoi(port);

    spinlock_acquire(&crt_lock);

    SSL_LISTENER *ssl = NULL;
    bool rval = false;

    if (!serviceHasListener(service, proto, addr, u_port))
    {
        rval = true;

        if (ssl_key && ssl_cert && ssl_ca)
        {
            ssl = create_ssl(name, ssl_key, ssl_cert, ssl_ca, ssl_version, ssl_depth);

            if (ssl == NULL)
            {
                MXS_ERROR("SSL initialization for listener '%s' failed.", name);
                rval = false;
            }
        }

        if (rval)
        {
            const char *print_addr = addr ? addr : "::";
            SERV_LISTENER *listener = serviceCreateListener(service, name, proto, addr,
                                                            u_port, auth, auth_opt, ssl);

            if (listener && listener_serialize(listener) && serviceLaunchListener(service, listener))
            {
                MXS_NOTICE("Created %slistener '%s' at %s:%s for service '%s'",
                           ssl ? "TLS encrypted " : "",
                           name, print_addr, port, service->name);
            }
            else
            {
                MXS_ERROR("Failed to start listener '%s' at %s:%s.", name, print_addr, port);
                rval = false;
            }
        }
    }

    spinlock_release(&crt_lock);
    return rval;
}

bool runtime_destroy_listener(SERVICE *service, const char *name)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), name);

    spinlock_acquire(&crt_lock);

    if (unlink(filename) == -1)
    {
        if (errno != ENOENT)
        {
            MXS_ERROR("Failed to remove persisted listener configuration '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
        else
        {
            rval = false;
            MXS_WARNING("Listener '%s' was not created at runtime. Remove the "
                        "listener manually from the correct configuration file.",
                        name);
        }
    }
    else
    {
        rval = true;
    }

    if (rval)
    {
        rval = serviceStopListener(service, name);

        if (rval)
        {
            MXS_NOTICE("Destroyed listener '%s' for service '%s'. The listener "
                       "will be removed after the next restart of MaxScale.",
                       name, service->name);
        }
        else
        {
            MXS_ERROR("Failed to destroy listener '%s' for service '%s'", name, service->name);
        }
    }

    spinlock_release(&crt_lock);
    return rval;
}

bool runtime_create_monitor(const char *name, const char *module)
{
    spinlock_acquire(&crt_lock);
    bool rval = false;

    if (monitor_find(name) == NULL)
    {
        MXS_MONITOR *monitor = monitor_alloc((char*)name, (char*)module);

        if (monitor)
        {
            /** Mark that this monitor was created after MaxScale was started */
            monitor->created_online = true;
            add_monitor_defaults(monitor);

            if (monitor_serialize(monitor))
            {
                MXS_NOTICE("Created monitor '%s'", name);
                rval = true;
            }
        }
    }

    spinlock_release(&crt_lock);
    return rval;
}

bool runtime_destroy_monitor(MXS_MONITOR *monitor)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(), monitor->name);

    spinlock_acquire(&crt_lock);

    if (unlink(filename) == -1)
    {
        if (errno != ENOENT)
        {
            MXS_ERROR("Failed to remove persisted monitor configuration '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
        else
        {
            rval = false;
            MXS_WARNING("Monitor '%s' was not created at runtime. Remove the "
                        "monitor manually from the correct configuration file.",
                        monitor->name);
        }
    }
    else
    {
        rval = true;
    }

    if (rval)
    {
        monitorStop(monitor);

        while (monitor->databases)
        {
            monitorRemoveServer(monitor, monitor->databases->server);
        }
        MXS_NOTICE("Destroyed monitor '%s'. The monitor will be removed "
                   "after the next restart of MaxScale.", monitor->name);
    }

    spinlock_release(&crt_lock);
    return rval;
}

static bool extract_relations(json_t* json, set<string>& relations,
                              const char** relation_types,
                              bool (*relation_check)(const string&, const string&))
{
    bool rval = true;
    json_t* rel;

    if ((rel = json_object_get(json, CN_RELATIONSHIPS)))
    {
        for (int i = 0; relation_types[i]; i++)
        {
            json_t* arr = json_object_get(rel, relation_types[i]);

            if (arr)
            {
                size_t size = json_array_size(arr);

                for (size_t j = 0; j < size; j++)
                {
                    json_t* t = json_array_get(arr, j);

                    if (json_is_string(t))
                    {
                        string value = json_string_value(t);

                        // Remove the link part
                        size_t pos = value.find_last_of("/");
                        if (pos != string::npos)
                        {
                            value.erase(0, pos + 1);
                        }

                        if (relation_check(relation_types[i], value))
                        {
                            relations.insert(value);
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
        }
    }

    return rval;
}

static inline const char* string_or_null(json_t* json, const char* name)
{
    const char* rval = NULL;
    json_t* value = json_object_get(json, name);

    if (value && json_is_string(value))
    {
        rval = json_string_value(value);
    }

    return rval;
}

static bool server_contains_required_fields(json_t* json)
{
    json_t* value;

    return (value = json_object_get(json, CN_NAME)) && json_is_string(value) &&
           (value = json_object_get(json, CN_ADDRESS)) && json_is_string(value) &&
           (value = json_object_get(json, CN_PORT)) && json_is_integer(value);
}

const char* server_relation_types[] =
{
    CN_SERVICES,
    CN_MONITORS,
    NULL
};

static bool server_relation_is_valid(const string& type, const string& value)
{
    return (type == CN_SERVICES && service_find(value.c_str())) ||
           (type == CN_MONITORS && monitor_find(value.c_str()));
}

static bool unlink_server_from_objects(SERVER* server, set<string>& relations)
{
    bool rval = true;

    for (set<string>::iterator it = relations.begin(); it != relations.end(); it++)
    {
        if (!runtime_unlink_server(server, it->c_str()))
        {
            rval = false;
        }
    }

    return rval;
}

static bool link_server_to_objects(SERVER* server, set<string>& relations)
{
    bool rval = true;

    for (set<string>::iterator it = relations.begin(); it != relations.end(); it++)
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

SERVER* runtime_create_server_from_json(json_t* json)
{
    SERVER* rval = NULL;

    if (server_contains_required_fields(json))
    {
        const char* name = json_string_value(json_object_get(json, CN_NAME));
        const char* address = json_string_value(json_object_get(json, CN_ADDRESS));

        /** The port needs to be in string format */
        char port[200]; // Enough to store any port value
        int i = json_integer_value(json_object_get(json, CN_PORT));
        snprintf(port, sizeof (port), "%d", i);

        /** Optional parameters */
        const char* protocol = string_or_null(json, CN_PROTOCOL);
        const char* authenticator = string_or_null(json, CN_AUTHENTICATOR);
        const char* authenticator_options = string_or_null(json, CN_AUTHENTICATOR_OPTIONS);


        set<string> relations;

        if (extract_relations(json, relations, server_relation_types, server_relation_is_valid) &&
            runtime_create_server(name, address, port, protocol, authenticator, authenticator_options))
        {
            rval = server_find_by_unique_name(name);
            ss_dassert(rval);

            if (!link_server_to_objects(rval, relations))
            {
                runtime_destroy_server(rval);
                rval = NULL;
            }
        }
    }

    return rval;
}

bool server_to_object_relations(SERVER* server, json_t* old_json, json_t* new_json)
{
    bool rval = false;
    set<string> old_relations;
    set<string> new_relations;

    if (extract_relations(old_json, old_relations, server_relation_types, server_relation_is_valid) &&
        extract_relations(new_json, new_relations, server_relation_types, server_relation_is_valid))
    {
        set<string> removed_relations;
        set<string> added_relations;

        std::set_difference(old_relations.begin(), old_relations.end(),
                            new_relations.begin(), new_relations.end(),
                            std::inserter(removed_relations, removed_relations.begin()));

        std::set_difference(new_relations.begin(), new_relations.end(),
                            old_relations.begin(), old_relations.end(),
                            std::inserter(added_relations, added_relations.begin()));

        if (link_server_to_objects(server, added_relations) &&
            unlink_server_from_objects(server, removed_relations))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_alter_server_from_json(SERVER* server, json_t* new_json)
{
    bool rval = false;
    Closer<json_t*> old_json(server_to_json(server, ""));
    ss_dassert(old_json.get());

    if (server_to_object_relations(server, old_json.get(), new_json))
    {
        json_t* parameters = json_object_get(new_json, CN_PARAMETERS);
        json_t* old_parameters = json_object_get(old_json.get(), CN_PARAMETERS);;
        ss_dassert(old_parameters);

        if (parameters)
        {
            rval = true;
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

static bool monitor_contains_required_fields(json_t* json)
{
    json_t* value;

    return (value = json_object_get(json, CN_NAME)) && json_is_string(value) &&
           (value = json_object_get(json, CN_MODULE)) && json_is_string(value);
}

const char* object_relation_types[] =
{
    CN_SERVERS,
    NULL
};

static bool object_relation_is_valid(const string& type, const string& value)
{
    return type == CN_SERVERS && server_find_by_unique_name(value.c_str());
}

static bool unlink_object_from_servers(const char* target, set<string>& relations)
{
    bool rval = true;

    for (set<string>::iterator it = relations.begin(); it != relations.end(); it++)
    {
        SERVER* server = server_find_by_unique_name(it->c_str());

        if (!server || !runtime_unlink_server(server, target))
        {
            rval = false;
            break;
        }
    }

    return rval;
}

static bool link_object_to_servers(const char* target, set<string>& relations)
{
    bool rval = true;

    for (set<string>::iterator it = relations.begin(); it != relations.end(); it++)
    {
        SERVER* server = server_find_by_unique_name(it->c_str());

        if (!server || !runtime_link_server(server, target))
        {
            unlink_server_from_objects(server, relations);
            rval = false;
            break;
        }
    }

    return rval;
}

MXS_MONITOR* runtime_create_monitor_from_json(json_t* json)
{
    MXS_MONITOR* rval = NULL;

    if (monitor_contains_required_fields(json))
    {
        const char* name = json_string_value(json_object_get(json, CN_NAME));
        const char* module = json_string_value(json_object_get(json, CN_MODULE));

        set<string> relations;

        if (extract_relations(json, relations, object_relation_types, object_relation_is_valid) &&
            runtime_create_monitor(name, module))
        {
            rval = monitor_find(name);
            ss_dassert(rval);

            if (!link_object_to_servers(rval->name, relations))
            {
                runtime_destroy_monitor(rval);
                rval = NULL;
            }
        }
    }

    return rval;
}

bool object_to_server_relations(const char* target, json_t* old_json, json_t* new_json)
{
    bool rval = false;
    set<string> old_relations;
    set<string> new_relations;

    if (extract_relations(old_json, old_relations, object_relation_types, object_relation_is_valid) &&
        extract_relations(new_json, new_relations, object_relation_types, object_relation_is_valid))
    {
        set<string> removed_relations;
        set<string> added_relations;

        std::set_difference(old_relations.begin(), old_relations.end(),
                            new_relations.begin(), new_relations.end(),
                            std::inserter(removed_relations, removed_relations.begin()));

        std::set_difference(new_relations.begin(), new_relations.end(),
                            old_relations.begin(), old_relations.end(),
                            std::inserter(added_relations, added_relations.begin()));

        if (link_object_to_servers(target, added_relations) &&
            unlink_object_from_servers(target, removed_relations))
        {
            rval = true;
        }
    }

    return rval;
}

bool runtime_alter_monitor_from_json(MXS_MONITOR* monitor, json_t* new_json)
{
    bool rval = false;
    Closer<json_t*> old_json(monitor_to_json(monitor, ""));
    ss_dassert(old_json.get());

    if (object_to_server_relations(monitor->name, old_json.get(), new_json))
    {
        bool changed = false;
        json_t* parameters = json_object_get(new_json, CN_PARAMETERS);
        json_t* old_parameters = json_object_get(old_json.get(), CN_PARAMETERS);;
        ss_dassert(old_parameters);

        if (parameters)
        {
            rval = true;
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
                else if (runtime_alter_monitor(monitor, key, mxs::json_to_string(value).c_str()))
                {
                    changed = true;
                }
                else
                {
                    rval = false;
                }
            }
        }

        if (changed)
        {
            /** A configuration change was made, restart the monitor */
            monitorStop(monitor);
            monitorStart(monitor, monitor->parameters);
        }
    }

    return rval;
}
