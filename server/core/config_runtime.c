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
#include "maxscale/config_runtime.h"

#include <strings.h>

#include <maxscale/atomic.h>
#include <maxscale/paths.h>
#include <maxscale/spinlock.h>

#include "maxscale/config.h"
#include "maxscale/monitor.h"
#include "maxscale/modules.h"
#include "maxscale/service.h"

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
            monitor_serialize(monitor);
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
            monitor_serialize(monitor);
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
                char err[MXS_STRERROR_BUFLEN];
                MXS_ERROR("Failed to remove persisted server configuration '%s': %d, %s",
                          filename, errno, strerror_r(errno, err, sizeof(err)));
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
                                const char *ca, const char *version, const char *depth,
                                const char *verify)
{
    SSL_LISTENER *rval = NULL;
    CONFIG_CONTEXT *obj = config_context_create(name);

    if (obj)
    {
        if (config_add_param(obj, "ssl", "required") &&
            config_add_param(obj, "ssl_key", key) &&
            config_add_param(obj, "ssl_cert", cert) &&
            config_add_param(obj, "ssl_ca_cert", ca) &&
            (!version || config_add_param(obj, "ssl_version", version)) &&
            (!depth || config_add_param(obj, "ssl_cert_verify_depth", depth)) &&
            (!verify || config_add_param(obj, "ssl_verify_peer_certificate", verify)))
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
                               const char *ca, const char *version, const char *depth,
                               const char *verify)
{
    bool rval = false;

    if (key && cert && ca)
    {
        spinlock_acquire(&crt_lock);
        SSL_LISTENER *ssl = create_ssl(server->unique_name, key, cert, ca, version, depth, verify);

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

static inline bool is_valid_integer(const char* value)
{
    char* endptr;
    return strtol(value, &endptr, 10) >= 0 && *value && *endptr == '\0';
}

bool runtime_alter_server(SERVER *server, char *key, char *value)
{
    spinlock_acquire(&crt_lock);
    bool valid = true;

    if (strcmp(key, "address") == 0)
    {
        server_update_address(server, value);
    }
    else if (strcmp(key, "port") == 0)
    {
        server_update_port(server, atoi(value));
    }
    else if (strcmp(key, "monuser") == 0 || strcmp(key, "monitoruser") == 0)
    {
        if (strcmp(key, "monuser") == 0)
        {
            MXS_WARNING("Use of `monuser` is deprecated, use `monitoruser` instead.");
        }

        server_update_credentials(server, value, server->monpw);
    }
    else if (strcmp(key, "monpw") == 0 || strcmp(key, "monitorpw") == 0)
    {
        if (strcmp(key, "monpw") == 0)
        {
            MXS_WARNING("Use of `monpw` is deprecated, use `monitorpw` instead.");
        }

        server_update_credentials(server, server->monuser, value);
    }
    else if (strcmp(key, "persistpoolmax") == 0)
    {
        if (is_valid_integer(value))
        {
            server->persistpoolmax = atoi(value);
        }
        else
        {
            valid = false;
        }
    }
    else if (strcmp(key, "persistmaxtime") == 0 && is_valid_integer(value))
    {
        if (is_valid_integer(value))
        {
            server->persistmaxtime = atoi(value);
        }
        else
        {
            valid = false;
        }
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
    CONFIG_CONTEXT ctx = {.object = ""};
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

bool runtime_alter_monitor(MXS_MONITOR *monitor, char *key, char *value)
{
    spinlock_acquire(&crt_lock);
    bool valid = false;

    if (strcmp(key, "user") == 0)
    {
        valid = true;
        monitorAddUser(monitor, value, monitor->password);
    }
    else if (strcmp(key, "password") == 0)
    {
        valid = true;
        monitorAddUser(monitor, monitor->user, value);
    }
    else if (strcmp(key, "monitor_interval") == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetInterval(monitor, ival);
        }
    }
    else if (strcmp(key, "backend_connect_timeout") == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_CONNECT_TIMEOUT, ival);
        }
    }
    else if (strcmp(key, "backend_write_timeout") == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_WRITE_TIMEOUT, ival);
        }
    }
    else if (strcmp(key, "backend_read_timeout") == 0)
    {
        long ival = get_positive_int(value);
        if (ival)
        {
            valid = true;
            monitorSetNetworkTimeout(monitor, MONITOR_READ_TIMEOUT, ival);
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
                MXS_CONFIG_PARAMETER p = {.name = key, .value = value};
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
                             const char *ssl_version, const char *ssl_depth,
                             const char *verify_ssl)
{

    if (addr == NULL || strcasecmp(addr, "default") == 0)
    {
        addr = "::";
    }
    if (port == NULL || strcasecmp(port, "default") == 0)
    {
        port = "3306";
    }
    if (proto == NULL || strcasecmp(proto, "default") == 0)
    {
        proto = "MySQLClient";
    }

    if (auth && strcasecmp(auth, "default") == 0)
    {
        /** Set auth to NULL so the protocol default authenticator is used */
        auth = NULL;
    }

    if (auth_opt && strcasecmp(auth_opt, "default") == 0)
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
            ssl = create_ssl(name, ssl_key, ssl_cert, ssl_ca, ssl_version, ssl_depth, verify_ssl);

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
            char err[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Failed to remove persisted listener configuration '%s': %d, %s",
                      filename, errno, strerror_r(errno, err, sizeof(err)));
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
            char err[MXS_STRERROR_BUFLEN];
            MXS_ERROR("Failed to remove persisted monitor configuration '%s': %d, %s",
                      filename, errno, strerror_r(errno, err, sizeof(err)));
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
