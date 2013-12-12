/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright SkySQL Ab 2013
 */

/**
 * @file service.c  - A representation of the service within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 18/06/13	Mark Riddoch		Initial implementation
 * 24/06/13	Massimiliano Pinto	Added: Loading users from mysql backend in serviceStart
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <session.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <spinlock.h>
#include <modules.h>
#include <dcb.h>
#include <users.h>
#include <dbusers.h>
#include <poll.h>
#include <skygw_utils.h>
#include <log_manager.h>

extern int lm_enabled_logfiles_bitmask;

static SPINLOCK	service_spin = SPINLOCK_INIT;
static SERVICE	*allServices = NULL;

/**
 * Allocate a new service for the gateway to support
 *
 *
 * @param servname	The service name
 * @param router	Name of the router module this service uses
 *
 * @return		The newly created service or NULL if an error occured
 */
SERVICE *
service_alloc(char *servname, char *router)
{
SERVICE 	*service;

	if ((service = (SERVICE *)malloc(sizeof(SERVICE))) == NULL)
		return NULL;
	if ((service->router = load_module(router, MODULE_ROUTER)) == NULL)
	{
		free(service);
		return NULL;
	}
	service->name = strdup(servname);
	service->routerModule = strdup(router);
	memset(&service->stats, 0, sizeof(SERVICE_STATS));
	service->ports = NULL;
	service->stats.started = time(0);
	service->state = SERVICE_STATE_ALLOC;
	service->credentials.name = NULL;
	service->credentials.authdata = NULL;
	service->users = users_alloc();
	service->routerOptions = NULL;
	service->databases = NULL;
	spinlock_init(&service->spin);

	spinlock_acquire(&service_spin);
	service->next = allServices;
	allServices = service;
	spinlock_release(&service_spin);

	return service;
}

/**
 * Start an individual port/protocol pair
 *
 * @param service	The service
 * @param port		The port to start
 * @return		The number of listeners started
 */
static int
serviceStartPort(SERVICE *service, SERV_PROTOCOL *port)
{
int		listeners = 0;
char		config_bind[40];
GWPROTOCOL	*funcs;

        port->listener = dcb_alloc(DCB_ROLE_SERVICE_LISTENER);

        if (port->listener == NULL)
	{
		return 0;
	}
	if (strcmp(port->protocol, "MySQLClient") == 0) {
		int loaded = load_mysql_users(service);
		LOGIF(LM, (skygw_log_write(
                        LOGFILE_MESSAGE,
                        "Loaded %d MySQL Users.",
                        loaded)));
	}

	if ((funcs =
             (GWPROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) == NULL)
	{
		free(port->listener);
		port->listener = NULL;
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
			"Error : Unable to load protocol module %s. Listener "
                        "for service %s not started.",
			port->protocol,
                        service->name)));
		return 0;
	}
	memcpy(&(port->listener->func), funcs, sizeof(GWPROTOCOL));
	port->listener->session = NULL;
	sprintf(config_bind, "0.0.0.0:%d", port->port);

	if (port->listener->func.listen(port->listener, config_bind)) {
                port->listener->session = session_alloc(service, port->listener);
                
                if (port->listener->session != NULL) {
                        port->listener->session->state = SESSION_STATE_LISTENER;
                        listeners += 1;
                } else {
                        dcb_close(port->listener);
                }
        } else {
                dcb_close(port->listener);
                
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
			"Error : Unable to start to listen port %d for %s %s.",
			port->port,
                        port->protocol,
                        service->name)));
        }
	return listeners;
}

/**
 * Start a service
 *
 * This function loads the protocol modules for each port on which the
 * service listens and starts the listener on that port
 *
 * Also create the router_instance for the service.
 *
 * @param service	The Service that should be started
 * @return	Returns the number of listeners created
 */
int
serviceStart(SERVICE *service)
{
SERV_PROTOCOL	*port;
int		listeners = 0;

	service->router_instance = service->router->createInstance(service,
					service->routerOptions);

	port = service->ports;
	while (port)
	{
		listeners += serviceStartPort(service, port);
		port = port->next;
	}
	if (listeners)
		service->stats.started = time(0);

	return listeners;
}

/**
 * Start an individual listener
 *
 * @param service	The service to start the listener for
 * @param protocol	The name of the protocol
 * @param port		The port number
 */
void
serviceStartProtocol(SERVICE *service, char *protocol, int port)
{
SERV_PROTOCOL	*ptr;

	ptr = service->ports;
	while (ptr)
	{
		if (strcmp(ptr->protocol, protocol) == 0 && ptr->port == port)
			serviceStartPort(service, ptr);
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
SERVICE	*ptr;
int	n = 0;

	ptr = allServices;
	while (ptr)
	{
		n += serviceStart(ptr);
		ptr = ptr->next;
	}
	return n;
}

/**
 * Stop a service
 *
 * This function stops the listener for the service
 *
 * @param service	The Service that should be stopped
 * @return	Returns the number of listeners restarted
 */
int
serviceStop(SERVICE *service)
{
SERV_PROTOCOL	*port;
int		listeners = 0;

	port = service->ports;
	while (port)
	{
		poll_remove_dcb(port->listener);
		port->listener->session->state = SESSION_STATE_LISTENER_STOPPED;
		listeners++;

		port = port->next;
	}

	return listeners;
}

/**
 * Restart a service
 *
 * This function stops the listener for the service
 *
 * @param service	The Service that should be restarted
 * @return	Returns the number of listeners restarted
 */
int
serviceRestart(SERVICE *service)
{
SERV_PROTOCOL	*port;
int		listeners = 0;

	port = service->ports;
	while (port)
	{
                if (poll_add_dcb(port->listener) == 0) {
                        port->listener->session->state = SESSION_STATE_LISTENER;
                        listeners++;
                }
		port = port->next;
	}

	return listeners;
}


/**
 * Deallocate the specified service
 *
 * @param service	The service to deallocate
 * @return	Returns true if the service was freed
 */
int
service_free(SERVICE *service)
{
SERVICE *ptr;

	if (service->stats.n_current)
		return 0;
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
			ptr->next = service->next;
	}
	spinlock_release(&service_spin);

	/* Clean up session and free the memory */
	free(service->name);
	free(service->routerModule);
	if (service->credentials.name)
		free(service->credentials.name);
	if (service->credentials.authdata)
		free(service->credentials.authdata);
	free(service);
	return 1;
}

/**
 * Add a protocol/port pair to the service
 *
 * @param service	The service
 * @param protocol	The name of the protocol module
 * @param port		The port to listen on
 * @return	TRUE if the protocol/port could be added
 */
int
serviceAddProtocol(SERVICE *service, char *protocol, unsigned short port)
{
SERV_PROTOCOL	*proto;

	if ((proto = (SERV_PROTOCOL *)malloc(sizeof(SERV_PROTOCOL))) == NULL)
	{
		return 0;
	}
	proto->protocol = strdup(protocol);
	proto->port = port;
	spinlock_acquire(&service->spin);
	proto->next = service->ports;
	service->ports = proto;
	spinlock_release(&service->spin);

	return 1;
}

/**
 * Check if a protocol/port pair si part of the service
 *
 * @param service	The service
 * @param protocol	The name of the protocol module
 * @param port		The port to listen on
 * @return	TRUE if the protocol/port is already part of the service
 */
int
serviceHasProtocol(SERVICE *service, char *protocol, unsigned short port)
{
SERV_PROTOCOL	*proto;

	spinlock_acquire(&service->spin);
	proto = service->ports;
	while (proto)
	{
		if (strcmp(proto->protocol, protocol) == 0 && proto->port == port)
			break;
		proto = proto->next;
	}
	spinlock_release(&service->spin);

	return proto != NULL;
}

/**
 * Add a backend database server to a service
 *
 * @param service	The service to add the server to
 * @param server	The server to add
 */
void
serviceAddBackend(SERVICE *service, SERVER *server)
{
	spinlock_acquire(&service->spin);
	server->nextdb = service->databases;
	service->databases = server;
	spinlock_release(&service->spin);
}

/**
 * Test if a server is part of a service
 *
 * @param service	The service to add the server to
 * @param server	The server to add
 * @return		Non-zero if the server is already part of the service
 */
int
serviceHasBackend(SERVICE *service, SERVER *server)
{
SERVER	*ptr;

	spinlock_acquire(&service->spin);
	ptr = service->databases;
	while (ptr && ptr != server)
		ptr = ptr->nextdb;
	spinlock_release(&service->spin);

	return ptr != NULL;
}

/**
 * Add a router option to a service
 *
 * @param service	The service to add the router option to
 * @param option	The option string
 */
void
serviceAddRouterOption(SERVICE *service, char *option)
{
int	i;

	spinlock_acquire(&service->spin);
	if (service->routerOptions == NULL)
	{
		service->routerOptions = (char **)calloc(2, sizeof(char *));
		service->routerOptions[0] = strdup(option);
		service->routerOptions[1] = NULL;
	}
	else
	{
		for (i = 0; service->routerOptions[i]; i++)
			;
		service->routerOptions = (char **)realloc(service->routerOptions,
				(i + 2) * sizeof(char *));
		service->routerOptions[i] = strdup(option);
		service->routerOptions[i+1] = NULL;
	}
	spinlock_release(&service->spin);
}

/**
 * Remove the router options for the service
 *
 * @param	service	The service to remove the options from
 */
void
serviceClearRouterOptions(SERVICE *service)
{
int	i;

	spinlock_acquire(&service->spin);
	for (i = 0; service->routerOptions[i]; i++)
		free(service->routerOptions[i]);
	free(service->routerOptions);
	service->routerOptions = NULL;
	spinlock_release(&service->spin);
}
/**
 * Set the service user that is used to log in to the backebd servers
 * associated with this service.
 *
 * @param service	The service we are setting the data for
 * @param user		The user name to use for connections
 * @param auth		The authentication data we need, e.g. MySQL SHA1 password
 * @return	0 on failure
 */
int
serviceSetUser(SERVICE *service, char *user, char *auth)
{
	if (service->credentials.name)
		free(service->credentials.name);
	if (service->credentials.authdata)
		free(service->credentials.authdata);
	service->credentials.name = strdup(user);
	service->credentials.authdata = strdup(auth);

	if (service->credentials.name == NULL || service->credentials.authdata == NULL)
		return 0;
	return 1;
}


/**
 * Get the service user that is used to log in to the backebd servers
 * associated with this service.
 *
 * @param service	The service we are setting the data for
 * @param user		The user name to use for connections
 * @param auth		The authentication data we need, e.g. MySQL SHA1 password
 * @return	0 on failure
 */
int
serviceGetUser(SERVICE *service, char **user, char **auth)
{
	if (service->credentials.name == NULL || service->credentials.authdata == NULL)
		return 0;
	*user = service->credentials.name;
	*auth = service->credentials.authdata;
	return 1;
}

/**
 * Return a named service
 *
 * @param servname	The name of the service to find
 * @return The service or NULL if not found
 */
SERVICE *
service_find(char *servname)
{
SERVICE 	*service;

	spinlock_acquire(&service_spin);
	service = allServices;
	while (service && strcmp(service->name, servname) != 0)
		service = service->next;
	spinlock_release(&service_spin);

	return service;
}


/**
 * Print details of an individual service
 *
 * @param service	Service to print
 */
void
printService(SERVICE *service)
{
SERVER	*ptr = service->databases;

	printf("Service %p\n", service);
	printf("\tService:		%s\n", service->name);
	printf("\tRouter:			%s (%p)\n", service->routerModule, service->router);
	printf("\tStarted:		%s", asctime(localtime(&service->stats.started)));
	printf("\tBackend databases\n");
	while (ptr)
	{
		printf("\t\t%s:%d  Protocol: %s\n", ptr->name, ptr->port, ptr->protocol);
		ptr = ptr->nextdb;
	}
	printf("\tUsers data:        	%p\n", service->users);
	printf("\tTotal connections:	%d\n", service->stats.n_sessions);
	printf("\tCurrently connected:	%d\n", service->stats.n_current);
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
SERVICE	*ptr;

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
 * Designed to be called within a debugger session in order
 * to display all active services within the gateway
 */
void
dprintAllServices(DCB *dcb)
{
SERVICE	*ptr;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	while (ptr)
	{
		SERVER	*server = ptr->databases;
		dcb_printf(dcb, "Service %p\n", ptr);
		dcb_printf(dcb, "\tService:		%s\n", ptr->name);
		dcb_printf(dcb, "\tRouter:			%s (%p)\n", ptr->routerModule,
										ptr->router);
		if (ptr->router)
			ptr->router->diagnostics(ptr->router_instance, dcb);
		dcb_printf(dcb, "\tStarted:		%s",
						asctime(localtime(&ptr->stats.started)));
		dcb_printf(dcb, "\tBackend databases\n");
		while (server)
		{
			dcb_printf(dcb, "\t\t%s:%d  Protocol: %s\n", server->name, server->port,
									server->protocol);
			server = server->nextdb;
		}
		dcb_printf(dcb, "\tUsers data:        	%p\n", ptr->users);
		dcb_printf(dcb, "\tTotal connections:	%d\n", ptr->stats.n_sessions);
		dcb_printf(dcb, "\tCurrently connected:	%d\n", ptr->stats.n_current);
		ptr = ptr->next;
	}
	spinlock_release(&service_spin);
}

/**
 * Update the definition of a service
 *
 * @param service	The service to update
 * @param router	The router module to use
 * @param user		The user to use to extract information from the database
 * @param auth		The password for the user above
 */
void
service_update(SERVICE *service, char *router, char *user, char *auth)
{
void	*router_obj;

	if (!strcmp(service->routerModule, router))
	{
		if ((router_obj = load_module(router, MODULE_ROUTER)) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Failed to update router "
                                "for service %s to %s.",
				service->name,
                                router)));
		}
		else
		{
			LOGIF(LM, (skygw_log_write(
                                LOGFILE_MESSAGE,
                                "Update router for service %s to %s.",
				service->name,
                                router)));
			free(service->routerModule);
			service->routerModule = strdup(router);
			service->router = router_obj;
		}
	}
	if (user &&
            (strcmp(service->credentials.name, user) != 0 ||
             strcmp(service->credentials.authdata, auth) != 0))
	{
		LOGIF(LM, (skygw_log_write(
                        LOGFILE_MESSAGE,
                        "Update credentials for service %s.",
                        service->name)));
		serviceSetUser(service, user, auth);
	}
}
