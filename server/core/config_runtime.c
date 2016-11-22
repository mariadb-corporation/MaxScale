/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/atomic.h>
#include <maxscale/config_runtime.h>
#include <maxscale/gwdirs.h>
#include <maxscale/spinlock.h>

static SPINLOCK crt_lock = SPINLOCK_INIT;

bool runtime_link_server(SERVER *server, const char *target)
{
    spinlock_acquire(&crt_lock);

    bool rval = false;
    SERVICE *service = service_find(target);
    MONITOR *monitor = service ? NULL : monitor_find(target);

    if (service || monitor)
    {
        rval = true;

        if (service)
        {
            serviceAddBackend(service, server);
            service_serialize_servers(service);
        }
        else if (monitor)
        {
            monitorAddServer(monitor, server);
            monitor_serialize_servers(monitor);
        }

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
    MONITOR *monitor = service ? NULL : monitor_find(target);

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

bool runtime_enable_server_ssl(SERVER *server, const char *key, const char *cert,
                               const char *ca, const char *version, const char *depth)
{
    spinlock_acquire(&crt_lock);
    bool rval = false;

    if (key && cert && ca)
    {
        CONFIG_CONTEXT *obj = config_context_create(server->unique_name);

        if (obj && config_add_param(obj, "ssl_key", key) &&
            config_add_param(obj, "ssl_cert", cert) &&
            config_add_param(obj, "ssl_ca_cert", ca) &&
            (!version || config_add_param(obj, "ssl_version", version)) &&
            (!depth || config_add_param(obj, "ssl_cert_verify_depth", depth)))
        {
            int err = 0;
            SSL_LISTENER *ssl = make_ssl_structure(obj, true, &err);

            if (err == 0 && ssl && listener_init_SSL(ssl) == 0)
            {
                /** Sync to prevent reads on partially initialized server_ssl */
                atomic_synchronize();

                server->server_ssl = ssl;
                if (server_serialize(server))
                {
                    rval = true;
                }
            }
        }

        config_context_free(obj);
    }

    spinlock_release(&crt_lock);
    return rval;
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
    else if (strcmp(key, "monuser") == 0)
    {
        server_update_credentials(server, value, server->monpw);
    }
    else if (strcmp(key, "monpw") == 0)
    {
        server_update_credentials(server, server->monuser, value);
    }
    else
    {
        valid = false;
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

bool runtime_alter_monitor(MONITOR *monitor, char *key, char *value)
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

    spinlock_release(&crt_lock);
    return valid;
}
