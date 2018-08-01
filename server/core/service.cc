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

/**
 * @file service.c  - A representation of a service within MaxScale
 */

#include <maxscale/cppdefs.hh>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <fcntl.h>
#include <atomic>
#include <map>
#include <string>
#include <set>
#include <vector>
#include <unordered_set>

#include <maxscale/service.h>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/paths.h>
#include <maxscale/housekeeper.h>
#include <maxscale/listener.h>
#include <maxscale/log_manager.h>
#include <maxscale/poll.h>
#include <maxscale/protocol.h>
#include <maxscale/resultset.hh>
#include <maxscale/router.h>
#include <maxscale/server.h>
#include <maxscale/session.h>
#include <maxscale/spinlock.h>
#include <maxscale/users.h>
#include <maxscale/utils.h>
#include <maxscale/utils.hh>
#include <maxscale/version.h>
#include <maxscale/jansson.h>
#include <maxscale/json_api.h>
#include <maxscale/routingworker.h>

#include "internal/config.hh"
#include "internal/filter.hh"
#include "internal/modules.h"
#include "internal/service.hh"
#include "internal/routingworker.hh"
#include "internal/maxscale.h"

/** This define is needed in CentOS 6 systems */
#if !defined(UINT64_MAX)
#define UINT64_MAX      (18446744073709551615UL)
#endif

using std::string;
using std::set;
using namespace maxscale;

/** Base value for server weights */
#define SERVICE_BASE_SERVER_WEIGHT 1000

static SPINLOCK service_spin = SPINLOCK_INIT;
static std::vector<Service*> allServices;

static bool service_internal_restart(void *data);
static void service_calculate_weights(SERVICE *service);

Service* service_alloc(const char *name, const char *router, MXS_CONFIG_PARAMETER* params)
{
    MXS_ROUTER_OBJECT* router_api = (MXS_ROUTER_OBJECT*)load_module(router, MODULE_ROUTER);

    if (router_api == NULL)
    {
        MXS_ERROR("Unable to load router module '%s'", router);
        return NULL;
    }

    char *my_name = MXS_STRDUP(name);
    char *my_router = MXS_STRDUP(router);
    Service* service = new (std::nothrow) Service;
    SERVICE_REFRESH_RATE* rate_limits = (SERVICE_REFRESH_RATE*)MXS_CALLOC(config_threadcount(),
                                                                         sizeof(*rate_limits));
    if (!my_name || !my_router || !service || !rate_limits)
    {
        MXS_FREE(my_name);
        MXS_FREE(my_router);
        MXS_FREE(rate_limits);
        delete service;
        return NULL;
    }

    const MXS_MODULE* module = get_module(my_router, MODULE_ROUTER);
    ss_dassert(module);

    service->router = router_api;
    service->capabilities = module->module_capabilities;
    service->client_count = 0;
    service->n_dbref = 0;
    service->name = my_name;
    service->routerModule = my_router;
    service->svc_config_param = NULL;
    service->svc_config_version = 0;
    service->rate_limits = rate_limits;
    service->stats.started = time(0);
    service->stats.n_failed_starts = 0;
    service->stats.n_current = 0;
    service->stats.n_sessions = 0;
    service->state = SERVICE_STATE_ALLOC;
    service->active = true;
    service->ports = NULL;
    service->dbref = NULL;
    service->n_dbref = 0;
    service->filters = NULL;
    service->n_filters = 0;
    service->weightby[0] = '\0';
    spinlock_init(&service->spin);

    service->max_retry_interval = config_get_integer(params, CN_MAX_RETRY_INTERVAL);
    service->users_from_all = config_get_bool(params, CN_AUTH_ALL_SERVERS);
    service->localhost_match_wildcard_host = config_get_bool(params, CN_LOCALHOST_MATCH_WILDCARD_HOST);
    service->retry_start = config_get_bool(params, CN_RETRY_ON_FAILURE);
    service->enable_root = config_get_bool(params, CN_ENABLE_ROOT_USER);
    service->conn_idle_timeout = config_get_integer(params, CN_CONNECTION_TIMEOUT);
    service->max_connections = config_get_integer(params, CN_MAX_CONNECTIONS);
    service->log_auth_warnings = config_get_bool(params, CN_LOG_AUTH_WARNINGS);
    service->strip_db_esc = config_get_bool(params, CN_STRIP_DB_ESC);
    service->session_track_trx_state = config_get_bool(params, CN_SESSION_TRACK_TRX_STATE);

    serviceWeightBy(service, config_get_string(params, CN_WEIGHTBY));
    serviceSetUser(service, config_get_string(params, CN_USER),
                   config_get_string(params, CN_PASSWORD));

    std::string version_string = config_get_string(params, CN_VERSION_STRING);

    if (!version_string.empty())
    {
        /** Add the 5.5.5- string to the start of the version string if
         * the version string starts with "10.".
         * This mimics MariaDB 10.0 replication which adds 5.5.5- for backwards compatibility. */
        if (version_string[0] != '5')
        {
            version_string = "5.5.5-" + version_string;
        }

        serviceSetVersionString(service, version_string.c_str());
    }
    else if (config_get_global_options()->version_string)
    {
        serviceSetVersionString(service, config_get_global_options()->version_string);
    }

    if (service->conn_idle_timeout)
    {
        dcb_enable_session_timeouts();
    }

    // Store parameters in the service
    service_add_parameters(service, params);

    service->router_instance = router_api->createInstance(service, params);

    if (service->router_instance == NULL)
    {
        MXS_ERROR("%s: Failed to create router instance. Service not started.", service->name);
        service->active = false;
        service_free(service);
        return NULL;
    }

    if (router_api->getCapabilities)
    {
        service->capabilities |= router_api->getCapabilities(service->router_instance);
    }

    spinlock_acquire(&service_spin);
    allServices.push_back(service);
    spinlock_release(&service_spin);

    return service;
}

void service_free(Service* service)
{
    ss_dassert(atomic_load_int(&service->client_count) == 0);
    ss_dassert(!service->active || maxscale_teardown_in_progress());

    spinlock_acquire(&service_spin);
    auto it = std::remove(allServices.begin(), allServices.end(), service);
    allServices.erase(it);
    spinlock_release(&service_spin);

    while (service->ports)
    {
        auto tmp = service->ports;
        service->ports = service->ports->next;
        ss_dassert(!tmp->active || maxscale_teardown_in_progress());
        listener_free(tmp);
    }

    if (service->router && service->router_instance && service->router->destroyInstance)
    {
        service->router->destroyInstance(service->router_instance);
    }

    while (service->dbref)
    {
        SERVER_REF* tmp = service->dbref;
        ss_dassert(!tmp->active || maxscale_teardown_in_progress());
        service->dbref = service->dbref->next;
        MXS_FREE(tmp);
    }

    config_parameter_free(service->svc_config_param);
    MXS_FREE(service->name);
    MXS_FREE(service->routerModule);
    MXS_FREE(service->filters);
    MXS_FREE(service->rate_limits);
    delete service;
}

void service_destroy(Service* service)
{
#ifdef SS_DEBUG
    auto current = mxs::RoutingWorker::get_current();
    auto main = mxs::RoutingWorker::get(mxs::RoutingWorker::MAIN);
    ss_info_dassert(current == main, "Destruction of service must be done on the main worker");
#endif

    ss_dassert(service->active);
    atomic_store_int(&service->active, false);

    char filename[PATH_MAX + 1];
    snprintf(filename, sizeof(filename), "%s/%s.cnf", get_config_persistdir(),
             service->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove persisted service configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }

    if (atomic_load_int(&service->client_count) == 0)
    {
        // The service has no active sessions, it can be closed immediately
        service_free(service);
    }
}

/**
 * Check to see if a service pointer is valid
 *
 * @param service       The pointer to check
 * @return 1 if the service is in the list of all services
 */
bool service_isvalid(Service *service)
{
    bool rval = false;

    spinlock_acquire(&service_spin);

    for (Service* s: allServices)
    {
        if (s == service)
        {
            rval = true;
            break;
        }
    }

    spinlock_release(&service_spin);

    return rval;
}

static inline void close_port(SERV_LISTENER *port)
{
    if (port->service->state == SERVICE_STATE_ALLOC)
    {
        /** The service failed when it was being allocated */
        port->service->state = SERVICE_STATE_FAILED;
    }

    if (port->listener)
    {
        dcb_close(port->listener);
        port->listener = NULL;
    }
}

/**
 * Start an individual port/protocol pair
 *
 * @param service       The service
 * @param port          The port to start
 * @return              The number of listeners started
 */
static int
serviceStartPort(Service *service, SERV_LISTENER *port)
{
    const size_t ANY_IPV4_ADDRESS_LEN = 7; // strlen("0:0:0:0");

    int listeners = 0;
    size_t config_bind_len =
        (port->address ? strlen(port->address) : ANY_IPV4_ADDRESS_LEN) + 1 + UINTLEN(port->port);
    char config_bind[config_bind_len + 1]; // +1 for NULL
    MXS_PROTOCOL *funcs;

    if (service == NULL || service->router == NULL || service->router_instance == NULL)
    {
        /* Should never happen, this guarantees it can't */
        MXS_ERROR("Attempt to start port with null or incomplete service");
        close_port(port);
        ss_dassert(false);
        return 0;
    }

    port->listener = dcb_alloc(DCB_ROLE_SERVICE_LISTENER, port);

    if (port->listener == NULL)
    {
        MXS_ERROR("Failed to create listener for service %s.", service->name);
        close_port(port);
        return 0;
    }

    port->listener->service = service;

    if ((funcs = (MXS_PROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) == NULL)
    {
        MXS_ERROR("Unable to load protocol module %s. Listener for service %s not started.",
                  port->protocol, service->name);
        close_port(port);
        return 0;
    }

    memcpy(&(port->listener->func), funcs, sizeof(MXS_PROTOCOL));

    const char *authenticator_name = "NullAuthDeny";

    if (port->authenticator)
    {
        authenticator_name = port->authenticator;
    }
    else if (port->listener->func.auth_default)
    {
        authenticator_name = port->listener->func.auth_default();
    }

    MXS_AUTHENTICATOR *authfuncs = (MXS_AUTHENTICATOR *)load_module(authenticator_name, MODULE_AUTHENTICATOR);

    if (authfuncs == NULL)
    {
        MXS_ERROR("Failed to load authenticator module '%s' for listener '%s'",
                  authenticator_name, port->name);
        close_port(port);
        return 0;
    }

    // Add protocol and authenticator capabilities from the listener
    const MXS_MODULE* proto_mod = get_module(port->protocol, MODULE_PROTOCOL);
    const MXS_MODULE* auth_mod = get_module(authenticator_name, MODULE_AUTHENTICATOR);
    ss_dassert(proto_mod && auth_mod);
    service->capabilities |= proto_mod->module_capabilities | auth_mod->module_capabilities;

    memcpy(&port->listener->authfunc, authfuncs, sizeof(MXS_AUTHENTICATOR));

    /**
     * Normally, we'd allocate the DCB specific authentication data. As the
     * listeners aren't normal DCBs, we can skip that.
     */

    if (port->address)
    {
        sprintf(config_bind, "%s|%d", port->address, port->port);
    }
    else
    {
        sprintf(config_bind, "::|%d", port->port);
    }

    /** Load the authentication users before before starting the listener */
    if (port->listener->authfunc.loadusers)
    {
        switch (port->listener->authfunc.loadusers(port))
        {
        case MXS_AUTH_LOADUSERS_FATAL:
            MXS_ERROR("[%s] Fatal error when loading users for listener '%s', "
                      "service is not started.", service->name, port->name);
            close_port(port);
            return 0;

        case MXS_AUTH_LOADUSERS_ERROR:
            MXS_WARNING("[%s] Failed to load users for listener '%s', authentication"
                        " might not work.", service->name, port->name);
            break;

        default:
            break;
        }
    }

    MXS_CONFIG* config = config_get_global_options();
    time_t last;
    bool warned;

    /**
     * At service start last update is set to config->users_refresh_time seconds earlier.
     * This way MaxScale could try reloading users just after startup. But only if user
     * refreshing has not been turned off.
     */
    if (config->users_refresh_time == INT32_MAX)
    {
        last = time(NULL);
        warned = true;  // So that there will not be a refresh rate warning.
    }
    else
    {
        last = time(NULL) - config->users_refresh_time;
        warned = false;
    }

    int nthreads = config_threadcount();

    for (int i = 0; i < nthreads; ++i)
    {
        service->rate_limits[i].last = last;
        service->rate_limits[i].warned = warned;
    }

    if (port->listener->func.listen(port->listener, config_bind))
    {
        port->listener->session = session_alloc(service, port->listener);

        if (port->listener->session != NULL)
        {
            port->listener->session->state = SESSION_STATE_LISTENER;
            listeners += 1;
        }
        else
        {
            MXS_ERROR("[%s] Failed to create listener session.", service->name);
            close_port(port);
        }
    }
    else
    {
        MXS_ERROR("[%s] Failed to listen on %s", service->name, config_bind);
        close_port(port);
    }

    return listeners;
}

/**
 * Start all ports for a service.
 * serviceStartAllPorts will try to start all listeners associated with the service.
 * If no listeners are started, the starting of ports will be retried after a period of time.
 * @param service Service to start
 * @return Number of started listeners. This is equal to the number of ports the service
 * is listening to.
 */
int serviceStartAllPorts(Service* service)
{
    SERV_LISTENER *port = service->ports;
    int listeners = 0;

    if (port)
    {
        while (!service_should_stop && port)
        {
            listeners += serviceStartPort(service, port);
            port = port->next;
        }

        if (service->state == SERVICE_STATE_FAILED)
        {
            listeners = 0;
        }
        else if (listeners)
        {
            service->state = SERVICE_STATE_STARTED;
            service->stats.started = time(0);
        }
        else if (service->retry_start)
        {
            /** Service failed to start any ports. Try again later. */
            service->stats.n_failed_starts++;
            char taskname[strlen(service->name) + strlen("_start_retry_") +
                                                (int) ceil(log10(INT_MAX)) + 1];
            int retry_after = MXS_MIN(service->stats.n_failed_starts * 10, service->max_retry_interval);
            snprintf(taskname, sizeof(taskname), "%s_start_retry_%d",
                     service->name, service->stats.n_failed_starts);
            hktask_add(taskname, service_internal_restart, service, retry_after);
            MXS_NOTICE("Failed to start service %s, retrying in %d seconds.",
                       service->name, retry_after);

            /** This will prevent MaxScale from shutting down if service start is retried later */
            listeners = 1;
        }
    }
    else
    {
        MXS_WARNING("Service '%s' has no listeners defined.", service->name);
        listeners = 1; /** Set this to one to suppress errors */
    }

    return listeners;
}

/**
 * Start a service
 *
 * This function loads the protocol modules for each port on which the
 * service listens and starts the listener on that port
 *
 * @param service The Service that should be started
 *
 * @return Returns the number of listeners created
 */
int serviceInitialize(Service *service)
{
    /** Calculate the server weights */
    service_calculate_weights(service);

    int listeners = 0;

    if (!config_get_global_options()->config_check)
    {
        listeners = serviceStartAllPorts(service);
    }
    else
    {
        /** We're only checking that the configuration is valid */
        listeners++;
    }

    return listeners;
}

bool serviceLaunchListener(Service *service, SERV_LISTENER *port)
{
    ss_dassert(service->state != SERVICE_STATE_FAILED);
    bool rval = true;

    spinlock_acquire(&service->spin);

    if (serviceStartPort(service, port) == 0)
    {
        /** Failed to start the listener */
        service_remove_listener(service, port->name);
        rval = false;
    }

    spinlock_release(&service->spin);

    return rval;
}

bool serviceStopListener(SERVICE *svc, const char *name)
{
    Service* service = static_cast<Service*>(svc);
    bool rval = false;
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && strcmp(listener->name, name) == 0)
        {
            if (poll_remove_dcb(listener->listener) == 0)
            {
                listener->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
                rval = true;
            }
            break;
        }
    }

    return rval;
}

bool serviceStartListener(SERVICE *svc, const char *name)
{
    Service* service = static_cast<Service*>(svc);
    bool rval = false;
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && strcmp(listener->name, name) == 0)
        {
            if (listener->listener && listener->listener->session->state == SESSION_STATE_LISTENER_STOPPED &&
                poll_add_dcb(listener->listener) == 0)
            {
                listener->listener->session->state = SESSION_STATE_LISTENER;
                rval = true;
            }
            break;
        }
    }

    return rval;
}

int service_launch_all()
{
    int n = 0, i;
    bool error = false;
    int num_svc = allServices.size();

    MXS_NOTICE("Starting a total of %d services...", num_svc);

    int curr_svc = 1;
    for (Service* ptr: allServices)
    {
        n += (i = serviceInitialize(ptr));
        MXS_NOTICE("Service '%s' started (%d/%d)", ptr->name, curr_svc++, num_svc);

        if (i == 0)
        {
            MXS_ERROR("Failed to start service '%s'.", ptr->name);
            error = true;
        }

        if (service_should_stop)
        {
            break;
        }
    }

    return error ? -1 : n;
}

bool serviceStop(SERVICE *service)
{
    int listeners = 0;

    if (service)
    {
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
             listener; listener = listener_iterator_next(&iter))
        {
            if (listener_is_active(listener) &&
                listener->listener && listener->listener->session->state == SESSION_STATE_LISTENER)
            {
                if (poll_remove_dcb(listener->listener) == 0)
                {
                    listener->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
                    listeners++;
                }
            }
        }

        service->state = SERVICE_STATE_STOPPED;
    }

    return listeners > 0;
}

/**
 * Restart a service
 *
 * This function stops the listener for the service
 *
 * @param service       The Service that should be restarted
 * @return      Returns the number of listeners restarted
 */
bool serviceStart(SERVICE *service)
{
    int listeners = 0;

    if (service)
    {
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
             listener; listener = listener_iterator_next(&iter))
        {
            if (listener_is_active(listener) &&
                listener->listener && listener->listener->session->state == SESSION_STATE_LISTENER_STOPPED)
            {
                if (poll_add_dcb(listener->listener) == 0)
                {
                    listener->listener->session->state = SESSION_STATE_LISTENER;
                    listeners++;
                }
            }
        }

        service->state = SERVICE_STATE_STARTED;
    }

    return listeners > 0;
}

/**
 * Add a listener to a service
 *
 * @param service Service where listener is added
 * @param proto   Listener to add
 */
static void service_add_listener(SERVICE* service, SERV_LISTENER* proto)
{
    do
    {
        /** Read the current value of the list's head. This will be our expected
         * value for the following compare-and-swap operation. */
        proto->next = (SERV_LISTENER*)atomic_load_ptr((void**)&service->ports);
    }
    /** Compare the current value to our expected value and if they match, replace
     * the current value with our new value. */
    while (!atomic_cas_ptr((void**)&service->ports, (void**)&proto->next, proto));
}

bool service_remove_listener(Service *service, const char* target)
{
    bool rval = false;
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && strcmp(listener->name, target) == 0)
        {
            listener_set_active(listener, false);

            if (poll_remove_dcb(listener->listener) == 0)
            {
                listener->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
                rval = true;
            }
            break;
        }
    }

    return rval;
}

/**
 * Create a listener for the service
 *
 * @param service       The service
 * @param protocol      The name of the protocol module
 * @param address       The address to listen with
 * @param port          The port to listen on
 * @param authenticator Name of the authenticator to be used
 * @param ssl           SSL configuration
 *
 * @return Created listener or NULL on error
 */
SERV_LISTENER* serviceCreateListener(Service *service, const char *name, const char *protocol,
                                     const char *address, unsigned short port, const char *authenticator,
                                     const char *options, SSL_LISTENER *ssl)
{
    SERV_LISTENER *proto = listener_alloc(service, name, protocol, address,
                                          port, authenticator, options, ssl);

    if (proto)
    {
        service_add_listener(service, proto);
    }

    return proto;
}

SERV_LISTENER* service_find_listener(Service* service,
                                     const char* socket,
                                     const char* address, unsigned short port)
{
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener))
        {
            bool is_same_port = false;

            if (port && (port == listener->port) &&
                ((address && listener->address && strcmp(listener->address, address) == 0) ||
                 (address == NULL && listener->address == NULL)))
            {
                is_same_port = true;
            }

            bool is_same_socket = false;

            if (!is_same_port)
            {
                if (socket && listener->address && strcmp(listener->address, socket) == 0)
                {
                    is_same_socket = true;
                }
            }

            if (is_same_port || is_same_socket)
            {
                return listener;
            }
        }
    }

    return NULL;
}

/**
 * Check if a protocol/port pair is part of the service
 *
 * @param service       The service
 * @param protocol      The name of the protocol module
 * @param address       The address to listen on
 * @param port          The port to listen on
 * @return      True if the protocol/port is already part of the service
 */
bool serviceHasListener(Service* service, const char* name, const char* protocol,
                        const char* address, unsigned short port)
{
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) &&
            // Listener with same name exists
            (strcmp(listener->name, name) == 0 ||
             // Listener listening on the same interface and port exists
             ((strcmp(listener->protocol, protocol) == 0 && listener->port == port &&
               ((address && listener->address && strcmp(listener->address, address) == 0) ||
                (address == NULL && listener->address == NULL))))))
        {
            return true;
        }
    }

    return false;
}

bool service_has_named_listener(Service *service, const char *name)
{
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && strcmp(listener->name, name) == 0)
        {
            return true;
        }
    }

    return false;
}

bool service_can_be_destroyed(Service *service)
{
    bool rval = true;
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener))
        {
            rval = false;
            break;
        }
    }

    if (rval)
    {
        for (auto s = service->dbref; s; s = s->next)
        {
            if (s->active)
            {
                rval = false;
                break;
            }
        }
    }

    return rval;
}

/**
 * Allocate a new server reference
 *
 * @param server Server to refer to
 * @return Server reference or NULL on error
 */
static SERVER_REF* server_ref_create(SERVER *server)
{
    SERVER_REF *sref = (SERVER_REF*)MXS_MALLOC(sizeof(SERVER_REF));

    if (sref)
    {
        sref->next = NULL;
        sref->server = server;
        sref->weight = SERVICE_BASE_SERVER_WEIGHT;
        sref->connections = 0;
        sref->active = true;
    }

    return sref;
}

/**
 * Add a backend database server to a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 */
bool serviceAddBackend(SERVICE *svc, SERVER *server)
{
    Service* service = static_cast<Service*>(svc);
    bool rval = false;

    if (!serviceHasBackend(service, server))
    {
        SERVER_REF *new_ref = server_ref_create(server);

        if (new_ref)
        {
            rval = true;
            spinlock_acquire(&service->spin);

            service->n_dbref++;

            if (service->dbref)
            {
                SERVER_REF *ref = service->dbref;
                SERVER_REF *prev = ref;

                while (ref)
                {
                    if (ref->server == server)
                    {
                        ref->active = true;
                        break;
                    }
                    prev = ref;
                    ref = ref->next;
                }

                if (ref == NULL)
                {
                    /** A new server that hasn't been used by this service */
                    atomic_synchronize();
                    prev->next = new_ref;
                }
                else
                {
                    MXS_FREE(new_ref);
                }
            }
            else
            {
                atomic_synchronize();
                service->dbref = new_ref;
            }
            spinlock_release(&service->spin);
        }
    }

    return rval;
}

/**
 * @brief Remove a server from a service
 *
 * This function sets the server reference into an inactive state. This does not
 * remove the server from the list or free any of the memory.
 *
 * @param service Service to modify
 * @param server  Server to remove
 */
void serviceRemoveBackend(Service *service, const SERVER *server)
{
    spinlock_acquire(&service->spin);

    for (SERVER_REF *ref = service->dbref; ref; ref = ref->next)
    {
        if (ref->server == server && ref->active)
        {
            ref->active = false;
            service->n_dbref--;
            break;
        }
    }

    spinlock_release(&service->spin);
}

/**
 * Test if a server is part of a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 * @return              Non-zero if the server is already part of the service
 */
bool serviceHasBackend(Service *service, SERVER *server)
{
    SERVER_REF *ptr;

    spinlock_acquire(&service->spin);
    ptr = service->dbref;
    while (ptr)
    {
        if (ptr->server == server && ptr->active)
        {
            break;
        }
        ptr = ptr->next;
    }
    spinlock_release(&service->spin);

    return ptr != NULL;
}

/**
 * Set the service user that is used to log in to the backebd servers
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param user          The user name to use for connections
 * @param auth          The authentication data we need, e.g. MySQL SHA1 password
 * @return      0 on failure
 */
int serviceSetUser(Service *service, const char *user, const char *auth)
{
    if (service->credentials.name != user)
    {
        snprintf(service->credentials.name,
                 sizeof(service->credentials.name), "%s", user);
    }

    if (service->credentials.authdata != auth)
    {
        snprintf(service->credentials.authdata,
                 sizeof(service->credentials.authdata), "%s", auth);
    }

    return 1;
}


/**
 * Get the service user that is used to log in to the backebd servers
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param user          The user name to use for connections
 * @param auth          The authentication data we need, e.g. MySQL SHA1 password
 * @return              0 on failure
 */
int
serviceGetUser(SERVICE *svc, char **user, char **auth)
{
    Service* service = static_cast<Service*>(svc);
    if (service->credentials.name == NULL || service->credentials.authdata == NULL)
    {
        return 0;
    }
    *user = service->credentials.name;
    *auth = service->credentials.authdata;
    return 1;
}

/**
 * Enable/Disable root user for this service
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param action        1 for root enable, 0 for disable access
 * @return              0 on failure
 */

int serviceEnableRootUser(Service *svc, int action)
{
    Service* service = static_cast<Service*>(svc);

    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->enable_root = action;

    return 1;
}

/**
 * Enable/Disable loading the user data from only one server or all of them
 *
 * @param service       The service we are setting the data for
 * @param action        1 for all servers, 0 for single server
 * @return              0 on failure
 */

int
serviceAuthAllServers(Service *service, int action)
{
    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->users_from_all = action;

    return 1;
}

/**
 * Whether to strip escape characters from the name of the database the client
 * is connecting to.
 * @param service Service to configure
 * @param action 0 for disabled, 1 for enabled
 * @return 1 if successful, 0 on error
 */
int serviceStripDbEsc(Service* service, int action)
{
    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->strip_db_esc = action;

    return 1;
}


/**
 * Sets the session timeout for the service.
 * @param service Service to configure
 * @param val Timeout in seconds
 * @return 1 on success, 0 when the value is invalid
 */
int
serviceSetTimeout(Service *service, int val)
{

    if (val < 0)
    {
        return 0;
    }

    /** Enable the session timeout checks if and only if at least one service is
     * configured with a idle timeout. */
    if ((service->conn_idle_timeout = val))
    {
        dcb_enable_session_timeouts();
    }

    return 1;
}

void serviceSetVersionString(Service *service, const char* value)
{
    if (service->version_string != value)
    {
        snprintf(service->version_string, sizeof(service->version_string), "%s", value);
    }
}

/**
 * Sets the connection limits, if any, for the service.
 * @param service Service to configure
 * @param max The maximum number of client connections at any one time
 * @param queued    The maximum number of connections to queue up when
 *                  max_connections clients are already connected
 * @param timeout   Maximum amount of time to wait for a connection to
 *                  become available.
 * @return 1 on success, 0 when the values are invalid
 */
int
serviceSetConnectionLimits(Service *service, int max, int queued, int timeout)
{

    if (max < 0 || queued < 0)
    {
        return 0;
    }

    service->max_connections = max;

    ss_info_dassert(queued == 0, "Queued connections not implemented.");
    ss_info_dassert(timeout == 0, "Queued connections not implemented.");

    return 1;
}

/**
 * Enable or disable the restarting of the service on failure.
 * @param service Service to configure
 * @param value A string representation of a boolean value
 */
void serviceSetRetryOnFailure(Service *service, const char* value)
{
    if (value)
    {
        service->retry_start = config_truth_value(value);
    }
}

void service_set_retry_interval(Service *service, int value)
{
    ss_dassert(value > 0);
    service->max_retry_interval = value;
}

/**
 * Set the filters used by the service
 *
 * @param service The service itself
 * @param filters The filters to use separated by the pipe character |
 *
 * @return True if loading and creating all filters was successful. False if a
 *         filter module was not found or the instance creation failed.
 */
bool service_set_filters(Service* service, const char* filters)
{
    bool rval = true;
    std::vector<MXS_FILTER_DEF*> flist;
    uint64_t capabilities = 0;

    for (auto&& f: mxs::strtok(filters, "|"))
    {
        fix_object_name(f);

        if (FilterDef* def = filter_find(f.c_str()))
        {
            flist.push_back(def);

            const MXS_MODULE* module = get_module(def->module, MODULE_FILTER);
            ss_dassert(module);
            capabilities |= module->module_capabilities;

            if (def->obj->getCapabilities)
            {
                capabilities |= def->obj->getCapabilities(def->filter);
            }
        }
        else
        {
            MXS_ERROR("Unable to find filter '%s' for service '%s'", f.c_str(), service->name);
            rval = false;
        }
    }

    if (rval)
    {
        service->filters = (MXS_FILTER_DEF**)MXS_MALLOC((flist.size() + 1) * sizeof(MXS_FILTER_DEF*));
        std::copy(flist.begin(), flist.end(), service->filters);
        service->n_filters = flist.size();
        service->filters[service->n_filters] = NULL;
        service->capabilities |= capabilities;
    }

    return rval;
}

Service* service_internal_find(const char *name)
{
    Service* service = nullptr;

    spinlock_acquire(&service_spin);

    for (Service* s: allServices)
    {
        if (strcmp(s->name, name) == 0 && atomic_load_int(&s->active))
        {
            service = s;
            break;
        }
    }

    spinlock_release(&service_spin);

    return service;
}

/**
 * Return a named service
 *
 * @param servname      The name of the service to find
 * @return The service or NULL if not found
 */
SERVICE* service_find(const char *servname)
{
    return service_internal_find(servname);
}

/**
 * Print all services to a DCB
 *
 * Designed to be called within a CLI command in order
 * to display all active services within the gateway
 */
void
dprintAllServices(DCB *dcb)
{
    spinlock_acquire(&service_spin);

    for (Service* s: allServices)
    {
        dprintService(dcb, s);
    }

    spinlock_release(&service_spin);
}

/**
 * Print details of a single service.
 *
 * @param dcb           DCB to print data to
 * @param service       The service to print
 */
void dprintService(DCB *dcb, SERVICE *svc)
{
    Service* service = static_cast<Service*>(svc);
    SERVER_REF *server = service->dbref;
    struct tm result;
    char timebuf[30];
    int i;

    dcb_printf(dcb, "\tService:                             %s\n", service->name);
    dcb_printf(dcb, "\tRouter:                              %s\n", service->routerModule);
    switch (service->state)
    {
    case SERVICE_STATE_STARTED:
        dcb_printf(dcb, "\tState:                               Started\n");
        break;
    case SERVICE_STATE_STOPPED:
        dcb_printf(dcb, "\tState:                               Stopped\n");
        break;
    case SERVICE_STATE_FAILED:
        dcb_printf(dcb, "\tState:                               Failed\n");
        break;
    case SERVICE_STATE_ALLOC:
        dcb_printf(dcb, "\tState:                               Allocated\n");
        break;
    }
    if (service->router && service->router_instance)
    {
        service->router->diagnostics(service->router_instance, dcb);
    }
    dcb_printf(dcb, "\tStarted:                             %s",
               asctime_r(localtime_r(&service->stats.started, &result), timebuf));
    dcb_printf(dcb, "\tRoot user access:                    %s\n",
               service->enable_root ? "Enabled" : "Disabled");
    if (service->n_filters)
    {
        dcb_printf(dcb, "\tFilter chain:                ");
        for (i = 0; i < service->n_filters; i++)
        {
            dcb_printf(dcb, "%s %s ", filter_def_get_name(service->filters[i]),
                       i + 1 < service->n_filters ? "|" : "");
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb, "\tBackend databases:\n");
    while (server)
    {
        if (SERVER_REF_IS_ACTIVE(server))
        {
            dcb_printf(dcb, "\t\t[%s]:%d    Protocol: %s    Name: %s\n",
                       server->server->address, server->server->port,
                       server->server->protocol, server->server->name);
        }
        server = server->next;
    }
    if (*service->weightby)
    {
        dcb_printf(dcb, "\tRouting weight parameter:            %s\n",
                   service->weightby);
    }

    dcb_printf(dcb, "\tTotal connections:                   %d\n",
               service->stats.n_sessions);
    dcb_printf(dcb, "\tCurrently connected:                 %d\n",
               service->stats.n_current);
}

/**
 * List the defined services in a tabular format.
 *
 * @param dcb           DCB to print the service list to.
 */
void
dListServices(DCB *dcb)
{
    const char HORIZ_SEPARATOR[] = "--------------------------+-------------------"
                                   "+--------+----------------+-------------------\n";
    spinlock_acquire(&service_spin);

    if (!allServices.empty())
    {
        dcb_printf(dcb, "Services.\n");
        dcb_printf(dcb, "%s", HORIZ_SEPARATOR);
        dcb_printf(dcb, "%-25s | %-17s | #Users | Total Sessions | Backend databases\n",
                   "Service Name", "Router Module");
        dcb_printf(dcb, "%s", HORIZ_SEPARATOR);
    }
    for (Service* service: allServices)
    {
        ss_dassert(service->stats.n_current >= 0);
        dcb_printf(dcb, "%-25s | %-17s | %6d | %14d | ",
                   service->name, service->routerModule,
                   service->stats.n_current, service->stats.n_sessions);

        SERVER_REF* server_ref = service->dbref;
        bool first = true;
        while (server_ref)
        {
            if (SERVER_REF_IS_ACTIVE(server_ref))
            {
                if (first)
                {
                    dcb_printf(dcb, "%s", server_ref->server->name);
                }
                else
                {
                    dcb_printf(dcb, ", %s", server_ref->server->name);
                }
                first = false;
            }
            server_ref = server_ref->next;
        }
        dcb_printf(dcb, "\n");
    }
    if (!allServices.empty())
    {
        dcb_printf(dcb, "%s\n", HORIZ_SEPARATOR);
    }
    spinlock_release(&service_spin);
}

/**
 * List the defined listeners in a tabular format.
 *
 * @param dcb           DCB to print the service list to.
 */
void
dListListeners(DCB *dcb)
{
    SERVICE *service;
    SERV_LISTENER *port;

    spinlock_acquire(&service_spin);

    if (!allServices.empty())
    {
        dcb_printf(dcb, "Listeners.\n");
        dcb_printf(dcb, "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n");
        dcb_printf(dcb, "%-20s | %-19s | %-18s | %-15s | Port  | State\n",
                   "Name", "Service Name", "Protocol Module", "Address");
        dcb_printf(dcb, "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n");
    }
    for (Service* service: allServices)
    {
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
             listener; listener = listener_iterator_next(&iter))
        {
            if (listener_is_active(listener))
            {
                dcb_printf(dcb, "%-20s | %-19s | %-18s | %-15s | %5d | %s\n",
                           listener->name, service->name, listener->protocol,
                           (listener && listener->address) ? listener->address : "*",
                           listener->port,
                           listener_state_to_string(listener));
            }
        }
    }
    if (!allServices.empty())
    {
        dcb_printf(dcb, "---------------------+---------------------+"
                   "--------------------+-----------------+-------+--------\n\n");
    }
    spinlock_release(&service_spin);
}

/**
 * Refresh the database users for the service
 * This function replaces the MySQL users used by the service with the latest
 * version found on the backend servers. There is a limit on how often the users
 * can be reloaded and if this limit is exceeded, the reload will fail.
 * @param service Service to reload
 * @return 0 on success and 1 on error
 */
int service_refresh_users(SERVICE *service)
{
    ss_dassert(service);
    int ret = 1;
    int self = mxs_rworker_get_current_id();
    ss_dassert(self >= 0);
    time_t now = time(NULL);

    if ((service->capabilities & ACAP_TYPE_ASYNC) == 0)
    {
        spinlock_acquire(&service->spin);
        // Use only one rate limitation for synchronous authenticators to keep
        // rate limitations synchronous as well
        self = 0;
    }

    MXS_CONFIG* config = config_get_global_options();

    /* Check if refresh rate limit has been exceeded */
    if (now < service->rate_limits[self].last + config->users_refresh_time)
    {
        if (!service->rate_limits[self].warned)
        {
            MXS_WARNING("[%s] Refresh rate limit (once every %ld seconds) exceeded for "
                        "load of users' table.",
                        service->name, config->users_refresh_time);
            service->rate_limits[self].warned = true;
        }
    }
    else
    {
        service->rate_limits[self].last = now;
        service->rate_limits[self].warned = false;


        ret = 0;
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
             listener; listener = listener_iterator_next(&iter))
        {
            /** Load the authentication users before before starting the listener */
            if (listener_is_active(listener) && listener->listener &&
                listener->listener->authfunc.loadusers)
            {
                switch (listener->listener->authfunc.loadusers(listener))
                {
                case MXS_AUTH_LOADUSERS_FATAL:
                    MXS_ERROR("[%s] Fatal error when loading users for listener '%s',"
                              " authentication will not work.", service->name, listener->name);
                    ret = 1;
                    break;

                case MXS_AUTH_LOADUSERS_ERROR:
                    MXS_WARNING("[%s] Failed to load users for listener '%s', authentication"
                                " might not work.", service->name, listener->name);
                    ret = 1;
                    break;

                default:
                    break;
                }
            }
        }
    }

    if ((service->capabilities & ACAP_TYPE_ASYNC) == 0)
    {
        spinlock_release(&service->spin);
    }

    return ret;
}

void service_add_parameters(Service *service, const MXS_CONFIG_PARAMETER *param)
{
    while (param)
    {
        MXS_CONFIG_PARAMETER *new_param = config_clone_param(param);
        new_param->next = service->svc_config_param;
        service->svc_config_param = new_param;
        param = param->next;
    }
}

void service_add_parameter(Service *service, const char* key, const char* value)
{
    MXS_CONFIG_PARAMETER p{const_cast<char*>(key), const_cast<char*>(value), nullptr};
    service_add_parameters(service, &p);
}

void service_remove_parameter(Service *service, const char* key)
{
    if (MXS_CONFIG_PARAMETER* params = service->svc_config_param)
    {
        MXS_CONFIG_PARAMETER* to_free = NULL;

        if (strcasecmp(params->name, key) == 0)
        {
            service->svc_config_param = params->next;
            to_free = params;
        }
        else
        {
            while (MXS_CONFIG_PARAMETER* p = params->next)
            {
                if (strcasecmp(p->name, key) == 0)
                {
                    params->next = p->next;
                    to_free = p;
                    break;
                }

                params = p;
            }
        }

        if (to_free)
        {
            // Set next pointer to null to prevent freeing of other parameters
            to_free->next = NULL;
            config_parameter_free(to_free);
        }
    }
}

void service_replace_parameter(Service *service, const char* key, const char* value)
{
    for (MXS_CONFIG_PARAMETER* p = service->svc_config_param; p; p = p->next)
    {
        if (strcasecmp(p->name, key) == 0)
        {
            MXS_FREE(p->value);
            p->value = MXS_STRDUP_A(value);
            return;
        }
    }

    service_add_parameter(service, key, value);
}

/**
 * Set the weighting parameter for the service
 *
 * @param       service         The service pointer
 * @param       weightby        The parameter name to weight the routing by
 */
void serviceWeightBy(Service *service, const char *weightby)
{
    if (service->weightby != weightby)
    {
        snprintf(service->weightby, sizeof(service->weightby), "%s", weightby);
    }
}

/**
 * Return the parameter the wervice shoudl use to weight connections
 * by
 * @param service               The Service pointer
 */
const char* serviceGetWeightingParameter(SERVICE *svc)
{
    Service* service = static_cast<Service*>(svc);
    return service->weightby;
}

/**
 * Enable/Disable localhost authentication match criteria
 * associated with this service.
 *
 * @param service       The service we are setting the data for
 * @param action        1 for enable, 0 for disable access
 * @return              0 on failure
 */

int
serviceEnableLocalhostMatchWildcardHost(Service *service, int action)
{
    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->localhost_match_wildcard_host = action;

    return 1;
}

volatile sig_atomic_t service_should_stop = 0;

void service_shutdown()
{
    service_should_stop = 1;
}

void service_destroy_instances(void)
{
    // The global list is modified by service_free so we need a copy of it
    std::vector<Service*> my_services = allServices;

    for (Service* s: my_services)
    {
        service_free(s);
    }
}

/**
 * Return the count of all sessions active for all services
 *
 * @return Count of all active sessions
 */
int
serviceSessionCountAll()
{
    int rval = 0;

    spinlock_acquire(&service_spin);
    for (Service* service: allServices)
    {
        rval += service->stats.n_current;
    }
    spinlock_release(&service_spin);

    return rval;
}

/**
 * Return a resultset that has the current set of services in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> serviceGetListenerList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Service Name", "Protocol Module", "Address", "Port", "State"});
    spinlock_acquire(&service_spin);

    for (Service* service : allServices)
    {
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER* lptr = listener_iterator_init(service, &iter);
             lptr; lptr = listener_iterator_next(&iter))
        {
            set->add_row({service->name, lptr->protocol, lptr->address,
                         std::to_string(lptr->port), listener_state_to_string(lptr)});
        }
    }

    spinlock_release(&service_spin);
    return set;
}

/**
 * Return a result set that has the current set of services in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> serviceGetList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Service Name", "Router Module", "No. Sessions", "Total Sessions"});
    spinlock_acquire(&service_spin);

    for (Service* s : allServices)
    {
        set->add_row({s->name, s->routerModule, std::to_string(s->stats.n_current),
                     std::to_string(s->stats.n_sessions)});
    }

    spinlock_release(&service_spin);
    return set;
}

/**
 * Function called by the housekeeper thread to retry starting of a service
 * @param data Service to restart
 */
static bool service_internal_restart(void *data)
{
    Service* service = (Service*)data;
    serviceStartAllPorts(service);
    return false;
}

/**
 * Check that all services have listeners
 * @return True if all services have listeners
 */
bool service_all_services_have_listeners()
{
    bool rval = true;
    spinlock_acquire(&service_spin);

    for (Service* service: allServices)
    {
        LISTENER_ITERATOR iter;
        SERV_LISTENER *listener = listener_iterator_init(service, &iter);

        if (listener == NULL)
        {
            MXS_ERROR("Service '%s' has no listeners.", service->name);
            rval = false;
        }
    }

    spinlock_release(&service_spin);
    return rval;
}

static void service_calculate_weights(SERVICE *service)
{
    const char *weightby = serviceGetWeightingParameter(service);

    if (*weightby && service->dbref)
    {
        char buf[50]; // Enough to hold most numbers
        /** Service has a weighting parameter and at least one server */
        int total = 0;

        /** Calculate total weight */
        for (SERVER_REF *server = service->dbref; server; server = server->next)
        {
            server->weight = SERVICE_BASE_SERVER_WEIGHT;

            if (server_get_parameter(server->server, weightby, buf, sizeof(buf)))
            {
                total += atoi(buf);
            }
        }

        if (total == 0)
        {
            MXS_WARNING("Weighting Parameter for service '%s' will be ignored as "
                        "no servers have values for the parameter '%s'.",
                        service->name, weightby);
        }
        else if (total < 0)
        {
            MXS_ERROR("Sum of weighting parameter '%s' for service '%s' exceeds "
                      "maximum value of %d. Weighting will be ignored.",
                      weightby, service->name, INT_MAX);
        }
        else
        {
            /** Calculate the relative weight of the servers */
            for (SERVER_REF *server = service->dbref; server; server = server->next)
            {
                if (server_get_parameter(server->server, weightby, buf, sizeof(buf)))
                {
                    int wght = atoi(buf);
                    int perc = (wght * SERVICE_BASE_SERVER_WEIGHT) / total;

                    if (perc == 0)
                    {
                        MXS_WARNING("Weighting parameter '%s' with a value of %d for"
                                    " server '%s' rounds down to zero with total weight"
                                    " of %d for service '%s'. No queries will be "
                                    "routed to this server as long as a server with"
                                    " positive weight is available.",
                                    weightby, wght, server->server->name,
                                    total, service->name);
                    }
                    else if (perc < 0)
                    {
                        MXS_ERROR("Weighting parameter '%s' for server '%s' is too large, "
                                  "maximum value is %d. No weighting will be used for this "
                                  "server.", weightby, server->server->name,
                                  INT_MAX / SERVICE_BASE_SERVER_WEIGHT);
                        perc = SERVICE_BASE_SERVER_WEIGHT;
                    }
                    server->weight = perc;
                }
                else
                {
                    MXS_WARNING("Server '%s' has no parameter '%s' used for weighting"
                                " for service '%s'.", server->server->name,
                                weightby, service->name);
                }
            }
        }
    }
}

void service_update_weights()
{
    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        service_calculate_weights(service);
    }

    spinlock_release(&service_spin);
}

bool service_server_in_use(const SERVER *server)
{
    bool rval = false;

    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        spinlock_acquire(&service->spin);

        for (SERVER_REF *ref = service->dbref; ref && !rval; ref = ref->next)
        {
            if (ref->active && ref->server == server)
            {
                rval = true;
            }
        }

        spinlock_release(&service->spin);

        if (rval)
        {
            break;
        }
    }

    spinlock_release(&service_spin);

    return rval;
}

bool service_filter_in_use(const MXS_FILTER_DEF *filter)
{
    ss_dassert(filter);
    bool rval = false;

    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        spinlock_acquire(&service->spin);
        for (int i = 0; i < service->n_filters; i++)
        {
            if (service->filters[i] == filter)
            {
                rval = true;
                break;
            }
        }

        spinlock_release(&service->spin);

        if (rval)
        {
            break;
        }
    }

    spinlock_release(&service_spin);

    return rval;
}

/**
 * Creates a service configuration at the location pointed by @c filename
 *
 * @param service Service to serialize into a configuration
 * @param filename Filename where configuration is written
 * @return True on success, false on error
 */
static bool create_service_config(const SERVICE *service, const char *filename)
{
    int file = open(filename, O_EXCL | O_CREAT | O_WRONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

    if (file == -1)
    {
        MXS_ERROR("Failed to open file '%s' when serializing service '%s': %d, %s",
                  filename, service->name, errno, mxs_strerror(errno));
        return false;
    }

    /**
     * TODO: Check for return values on all of the dprintf calls
     */
    dprintf(file, "[%s]\n", service->name);
    dprintf(file, "%s=service\n", CN_TYPE);
    dprintf(file, "%s=%s\n", CN_ROUTER, service->routerModule);
    dprintf(file, "%s=%s\n", CN_USER, service->credentials.name);
    dprintf(file, "%s=%s\n", CN_PASSWORD, service->credentials.authdata);
    dprintf(file, "%s=%s\n", CN_ENABLE_ROOT_USER, service->enable_root ? "true" : "false");
    dprintf(file, "%s=%d\n", CN_MAX_RETRY_INTERVAL, service->max_retry_interval);
    dprintf(file, "%s=%d\n", CN_MAX_CONNECTIONS, service->max_connections);
    dprintf(file, "%s=%ld\n", CN_CONNECTION_TIMEOUT, service->conn_idle_timeout);
    dprintf(file, "%s=%s\n", CN_AUTH_ALL_SERVERS, service->users_from_all ? "true" : "false");
    dprintf(file, "%s=%s\n", CN_STRIP_DB_ESC, service->strip_db_esc ? "true" : "false");
    dprintf(file, "%s=%s\n", CN_LOCALHOST_MATCH_WILDCARD_HOST,
            service->localhost_match_wildcard_host ? "true" : "false");
    dprintf(file, "%s=%s\n", CN_LOG_AUTH_WARNINGS, service->log_auth_warnings ? "true" : "false");
    dprintf(file, "%s=%s\n", CN_RETRY_ON_FAILURE, service->retry_start ? "true" : "false");

    if (*service->version_string)
    {
        dprintf(file, "%s=%s\n", CN_VERSION_STRING, service->version_string);
    }

    if (*service->weightby)
    {
        dprintf(file, "%s=%s\n", CN_WEIGHTBY, service->weightby);
    }

    if (service->dbref)
    {
        dprintf(file, "%s=", CN_SERVERS);
        const char *sep = "";

        for (SERVER_REF *db = service->dbref; db; db = db->next)
        {
            if (SERVER_REF_IS_ACTIVE(db))
            {
                dprintf(file, "%s%s", sep, db->server->name);
                sep = ",";
            }
        }
        dprintf(file, "\n");
    }

    std::unordered_set<std::string> common_params
    {
        CN_TYPE,
        CN_USER,
        CN_PASSWORD,
        CN_ENABLE_ROOT_USER,
        CN_MAX_RETRY_INTERVAL,
        CN_MAX_CONNECTIONS,
        CN_CONNECTION_TIMEOUT,
        CN_AUTH_ALL_SERVERS,
        CN_STRIP_DB_ESC,
        CN_LOCALHOST_MATCH_WILDCARD_HOST,
        CN_LOG_AUTH_WARNINGS,
        CN_RETRY_ON_FAILURE,
        CN_VERSION_STRING,
        CN_WEIGHTBY,
        CN_SERVERS
    };

    // Dump router specific parameters
    for (auto p = service->svc_config_param; p; p = p->next)
    {
        if (common_params.count(p->name) == 0)
        {
            dprintf(file, "%s=%s\n", p->name, p->value);
        }
    }

    close(file);

    return true;
}

bool service_serialize(const Service *service)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             service->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary service configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_service_config(service, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        ss_dassert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary service configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

bool service_serialize_servers(const SERVICE *service)
{
    bool rval = false;
    char filename[PATH_MAX];
    snprintf(filename, sizeof(filename), "%s/%s.cnf.tmp", get_config_persistdir(),
             service->name);

    if (unlink(filename) == -1 && errno != ENOENT)
    {
        MXS_ERROR("Failed to remove temporary service configuration at '%s': %d, %s",
                  filename, errno, mxs_strerror(errno));
    }
    else if (create_service_config(service, filename))
    {
        char final_filename[PATH_MAX];
        strcpy(final_filename, filename);

        char *dot = strrchr(final_filename, '.');
        ss_dassert(dot);
        *dot = '\0';

        if (rename(filename, final_filename) == 0)
        {
            rval = true;
        }
        else
        {
            MXS_ERROR("Failed to rename temporary service configuration at '%s': %d, %s",
                      filename, errno, mxs_strerror(errno));
        }
    }

    return rval;
}

void service_print_users(DCB *dcb, const SERVICE *service)
{
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && listener->listener &&
            listener->listener->authfunc.diagnostic)
        {
            dcb_printf(dcb, "User names (%s): ", listener->name);

            listener->listener->authfunc.diagnostic(dcb, listener);

            dcb_printf(dcb, "\n");
        }
    }
}

bool service_port_is_used(unsigned short port)
{
    bool rval = false;
    spinlock_acquire(&service_spin);

    for (SERVICE *service: allServices)
    {
        LISTENER_ITERATOR iter;

        for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
             listener; listener = listener_iterator_next(&iter))
        {
            if (listener_is_active(listener) && listener->port == port)
            {
                rval = true;
                break;
            }
        }

        if (rval)
        {
            break;
        }
    }

    spinlock_release(&service_spin);

    return rval;
}

static const char* service_state_to_string(int state)
{
    switch (state)
    {
    case SERVICE_STATE_STARTED:
        return "Started";

    case SERVICE_STATE_STOPPED:
        return "Stopped";

    case SERVICE_STATE_FAILED:
        return "Failed";

    case SERVICE_STATE_ALLOC:
        return "Allocated";

    default:
        ss_dassert(false);
        return "Unknown";
    }
}

json_t* service_parameters_to_json(const SERVICE* service)
{
    json_t* rval = json_object();

    string options{config_get_string(service->svc_config_param, "router_options")};

    json_object_set_new(rval, CN_ROUTER_OPTIONS, json_string(options.c_str()));
    json_object_set_new(rval, CN_USER, json_string(service->credentials.name));
    json_object_set_new(rval, CN_PASSWORD, json_string(service->credentials.authdata));

    json_object_set_new(rval, CN_ENABLE_ROOT_USER, json_boolean(service->enable_root));
    json_object_set_new(rval, CN_MAX_RETRY_INTERVAL, json_integer(service->max_retry_interval));
    json_object_set_new(rval, CN_MAX_CONNECTIONS, json_integer(service->max_connections));
    json_object_set_new(rval, CN_CONNECTION_TIMEOUT, json_integer(service->conn_idle_timeout));

    json_object_set_new(rval, CN_AUTH_ALL_SERVERS, json_boolean(service->users_from_all));
    json_object_set_new(rval, CN_STRIP_DB_ESC, json_boolean(service->strip_db_esc));
    json_object_set_new(rval, CN_LOCALHOST_MATCH_WILDCARD_HOST,
                        json_boolean(service->localhost_match_wildcard_host));
    json_object_set_new(rval, CN_VERSION_STRING, json_string(service->version_string));

    if (*service->weightby)
    {
        json_object_set_new(rval, CN_WEIGHTBY, json_string(service->weightby));
    }

    json_object_set_new(rval, CN_LOG_AUTH_WARNINGS, json_boolean(service->log_auth_warnings));
    json_object_set_new(rval, CN_RETRY_ON_FAILURE, json_boolean(service->retry_start));

    /** Add custom module parameters */
    const MXS_MODULE* mod = get_module(service->routerModule, MODULE_ROUTER);
    config_add_module_params_json(mod, service->svc_config_param, config_service_params, rval);

    return rval;
}

static inline bool have_active_servers(const SERVICE* service)
{
    for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
    {
        if (SERVER_REF_IS_ACTIVE(ref))
        {
            return true;
        }
    }

    return false;
}

static json_t* service_all_listeners_json_data(const SERVICE* service)
{
    json_t* arr = json_array();
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener))
        {
            json_array_append_new(arr, listener_to_json(listener));
        }
    }

    return arr;
}

static json_t* service_listener_json_data(const SERVICE* service, const char* name)
{
    LISTENER_ITERATOR iter;

    for (SERV_LISTENER *listener = listener_iterator_init(service, &iter);
         listener; listener = listener_iterator_next(&iter))
    {
        if (listener_is_active(listener) && strcmp(listener->name, name) == 0)
        {
            return listener_to_json(listener);
        }
    }

    return NULL;
}

json_t* service_attributes(const SERVICE* service)
{
    json_t* attr = json_object();

    json_object_set_new(attr, CN_ROUTER, json_string(service->routerModule));
    json_object_set_new(attr, CN_STATE, json_string(service_state_to_string(service->state)));

    if (service->router && service->router_instance && service->router->diagnostics_json)
    {
        json_t* diag = service->router->diagnostics_json(service->router_instance);

        if (diag)
        {
            json_object_set_new(attr, CN_ROUTER_DIAGNOSTICS, diag);
        }
    }

    struct tm result;
    char timebuf[30];

    asctime_r(localtime_r(&service->stats.started, &result), timebuf);
    trim(timebuf);

    json_object_set_new(attr, "started", json_string(timebuf));
    json_object_set_new(attr, "total_connections", json_integer(service->stats.n_sessions));
    json_object_set_new(attr, "connections", json_integer(service->stats.n_current));

    /** Add service parameters and listeners */
    json_object_set_new(attr, CN_PARAMETERS, service_parameters_to_json(service));
    json_object_set_new(attr, CN_LISTENERS, service_all_listeners_json_data(service));

    return attr;
}

json_t* service_relationships(const SERVICE* service, const char* host)
{
    /** Store relationships to other objects */
    json_t* rel = json_object();

    if (service->n_filters)
    {
        json_t* filters = mxs_json_relationship(host, MXS_JSON_API_FILTERS);

        for (int i = 0; i < service->n_filters; i++)
        {
            mxs_json_add_relation(filters, filter_def_get_name(service->filters[i]), CN_FILTERS);
        }

        json_object_set_new(rel, CN_FILTERS, filters);
    }

    if (have_active_servers(service))
    {
        json_t* servers = mxs_json_relationship(host, MXS_JSON_API_SERVERS);

        for (SERVER_REF* ref = service->dbref; ref; ref = ref->next)
        {
            if (SERVER_REF_IS_ACTIVE(ref))
            {
                mxs_json_add_relation(servers, ref->server->name, CN_SERVERS);
            }
        }

        json_object_set_new(rel, CN_SERVERS, servers);
    }

    return rel;
}

json_t* service_json_data(const SERVICE* service, const char* host)
{
    json_t* rval = json_object();

    spinlock_acquire(&service->spin);

    json_object_set_new(rval, CN_ID, json_string(service->name));
    json_object_set_new(rval, CN_TYPE, json_string(CN_SERVICES));
    json_object_set_new(rval, CN_ATTRIBUTES, service_attributes(service));
    json_object_set_new(rval, CN_RELATIONSHIPS, service_relationships(service, host));
    json_object_set_new(rval, CN_LINKS, mxs_json_self_link(host, CN_SERVICES, service->name));

    spinlock_release(&service->spin);

    return rval;
}

json_t* service_to_json(const Service* service, const char* host)
{
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    return mxs_json_resource(host, self.c_str(), service_json_data(service, host));
}

json_t* service_listener_list_to_json(const Service* service, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    self += "/listeners";

    return mxs_json_resource(host, self.c_str(), service_all_listeners_json_data(service));
}

json_t* service_listener_to_json(const Service* service, const char* name, const char* host)
{
    /** This needs to be done here as the listeners are sort of sub-resources
     * of the service. */
    string self = MXS_JSON_API_SERVICES;
    self += service->name;
    self += "/listeners/";
    self += name;

    return mxs_json_resource(host, self.c_str(), service_listener_json_data(service, name));
}

json_t* service_list_to_json(const char* host)
{
    json_t* arr = json_array();

    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        json_t* svc = service_json_data(service, host);

        if (svc)
        {
            json_array_append_new(arr, svc);
        }
    }

    spinlock_release(&service_spin);

    return mxs_json_resource(host, MXS_JSON_API_SERVICES, arr);
}

json_t* service_relations_to_filter(const MXS_FILTER_DEF* filter, const char* host)
{
    json_t* rel = mxs_json_relationship(host, MXS_JSON_API_SERVICES);

    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        spinlock_acquire(&service->spin);

        for (int i = 0; i < service->n_filters; i++)
        {
            if (service->filters[i] == filter)
            {
                mxs_json_add_relation(rel, service->name, CN_SERVICES);
            }
        }

        spinlock_release(&service->spin);
    }

    spinlock_release(&service_spin);

    return rel;
}


json_t* service_relations_to_server(const SERVER* server, const char* host)
{
    std::vector<std::string> names;
    spinlock_acquire(&service_spin);

    for (Service *service: allServices)
    {
        spinlock_acquire(&service->spin);

        for (SERVER_REF *ref = service->dbref; ref; ref = ref->next)
        {
            if (ref->server == server && SERVER_REF_IS_ACTIVE(ref))
            {
                names.push_back(service->name);
            }
        }

        spinlock_release(&service->spin);
    }

    spinlock_release(&service_spin);

    json_t* rel = NULL;

    if (!names.empty())
    {
        rel = mxs_json_relationship(host, MXS_JSON_API_SERVICES);

        for (std::vector<std::string>::iterator it = names.begin();
             it != names.end(); it++)
        {
            mxs_json_add_relation(rel, it->c_str(), CN_SERVICES);
        }
    }

    return rel;
}

uint64_t service_get_version(const SERVICE *svc, service_version_which_t which)
{
    const Service* service = static_cast<const Service*>(svc);
    uint64_t version = 0;

    if (which == SERVICE_VERSION_ANY)
    {
        SERVER_REF* sref = service->dbref;

        while (sref && !sref->active)
        {
            sref = sref->next;
        }

        if (sref)
        {
            version = server_get_version(sref->server);
        }
    }
    else
    {
        size_t n = 0;

        uint64_t v;

        if (which == SERVICE_VERSION_MIN)
        {
            v = UINT64_MAX;
        }
        else
        {
            ss_dassert(which == SERVICE_VERSION_MAX);

            v = 0;
        }

        SERVER_REF* sref = service->dbref;

        while (sref)
        {
            if (sref->active)
            {
                ++n;

                SERVER* s = sref->server;
                uint64_t server_version = server_get_version(s);

                if (which == SERVICE_VERSION_MIN)
                {
                    if (server_version < v)
                    {
                        v = server_version;
                    }
                }
                else
                {
                    ss_dassert(which == SERVICE_VERSION_MAX);

                    if (server_version > v)
                    {
                        v = server_version;
                    }
                }
            }

            sref = sref->next;
        }

        if (n == 0)
        {
            v = 0;
        }

        version = v;
    }

    return version;
}

bool service_thread_init()
{
    spinlock_acquire(&service_spin);

    for (Service* service: allServices)
    {
        if (service->capabilities & ACAP_TYPE_ASYNC)
        {
            service_refresh_users(service);
        }
    }

    spinlock_release(&service_spin);

    return true;
}
