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

/**
 * @file service.c  - A representation of the service within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 18/06/13     Mark Riddoch            Initial implementation
 * 24/06/13     Massimiliano Pinto      Added: Loading users from mysql backend in serviceStart
 * 06/02/14     Massimiliano Pinto      Added: serviceEnableRootUser routine
 * 25/02/14     Massimiliano Pinto      Added: service refresh limit feature
 * 28/02/14     Massimiliano Pinto      users_alloc moved from service_alloc to
 *                                      serviceStartPort (generic hashable for services)
 * 07/05/14     Massimiliano Pinto      Added: version_string initialized to NULL
 * 23/05/14     Mark Riddoch            Addition of service validation call
 * 29/05/14     Mark Riddoch            Filter API implementation
 * 09/09/14     Massimiliano Pinto      Added service option for localhost authentication
 * 13/10/14     Massimiliano Pinto      Added hashtable for resources (i.e database names for MySQL services)
 * 06/02/15     Mark Riddoch            Added caching of authentication data
 * 18/02/15     Mark Riddoch            Added result set management
 * 03/03/15     Massimiliano Pinto      Added config_enable_feedback_task() call in serviceStartAll
 * 19/06/15     Martin Brampton         More meaningful names for temp variables
 * 31/05/16     Martin Brampton         Implement connection throttling
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <maxscale/session.h>
#include <maxscale/service.h>
#include <maxscale/gw_protocol.h>
#include <maxscale/listener.h>
#include <maxscale/server.h>
#include <maxscale/router.h>
#include <maxscale/spinlock.h>
#include <maxscale/modules.h>
#include <maxscale/dcb.h>
#include <maxscale/users.h>
#include <maxscale/filter.h>
#include <maxscale/dbusers.h>
#include <maxscale/poll.h>
#include <maxscale/log_manager.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <maxscale/housekeeper.h>
#include <maxscale/resultset.h>
#include <maxscale/gwdirs.h>
#include <math.h>
#include <maxscale/version.h>
#include <maxscale/queuemanager.h>
#include <maxscale/alloc.h>
#include <maxscale/utils.h>

/** To be used with configuration type checks */
typedef struct typelib_st
{
    int          tl_nelems;
    const char*  tl_name;
    const char** tl_p_elems;
} typelib_t;

/** Set of subsequent false,true pairs */
static const char* bool_strings[11]  = {"FALSE", "TRUE", "OFF", "ON", "N", "Y", "0", "1", "NO", "YES", 0};
typelib_t bool_type  = {MXS_ARRAY_NELEMS(bool_strings) - 1, "bool_type", bool_strings};

/** List of valid values */
static const char* sqlvar_target_strings[4] = {"MASTER", "ALL", 0};
typelib_t sqlvar_target_type =
{
    MXS_ARRAY_NELEMS(sqlvar_target_strings) - 1,
    "sqlvar_target_type",
    sqlvar_target_strings
};

static SPINLOCK service_spin = SPINLOCK_INIT;
static SERVICE  *allServices = NULL;

static int find_type(typelib_t* tl, const char* needle, int maxlen);

static void service_add_qualified_param(SERVICE*          svc,
                                        CONFIG_PARAMETER* param);
static void service_internal_restart(void *data);
static void service_queue_check(void *data);

/**
 * Allocate a new service for the gateway to support
 *
 *
 * @param servname      The service name
 * @param router        Name of the router module this service uses
 *
 * @return              The newly created service or NULL if an error occurred
 */
SERVICE *
service_alloc(const char *servname, const char *router)
{
    servname = MXS_STRDUP(servname);
    router = MXS_STRDUP(router);

    SERVICE *service = (SERVICE *)MXS_CALLOC(1, sizeof(*service));

    if (!servname || !router || !service)
    {
        MXS_FREE((void*)servname);
        MXS_FREE((void*)router);
        MXS_FREE(service);
        return NULL;
    }

    if ((service->router = load_module(router, MODULE_ROUTER)) == NULL)
    {
        char* home = get_libdir();
        char* ldpath = getenv("LD_LIBRARY_PATH");

        MXS_ERROR("Unable to load %s module \"%s\".\n\t\t\t"
                  "      Ensure that lib%s.so exists in one of the "
                  "following directories :\n\t\t\t      "
                  "- %s\n%s%s",
                  MODULE_ROUTER,
                  router,
                  router,
                  home,
                  ldpath ? "\t\t\t      - " : "",
                  ldpath ? ldpath : "");
        MXS_FREE((void*)servname);
        MXS_FREE((void*)router);
        MXS_FREE(service);
        return NULL;
    }

    service->client_count = 0;
    service->name = (char*)servname;
    service->routerModule = (char*)router;
    service->users_from_all = false;
    service->queued_connections = NULL;
    service->localhost_match_wildcard_host = SERVICE_PARAM_UNINIT;
    service->retry_start = true;
    service->conn_idle_timeout = SERVICE_NO_SESSION_TIMEOUT;
    service->weightby = NULL;
    service->credentials.authdata = NULL;
    service->credentials.name = NULL;
    service->version_string = NULL;
    service->svc_config_param = NULL;
    service->routerOptions = NULL;
    service->log_auth_warnings = true;
    service->strip_db_esc = true;
    if (service->name == NULL || service->routerModule == NULL)
    {
        if (service->name)
        {
            MXS_FREE(service->name);
        }
        MXS_FREE(service);
        return NULL;
    }
    service->stats.started = time(0);
    service->stats.n_failed_starts = 0;
    service->state = SERVICE_STATE_ALLOC;
    spinlock_init(&service->spin);
    spinlock_init(&service->users_table_spin);

    spinlock_acquire(&service_spin);
    service->next = allServices;
    allServices = service;
    spinlock_release(&service_spin);

    return service;
}

/**
 * Check to see if a service pointer is valid
 *
 * @param service       The pointer to check
 * @return 1 if the service is in the list of all services
 */
int
service_isvalid(SERVICE *service)
{
    SERVICE *checkservice;
    int rval = 0;

    spinlock_acquire(&service_spin);
    checkservice = allServices;
    while (checkservice)
    {
        if (checkservice == service)
        {
            rval = 1;
            break;
        }
        checkservice = checkservice->next;
    }
    spinlock_release(&service_spin);
    return rval;
}

/**
 * Start an individual port/protocol pair
 *
 * @param service       The service
 * @param port          The port to start
 * @return              The number of listeners started
 */
static int
serviceStartPort(SERVICE *service, SERV_LISTENER *port)
{
    const size_t ANY_IPV4_ADDRESS_LEN = 7; // strlen("0:0:0:0");

    int listeners = 0;
    size_t config_bind_len =
        (port->address ? strlen(port->address) : ANY_IPV4_ADDRESS_LEN) + 1 + UINTLEN(port->port);
    char config_bind[config_bind_len + 1]; // +1 for NULL
    GWPROTOCOL *funcs;

    if (service == NULL || service->router == NULL || service->router_instance == NULL)
    {
        /* Should never happen, this guarantees it can't */
        MXS_ERROR("Attempt to start port with null or incomplete service");
        goto retblock;
    }

    port->listener = dcb_alloc(DCB_ROLE_SERVICE_LISTENER, port);

    if (port->listener == NULL)
    {
        MXS_ERROR("Failed to create listener for service %s.", service->name);
        goto retblock;
    }

    port->listener->service = service;

    if (port->ssl)
    {
        listener_init_SSL(port->ssl);
    }

    if ((funcs = (GWPROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) == NULL)
    {
        dcb_close(port->listener);
        port->listener = NULL;
        MXS_ERROR("Unable to load protocol module %s. Listener "
                  "for service %s not started.",
                  port->protocol,
                  service->name);
        goto retblock;
    }

    memcpy(&(port->listener->func), funcs, sizeof(GWPROTOCOL));

    const char *authenticator_name = "NullAuth";

    if (port->authenticator)
    {
        authenticator_name = port->authenticator;
    }
    else if (port->listener->func.auth_default)
    {
        authenticator_name = port->listener->func.auth_default();
    }

    GWAUTHENTICATOR *authfuncs = (GWAUTHENTICATOR *)load_module(authenticator_name, MODULE_AUTHENTICATOR);

    if (authfuncs == NULL)
    {
        MXS_ERROR("Failed to load authenticator module '%s' for listener '%s'",
                  authenticator_name, port->name);
        dcb_close(port->listener);
        port->listener = NULL;
        return 0;
    }

    memcpy(&port->listener->authfunc, authfuncs, sizeof(GWAUTHENTICATOR));

    /**
     * Normally, we'd allocate the DCB specific authentication data. As the
     * listeners aren't normal DCBs, we can skip that.
     */

    if (port->address)
    {
        sprintf(config_bind, "%s:%d", port->address, port->port);
    }
    else
    {
        sprintf(config_bind, "0.0.0.0:%d", port->port);
    }

    /** Load the authentication users before before starting the listener */
    if (port->listener->authfunc.loadusers &&
        port->listener->authfunc.loadusers(port) != MXS_AUTH_LOADUSERS_OK)
    {
        MXS_ERROR("[%s] Failed to load users for listener '%s', authentication might not work.",
                  service->name, port->name);
    }

    /**
     * At service start last update is set to USERS_REFRESH_TIME seconds earlier. This way MaxScale
     * could try reloading users just after startup.
     */
    service->rate_limit.last = time(NULL) - USERS_REFRESH_TIME;
    service->rate_limit.nloads = 1;

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
            MXS_ERROR("Failed to create session to service %s.",
                      service->name);
            dcb_close(port->listener);
            port->listener = NULL;
            goto retblock;
        }
    }
    else
    {
        MXS_ERROR("Unable to start to listen port %d for %s %s.",
                  port->port,
                  port->protocol,
                  service->name);
        dcb_close(port->listener);
        port->listener = NULL;
    }

retblock:
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
int serviceStartAllPorts(SERVICE* service)
{
    SERV_LISTENER *port = service->ports;
    int listeners = 0;

    if (port)
    {
        while (!service->svc_do_shutdown && port)
        {
            listeners += serviceStartPort(service, port);
            port = port->next;
        }

        if (listeners)
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
            int retry_after = MXS_MIN(service->stats.n_failed_starts * 10, SERVICE_MAX_RETRY_INTERVAL);
            snprintf(taskname, sizeof(taskname), "%s_start_retry_%d",
                     service->name, service->stats.n_failed_starts);
            hktask_oneshot(taskname, service_internal_restart,
                           (void*) service, retry_after);
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

/** Helper function for copying an array of strings */
static char** copy_string_array(char** original)
{
    char **array = NULL;

    if (original)
    {
        int values = 0;

        while (original[values])
        {
            values++;
        }

        array = MXS_MALLOC(sizeof(char*) * (values + 1));

        if (array)
        {
            for (int i = 0; i < values; i++)
            {
                array[i] = MXS_STRDUP_A(original[i]);
            }
            array[values] = NULL;
        }
    }
    return array;
}

/** Helper function for freeing an array of strings */
static void free_string_array(char** array)
{
    if (array)
    {
        for (int i = 0; array[i]; i++)
        {
            MXS_FREE(array[i]);
        }
        MXS_FREE(array);
    }
}

/**
 * Start a service
 *
 * This function loads the protocol modules for each port on which the
 * service listens and starts the listener on that port
 *
 * Also create the router_instance for the service.
 *
 * @param service       The Service that should be started
 * @return      Returns the number of listeners created
 */
int
serviceStart(SERVICE *service)
{
    int listeners = 0;

    if (check_service_permissions(service))
    {
        char **router_options = copy_string_array(service->routerOptions);
        if ((service->router_instance = service->router->createInstance(
                                            service, router_options)))
        {
            listeners += serviceStartAllPorts(service);
        }
        else
        {
            MXS_ERROR("%s: Failed to create router instance for service. Service not started.",
                      service->name);
            service->state = SERVICE_STATE_FAILED;
        }
        free_string_array(router_options);
    }
    else
    {
        MXS_ERROR("%s: Inadequate user permissions for service. Service not started.",
                  service->name);
        service->state = SERVICE_STATE_FAILED;
    }
    return listeners;
}

/**
 * Start an individual listener
 *
 * @param service       The service to start the listener for
 * @param protocol      The name of the protocol
 * @param port          The port number
 */
void
serviceStartProtocol(SERVICE *service, char *protocol, int port)
{
    SERV_LISTENER *ptr;

    ptr = service->ports;
    while (ptr)
    {
        if (strcmp(ptr->protocol, protocol) == 0 && ptr->port == port)
        {
            serviceStartPort(service, ptr);
        }
        ptr = ptr->next;
    }
}


/**
 * Start all the services
 *
 * @return Return the number of services started
 */
int
serviceStartAll()
{
    SERVICE *ptr;
    int n = 0, i;
    bool error = false;

    config_enable_feedback_task();

    ptr = allServices;
    while (ptr && !ptr->svc_do_shutdown)
    {
        n += (i = serviceStart(ptr));

        if (i == 0)
        {
            MXS_ERROR("Failed to start service '%s'.", ptr->name);
            error = true;
        }

        ptr = ptr->next;
    }
    return error ? 0 : n;
}

/**
 * Stop a service
 *
 * This function stops the listener for the service
 *
 * @param service       The Service that should be stopped
 * @return      Returns the number of listeners restarted
 */
int
serviceStop(SERVICE *service)
{
    SERV_LISTENER *port;
    int listeners = 0;

    port = service->ports;
    while (port)
    {
        if (port->listener && port->listener->session->state == SESSION_STATE_LISTENER)
        {
            if (poll_remove_dcb(port->listener) == 0)
            {
                port->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
                listeners++;
            }
        }
        port = port->next;
    }
    service->state = SERVICE_STATE_STOPPED;

    return listeners;
}

/**
 * Restart a service
 *
 * This function stops the listener for the service
 *
 * @param service       The Service that should be restarted
 * @return      Returns the number of listeners restarted
 */
int
serviceRestart(SERVICE *service)
{
    SERV_LISTENER *port;
    int listeners = 0;

    port = service->ports;
    while (port)
    {
        if (port->listener && port->listener->session->state == SESSION_STATE_LISTENER_STOPPED)
        {
            if (poll_add_dcb(port->listener) == 0)
            {
                port->listener->session->state = SESSION_STATE_LISTENER;
                listeners++;
            }
        }
        port = port->next;
    }
    service->state = SERVICE_STATE_STARTED;
    return listeners;
}


/**
 * Deallocate the specified service
 *
 * @param service       The service to deallocate
 * @return      Returns true if the service was freed
 */
int
service_free(SERVICE *service)
{
    SERVICE *ptr;
    SERVER_REF *srv;
    if (service->stats.n_current)
    {
        return 0;
    }
    /* First of all remove from the linked list */
    spinlock_acquire(&service_spin);
    if (allServices == service)
    {
        allServices = service->next;
    }
    else
    {
        ptr = allServices;
        while (ptr && ptr->next != service)
        {
            ptr = ptr->next;
        }
        if (ptr)
        {
            ptr->next = service->next;
        }
    }
    spinlock_release(&service_spin);

    /* Clean up session and free the memory */
    while (service->dbref)
    {
        srv = service->dbref;
        service->dbref = service->dbref->next;
        MXS_FREE(srv);
    }

    MXS_FREE(service->name);
    MXS_FREE(service->routerModule);
    MXS_FREE(service->weightby);
    MXS_FREE(service->version_string);
    MXS_FREE(service->credentials.name);
    MXS_FREE(service->credentials.authdata);

    free_config_parameter(service->svc_config_param);
    serviceClearRouterOptions(service);

    MXS_FREE(service);
    return 1;
}

/**
 * Add a protocol/port pair to the service
 *
 * @param service       The service
 * @param protocol      The name of the protocol module
 * @param address       The address to listen with
 * @param port          The port to listen on
 * @param authenticator Name of the authenticator to be used
 * @param ssl           SSL configuration
 * @return      TRUE if the protocol/port could be added
 */
int
serviceAddProtocol(SERVICE *service, char *name, char *protocol, char *address,
                   unsigned short port, char *authenticator, char *options,
                   SSL_LISTENER *ssl)
{
    SERV_LISTENER *proto = listener_alloc(service, name, protocol, address,
                                          port, authenticator, options, ssl);

    if (proto)
    {
        spinlock_acquire(&service->spin);
        proto->next = service->ports;
        service->ports = proto;
        spinlock_release(&service->spin);
        return 1;
    }

    return 0;
}

/**
 * Check if a protocol/port pair is part of the service
 *
 * @param service       The service
 * @param protocol      The name of the protocol module
 * @param address       The address to listen on
 * @param port          The port to listen on
 * @return      TRUE if the protocol/port is already part of the service
 */
int serviceHasProtocol(SERVICE *service, const char *protocol,
                       const char* address, unsigned short port)
{
    SERV_LISTENER *proto;

    spinlock_acquire(&service->spin);
    proto = service->ports;
    while (proto)
    {
        if (strcmp(proto->protocol, protocol) == 0 && proto->port == port &&
            ((address && proto->address && strcmp(proto->address, address) == 0) ||
             (address == NULL && proto->address == NULL)))
        {
            break;
        }
        proto = proto->next;
    }
    spinlock_release(&service->spin);

    return proto != NULL;
}

/**
 * Add a backend database server to a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 */
void
serviceAddBackend(SERVICE *service, SERVER *server)
{
    SERVER_REF *sref = MXS_MALLOC(sizeof(SERVER_REF));

    if (sref)
    {
        sref->next = NULL;
        sref->server = server;

        spinlock_acquire(&service->spin);
        if (service->dbref)
        {
            SERVER_REF *ref = service->dbref;
            while (ref->next)
            {
                ref = ref->next;
            }
            ref->next = sref;
        }
        else
        {
            service->dbref = sref;
        }
        spinlock_release(&service->spin);
    }
}

/**
 * Test if a server is part of a service
 *
 * @param service       The service to add the server to
 * @param server        The server to add
 * @return              Non-zero if the server is already part of the service
 */
int
serviceHasBackend(SERVICE *service, SERVER *server)
{
    SERVER_REF *ptr;

    spinlock_acquire(&service->spin);
    ptr = service->dbref;
    while (ptr && ptr->server != server)
    {
        ptr = ptr->next;
    }
    spinlock_release(&service->spin);

    return ptr != NULL;
}

/**
 * Add a router option to a service
 *
 * @param service       The service to add the router option to
 * @param option        The option string
 */
void
serviceAddRouterOption(SERVICE *service, char *option)
{
    int i;

    spinlock_acquire(&service->spin);
    if (service->routerOptions == NULL)
    {
        service->routerOptions = (char **)MXS_CALLOC(2, sizeof(char *));
        MXS_ABORT_IF_NULL(service->routerOptions);
        service->routerOptions[0] = MXS_STRDUP_A(option);
        service->routerOptions[1] = NULL;
    }
    else
    {
        for (i = 0; service->routerOptions[i]; i++)
        {
            ;
        }
        service->routerOptions = (char **)MXS_REALLOC(service->routerOptions, (i + 2) * sizeof(char *));
        MXS_ABORT_IF_NULL(service->routerOptions);
        service->routerOptions[i] = MXS_STRDUP_A(option);
        service->routerOptions[i + 1] = NULL;
    }
    spinlock_release(&service->spin);
}

/**
 * Remove the router options for the service
 *
 * @param       service The service to remove the options from
 */
void
serviceClearRouterOptions(SERVICE *service)
{
    int i;

    spinlock_acquire(&service->spin);
    if (service->routerOptions != NULL)
    {
        for (i = 0; service->routerOptions[i]; i++)
        {
            MXS_FREE(service->routerOptions[i]);
        }
        MXS_FREE(service->routerOptions);
        service->routerOptions = NULL;
    }
    spinlock_release(&service->spin);
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
int
serviceSetUser(SERVICE *service, char *user, char *auth)
{
    user = MXS_STRDUP(user);
    auth = MXS_STRDUP(auth);

    if (!user || !auth)
    {
        MXS_FREE(user);
        MXS_FREE(auth);
        return 0;
    }

    MXS_FREE(service->credentials.name);
    MXS_FREE(service->credentials.authdata);

    service->credentials.name = user;
    service->credentials.authdata = auth;

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
serviceGetUser(SERVICE *service, char **user, char **auth)
{
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

int
serviceEnableRootUser(SERVICE *service, int action)
{
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
serviceAuthAllServers(SERVICE *service, int action)
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
int serviceStripDbEsc(SERVICE* service, int action)
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
serviceSetTimeout(SERVICE *service, int val)
{

    if (val < 0)
    {
        return 0;
    }

    /** Enable the session timeout checks if and only if at least one service is
     * configured with a idle timeout. */
    if ((service->conn_idle_timeout = val))
    {
        enable_session_timeouts();
    }

    return 1;
}

/**
 * Sets the connection limits, if any, for the service.
 * @param service Service to configure
 * @param max The maximum number of client connections at any one time
 * @param queued    The maximum number of connections to queue up when
 *                  max_connections clients are already connected
 * @return 1 on success, 0 when the values are invalid
 */
int
serviceSetConnectionLimits(SERVICE *service, int max, int queued, int timeout)
{

    if (max < 0 || queued < 0)
    {
        return 0;
    }

    service->max_connections = max;
    if (queued && timeout)
    {
        char callback_name[100];
        sprintf(callback_name, "Check queued connections %p", service);
        /* If memory allocation fails, result will be null so no queue */
        service->queued_connections = mxs_queue_alloc(queued, timeout);
        if (service->queued_connections)
        {
            hktask_add(callback_name, service_queue_check, (void *)service->queued_connections, 1);
        }
    }

    return 1;
}

/*
 * @brief The callback function triggered by housekeeping every second
 *
 * This function removes any expired connection requests from the queue, and
 * sends an error message "too many connections" for them.
 *
 * @param   The parameter provided by the callback is the queue config
 */
static void
service_queue_check(void *data)
{
    QUEUE_ENTRY expired;
    QUEUE_CONFIG *queue_config = (QUEUE_CONFIG *)data;
    /* The queued connections are in a FIFO queue, so we only look at the */
    /* start of the queue, and remove any expired entries. As soon as this */
    /* returns nothing, we stop. */
    while ((mxs_dequeue_if_expired(queue_config, &expired)))
    {
        DCB *dcb = (DCB *)expired.queued_object;
        dcb->func.connlimit(dcb, queue_config->queue_limit);
        dcb_close(dcb);
    }
}

/**
 * Enable or disable the restarting of the service on failure.
 * @param service Service to configure
 * @param value A string representation of a boolean value
 */
void serviceSetRetryOnFailure(SERVICE *service, char* value)
{
    if (value)
    {
        service->retry_start = config_truth_value(value);
    }
}

/**
 * Set the filters used by the service
 *
 * @param service       The service itself
 * @param filters       ASCII string of filters to use
 * @return True if loading and creating all filters was successful. False if a
 * filter module was not found or the instance creation failed.
 */
bool
serviceSetFilters(SERVICE *service, char *filters)
{
    FILTER_DEF **flist;
    char *ptr, *brkt;
    int n = 0;
    bool rval = true;

    if ((flist = (FILTER_DEF **) MXS_MALLOC(sizeof(FILTER_DEF *))) == NULL)
    {
        return false;
    }
    ptr = strtok_r(filters, "|", &brkt);
    while (ptr)
    {
        n++;
        FILTER_DEF **tmp;
        if ((tmp = (FILTER_DEF **) MXS_REALLOC(flist,
                                               (n + 1) * sizeof(FILTER_DEF *))) == NULL)
        {
            rval = false;
            break;
        }

        flist = tmp;
        char *filter_name = trim(ptr);

        if ((flist[n - 1] = filter_find(filter_name)))
        {
            if (!filter_load(flist[n - 1]))
            {
                MXS_ERROR("Failed to load filter '%s' for service '%s'.",
                          filter_name, service->name);
                rval = false;
                break;
            }
        }
        else
        {
            MXS_ERROR("Unable to find filter '%s' for service '%s'",
                      filter_name, service->name);
            rval = false;
            break;
        }

        flist[n] = NULL;
        ptr = strtok_r(NULL, "|", &brkt);
    }

    if (rval)
    {
        service->filters = flist;
        service->n_filters = n;
    }
    else
    {
        MXS_FREE(flist);
    }

    return rval;
}

/**
 * Return a named service
 *
 * @param servname      The name of the service to find
 * @return The service or NULL if not found
 */
SERVICE *
service_find(char *servname)
{
    SERVICE *service;

    spinlock_acquire(&service_spin);
    service = allServices;
    while (service && strcmp(service->name, servname) != 0)
    {
        service = service->next;
    }
    spinlock_release(&service_spin);

    return service;
}


/**
 * Print details of an individual service
 *
 * @param service       Service to print
 */
void
printService(SERVICE *service)
{
    SERVER_REF  *ptr = service->dbref;
    struct tm result;
    char time_buf[30];
    int i;

    printf("Service %p\n", (void *)service);
    printf("\tService:                              %s\n", service->name);
    printf("\tRouter:                               %s (%p)\n",
           service->routerModule, (void *)service->router);
    printf("\tStarted:              %s",
           asctime_r(localtime_r(&service->stats.started, &result), time_buf));
    printf("\tBackend databases\n");
    while (ptr)
    {
        printf("\t\t%s:%d  Protocol: %s\n", ptr->server->name, ptr->server->port, ptr->server->protocol);
        ptr = ptr->next;
    }
    if (service->n_filters)
    {
        printf("\tFilter chain:         ");
        for (i = 0; i < service->n_filters; i++)
        {
            printf("%s %s ", service->filters[i]->name,
                   i + 1 < service->n_filters ? "|" : "");
        }
        printf("\n");
    }

    SERV_LISTENER *port = service->ports;
    while (port)
    {
        printf("\tUsers data:           %p\n", (void *)port->users);
        port = port->next;
    }

    printf("\tTotal connections:    %d\n", service->stats.n_sessions);
    printf("\tCurrently connected:  %d\n", service->stats.n_current);
}

/**
 * Print all services
 *
 * Designed to be called within a debugger session in order
 * to display all active services within the gateway
 */
void
printAllServices()
{
    SERVICE *ptr;

    spinlock_acquire(&service_spin);
    ptr = allServices;
    while (ptr)
    {
        printService(ptr);
        ptr = ptr->next;
    }
    spinlock_release(&service_spin);
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
    SERVICE *ptr;

    spinlock_acquire(&service_spin);
    ptr = allServices;
    while (ptr)
    {
        dprintService(dcb, ptr);
        ptr = ptr->next;
    }
    spinlock_release(&service_spin);
}

/**
 * Print details of a single service.
 *
 * @param dcb           DCB to print data to
 * @param service       The service to print
 */
void dprintService(DCB *dcb, SERVICE *service)
{
    SERVER_REF *server = service->dbref;
    struct tm result;
    char timebuf[30];
    int i;

    dcb_printf(dcb, "Service %p\n", service);
    dcb_printf(dcb, "\tService:                             %s\n",
               service->name);
    dcb_printf(dcb, "\tRouter:                              %s (%p)\n",
               service->routerModule, service->router);
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
            dcb_printf(dcb, "%s %s ", service->filters[i]->name,
                       i + 1 < service->n_filters ? "|" : "");
        }
        dcb_printf(dcb, "\n");
    }
    dcb_printf(dcb, "\tBackend databases:\n");
    while (server)
    {
        dcb_printf(dcb, "\t\t%s:%d  Protocol: %s\n", server->server->name, server->server->port,
                   server->server->protocol);
        server = server->next;
    }
    if (service->weightby)
    {
        dcb_printf(dcb, "\tRouting weight parameter:            %s\n",
                   service->weightby);
    }

    SERV_LISTENER *port = service->ports;
    while (port)
    {
        dcb_printf(dcb, "\tUsers data:                          %p\n", port->users);
        port = port->next;
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
    SERVICE *service;

    spinlock_acquire(&service_spin);
    service = allServices;
    if (service)
    {
        dcb_printf(dcb, "Services.\n");
        dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n");
        dcb_printf(dcb, "%-25s | %-20s | #Users | Total Sessions\n",
                   "Service Name", "Router Module");
        dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n");
    }
    while (service)
    {
        ss_dassert(service->stats.n_current >= 0);
        dcb_printf(dcb, "%-25s | %-20s | %6d | %5d\n",
                   service->name, service->routerModule,
                   service->stats.n_current, service->stats.n_sessions);
        service = service->next;
    }
    if (allServices)
    {
        dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n\n");
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
    SERV_LISTENER *lptr;

    spinlock_acquire(&service_spin);
    service = allServices;
    if (service)
    {
        dcb_printf(dcb, "Listeners.\n");
        dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n");
        dcb_printf(dcb, "%-20s | %-18s | %-15s | Port  | State\n",
                   "Service Name", "Protocol Module", "Address");
        dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n");
    }
    while (service)
    {
        lptr = service->ports;
        while (lptr)
        {
            dcb_printf(dcb, "%-20s | %-18s | %-15s | %5d | %s\n",
                       service->name, lptr->protocol,
                       (lptr && lptr->address) ? lptr->address : "*",
                       lptr->port,
                       (!lptr->listener ||
                        !lptr->listener->session ||
                        lptr->listener->session->state == SESSION_STATE_LISTENER_STOPPED) ?
                       "Stopped" : "Running");

            lptr = lptr->next;
        }
        service = service->next;
    }
    if (allServices)
    {
        dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n\n");
    }
    spinlock_release(&service_spin);
}

/**
 * Update the definition of a service
 *
 * @param service       The service to update
 * @param router        The router module to use
 * @param user          The user to use to extract information from the database
 * @param auth          The password for the user above
 */
void
service_update(SERVICE *service, char *router, char *user, char *auth)
{
    void *router_obj;

    if (!strcmp(service->routerModule, router))
    {
        if ((router_obj = load_module(router, MODULE_ROUTER)) == NULL)
        {
            MXS_ERROR("Failed to update router "
                      "for service %s to %s.",
                      service->name,
                      router);
        }
        else
        {
            MXS_NOTICE("Update router for service %s to %s.",
                       service->name,
                       router);
            MXS_FREE(service->routerModule);
            service->routerModule = MXS_STRDUP_A(router);
            service->router = router_obj;
        }
    }
    if (user &&
        (strcmp(service->credentials.name, user) != 0 ||
         strcmp(service->credentials.authdata, auth) != 0))
    {
        MXS_NOTICE("Update credentials for service %s.", service->name);
        serviceSetUser(service, user, auth);
    }
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
    int ret = 1;

    if (spinlock_acquire_nowait(&service->users_table_spin))
    {
        time_t now = time(NULL);

        /* Check if refresh rate limit has been exceeded */
        if ((now < service->rate_limit.last + USERS_REFRESH_TIME) ||
            (service->rate_limit.nloads > USERS_REFRESH_MAX_PER_TIME))
        {
            MXS_ERROR("[%s] Refresh rate limit exceeded for load of users' table.", service->name);
        }
        else
        {
            service->rate_limit.nloads++;

            /** If we have reached the limit on users refreshes, reset refresh time and count */
            if (service->rate_limit.nloads > USERS_REFRESH_MAX_PER_TIME)
            {
                service->rate_limit.nloads = 1;
                service->rate_limit.last = now;
            }

            ret = 0;

            for (SERV_LISTENER *port = service->ports; port; port = port->next)
            {
                if (port->listener->authfunc.loadusers &&
                    port->listener->authfunc.loadusers(port) != MXS_AUTH_LOADUSERS_OK)
                {
                    MXS_ERROR("[%s] Failed to load users for listener '%s', authentication might not work.",
                              service->name, port->name);
                    ret = 1;
                }
            }
        }

        spinlock_release(&service->users_table_spin);
    }

    return ret;
}

bool service_set_param_value(SERVICE*            service,
                             CONFIG_PARAMETER*   param,
                             char*               valstr,
                             count_spec_t        count_spec,
                             config_param_type_t type)
{
    char* p;
    int valint;
    bool valbool;
    target_t valtarget;
    bool succp = true;

    if (PARAM_IS_TYPE(type, PERCENT_TYPE) || PARAM_IS_TYPE(type, COUNT_TYPE))
    {
        /**
         * Find out whether the value is numeric and ends with '%' or '\0'
         */
        p = valstr;

        while (isdigit(*p))
        {
            p++;
        }

        errno = 0;

        if (p == valstr || (*p != '%' && *p != '\0'))
        {
            succp = false;
        }
        else if (*p == '%')
        {
            if (*(p + 1) == '\0')
            {
                *p = '\0';
                valint = (int) strtol(valstr, (char **)NULL, 10);

                if (valint == 0 && errno != 0)
                {
                    succp = false;
                }
                else if (PARAM_IS_TYPE(type, PERCENT_TYPE))
                {
                    succp = true;
                    config_set_qualified_param(param, (void *)&valint, PERCENT_TYPE);
                }
                else
                {
                    /** Log error */
                }
            }
            else
            {
                succp = false;
            }
        }
        else if (*p == '\0')
        {
            valint = (int) strtol(valstr, (char **)NULL, 10);

            if (valint == 0 && errno != 0)
            {
                succp = false;
            }
            else if (PARAM_IS_TYPE(type, COUNT_TYPE))
            {
                succp = true;
                config_set_qualified_param(param, (void *)&valint, COUNT_TYPE);
            }
            else
            {
                /** Log error */
            }
        }
    }
    else if (type == BOOL_TYPE)
    {
        unsigned int rc;

        rc = find_type(&bool_type, valstr, strlen(valstr) + 1);

        if (rc > 0)
        {
            succp = true;
            if (rc % 2 == 1)
            {
                valbool = false;
            }
            else if (rc % 2 == 0)
            {
                valbool = true;
            }
            /** add param to config */
            config_set_qualified_param(param, (void *)&valbool, BOOL_TYPE);
        }
        else
        {
            succp = false;
        }
    }
    else if (type == SQLVAR_TARGET_TYPE)
    {
        unsigned int rc;

        rc = find_type(&sqlvar_target_type, valstr, strlen(valstr) + 1);

        if (rc > 0 && rc < 3)
        {
            succp = true;
            if (rc == 1)
            {
                valtarget = TYPE_MASTER;
            }
            else if (rc == 2)
            {
                valtarget = TYPE_ALL;
            }
            /** add param to config */
            config_set_qualified_param(param, (void *)&valtarget, SQLVAR_TARGET_TYPE);
        }
        else
        {
            succp = false;
        }
    }

    if (succp)
    {
        service_add_qualified_param(service, param); /*< add param to svc */
    }
    return succp;
}
/*
 * Function to find a string in typelib_t
 * (similar to find_type() of mysys/typelib.c)
 *
 *       SYNOPSIS
 *       find_type()
 *       lib                  typelib_t
 *       find                 String to find
 *       length               Length of string to find
 *       part_match           Allow part matching of value
 *
 *       RETURN
 *       0 error
 *       > 0 position in TYPELIB->type_names +1
 */
static int find_type(typelib_t*  tl,
                     const char* needle,
                     int         maxlen)
{
    int i;

    if (tl == NULL || needle == NULL || maxlen <= 0)
    {
        return -1;
    }

    for (i = 0; i < tl->tl_nelems; i++)
    {
        if (strncasecmp(tl->tl_p_elems[i], needle, maxlen) == 0)
        {
            return i + 1;
        }
    }
    return 0;
}

/**
 * Add qualified config parameter to SERVICE struct.
 */
static void service_add_qualified_param(SERVICE*          svc,
                                        CONFIG_PARAMETER* param)
{
    spinlock_acquire(&svc->spin);

    if (svc->svc_config_param == NULL)
    {
        svc->svc_config_param = config_clone_param(param);
        svc->svc_config_param->next = NULL;
    }
    else
    {
        CONFIG_PARAMETER* p = svc->svc_config_param;
        CONFIG_PARAMETER* prev = NULL;

        while (true)
        {
            CONFIG_PARAMETER* old;

            /** Replace existing parameter in the list, free old */
            if (strncasecmp(param->name,
                            p->name,
                            strlen(param->name)) == 0)
            {
                old = p;
                p = config_clone_param(param);
                p->next = old->next;

                if (prev != NULL)
                {
                    prev->next = p;
                }
                else
                {
                    svc->svc_config_param = p;
                }
                MXS_FREE(old);
                break;
            }
            prev = p;
            p = p->next;

            /** Hit end of the list, add new parameter */
            if (p == NULL)
            {
                p = config_clone_param(param);
                prev->next = p;
                p->next = NULL;
                break;
            }
        }
    }
    /** Increment service's configuration version */
    atomic_add(&svc->svc_config_version, 1);
    spinlock_release(&svc->spin);
}

/**
 * Return the name of the service
 *
 * @param svc           The service
 */
char *
service_get_name(SERVICE *svc)
{
    return svc->name;
}

/**
 * Set the weighting parameter for the service
 *
 * @param       service         The service pointer
 * @param       weightby        The parameter name to weight the routing by
 */
void
serviceWeightBy(SERVICE *service, char *weightby)
{
    if (service->weightby)
    {
        MXS_FREE(service->weightby);
    }
    service->weightby = MXS_STRDUP_A(weightby);
}

/**
 * Return the parameter the wervice shoudl use to weight connections
 * by
 * @param service               The Service pointer
 */
char *
serviceGetWeightingParameter(SERVICE *service)
{
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
serviceEnableLocalhostMatchWildcardHost(SERVICE *service, int action)
{
    if (action != 0 && action != 1)
    {
        return 0;
    }

    service->localhost_match_wildcard_host = action;

    return 1;
}

void service_shutdown()
{
    SERVICE* svc;
    spinlock_acquire(&service_spin);
    svc = allServices;
    while (svc != NULL)
    {
        svc->svc_do_shutdown = true;
        svc = svc->next;
    }
    spinlock_release(&service_spin);
}

/**
 * Return the count of all sessions active for all services
 *
 * @return Count of all active sessions
 */
int
serviceSessionCountAll()
{
    SERVICE *service;
    int rval = 0;

    spinlock_acquire(&service_spin);
    service = allServices;
    while (service)
    {
        rval += service->stats.n_current;
        service = service->next;
    }
    spinlock_release(&service_spin);
    return rval;
}

/**
 * Provide a row to the result set that defines the set of service
 * listeners
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
serviceListenerRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    int i = 0;;
    char buf[20];
    RESULT_ROW *row;
    SERVICE *service;
    SERV_LISTENER *lptr = NULL;

    spinlock_acquire(&service_spin);
    service = allServices;
    if (service)
    {
        lptr = service->ports;
    }
    while (i < *rowno && service)
    {
        lptr = service->ports;
        while (i < *rowno && lptr)
        {
            if ((lptr = lptr->next) != NULL)
            {
                i++;
            }
        }
        if (i < *rowno)
        {
            service = service->next;
            if (service && (lptr = service->ports) != NULL)
            {
                i++;
            }
        }
    }
    if (lptr == NULL)
    {
        spinlock_release(&service_spin);
        MXS_FREE(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, service->name);
    resultset_row_set(row, 1, lptr->protocol);
    resultset_row_set(row, 2, (lptr && lptr->address) ? lptr->address : "*");
    sprintf(buf, "%d", lptr->port);
    resultset_row_set(row, 3, buf);
    resultset_row_set(row, 4,
                      (!lptr->listener || !lptr->listener->session ||
                       lptr->listener->session->state == SESSION_STATE_LISTENER_STOPPED) ?
                      "Stopped" : "Running");
    spinlock_release(&service_spin);
    return row;
}

/**
 * Return a resultset that has the current set of services in it
 *
 * @return A Result set
 */
RESULTSET *
serviceGetListenerList()
{
    RESULTSET *set;
    int *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(serviceListenerRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Service Name", 25, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Protocol Module", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Address", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Port", 5, COL_TYPE_VARCHAR);
    resultset_add_column(set, "State", 8, COL_TYPE_VARCHAR);

    return set;
}

/**
 * Provide a row to the result set that defines the set of services
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
serviceRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    int i = 0;
    char buf[20];
    RESULT_ROW *row;
    SERVICE *service;

    spinlock_acquire(&service_spin);
    service = allServices;
    while (i < *rowno && service)
    {
        i++;
        service = service->next;
    }
    if (service == NULL)
    {
        spinlock_release(&service_spin);
        MXS_FREE(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, service->name);
    resultset_row_set(row, 1, service->routerModule);
    sprintf(buf, "%d", service->stats.n_current);
    resultset_row_set(row, 2, buf);
    sprintf(buf, "%d", service->stats.n_sessions);
    resultset_row_set(row, 3, buf);
    spinlock_release(&service_spin);
    return row;
}

/**
 * Return a result set that has the current set of services in it
 *
 * @return A Result set
 */
RESULTSET *
serviceGetList()
{
    RESULTSET *set;
    int *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(serviceRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Service Name", 25, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Router Module", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "No. Sessions", 10, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Total Sessions", 10, COL_TYPE_VARCHAR);

    return set;
}

/**
 * Function called by the housekeeper thread to retry starting of a service
 * @param data Service to restart
 */
static void service_internal_restart(void *data)
{
    SERVICE* service = (SERVICE*)data;
    serviceStartAllPorts(service);
}

/**
 * Check that all services have listeners
 * @return True if all services have listeners
 */
bool service_all_services_have_listeners()
{
    bool rval = true;
    spinlock_acquire(&service_spin);

    SERVICE* service = allServices;

    while (service)
    {
        if (service->ports == NULL)
        {
            MXS_ERROR("Service '%s' has no listeners.", service->name);
            rval = false;
        }
        service = service->next;
    }

    spinlock_release(&service_spin);
    return rval;
}
