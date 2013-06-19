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
 * Date		Who		Description
 * 18/06/13	Mark Riddoch	Initial implementation
 *
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
	memset(&service->stats, sizeof(SERVICE_STATS), 0);
	service->ports = NULL;
	service->stats.started = time(0);
	service->state = SERVICE_STATE_ALLOC;

	spinlock_acquire(&service_spin);
	service->next = allServices;
	allServices = service;
	spinlock_release(&service_spin);

	return service;
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
 * @param efd		The epoll descriptor
 * @return	Returns the number of listeners created
 */
int
serviceStart(SERVICE *service, int efd)
{
SERV_PROTOCOL	*port;
int		listeners = 0;
char		config_bind[40];
GWPROTOCOL	*funcs;

	service->router_instance = service->router->createInstance(service);

	port = service->ports;
	while (port)
	{
		if ((port->listener = alloc_dcb()) == NULL)
		{
			break;
		}
		if ((funcs = (GWPROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) == NULL)
		{
			free(port->listener);
			port->listener = NULL;
		}
		memcpy(&(port->listener->func), funcs, sizeof(GWPROTOCOL));
		port->listener->session = NULL;
		sprintf(config_bind, "0.0.0.0:%d", port->port);
		if (port->listener->func.listen(port->listener, efd, config_bind))
			listeners++;
		port->listener->session = session_alloc(service, port->listener);
		port->listener->session->state = SESSION_STATE_LISTENER;

		port = port->next;
	}
	if (listeners)
		service->stats.started = time(0);

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
 * Add a backend database server to a service
 *
 * @param service
 * @param server
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
	printf("\tRouter:		%s (%p)\n", service->routerModule, service->router);
	printf("\tStarted:		%s", asctime(localtime(&service->stats.started)));
	printf("\tBackend databases\n");
	while (ptr)
	{
		printf("\t\t%s:%d  %s\n", ptr->name, ptr->port, ptr->protocol);
		ptr = ptr->next;
	}
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
		dcb_printf(dcb, "\tRouter:		%s (%p)\n", ptr->routerModule,
										ptr->router);
		dcb_printf(dcb, "\tStarted:		%s",
						asctime(localtime(&ptr->stats.started)));
		dcb_printf(dcb, "\tBackend databases\n");
		while (server)
		{
			dcb_printf(dcb, "\t\t%s:%d  %s\n", server->name, server->port,
									server->protocol);
			server = server->next;
		}
		dcb_printf(dcb, "\tTotal connections:	%d\n", ptr->stats.n_sessions);
		dcb_printf(dcb, "\tCurrently connected:	%d\n", ptr->stats.n_current);
		ptr = ptr->next;
	}
	spinlock_release(&service_spin);
}
