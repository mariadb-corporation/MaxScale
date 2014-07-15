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
 * 06/02/14	Massimiliano Pinto	Added: serviceEnableRootUser routine
 * 25/02/14	Massimiliano Pinto	Added: service refresh limit feature
 * 28/02/14	Massimiliano Pinto	users_alloc moved from service_alloc to serviceStartPort (generic hashable for services)
 * 07/05/14	Massimiliano Pinto	Added: version_string initialized to NULL
 * 23/05/14	Mark Riddoch		Addition of service validation call
 * 29/05/14	Mark Riddoch		Filter API implementation
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <session.h>
#include <service.h>
#include <server.h>
#include <router.h>
#include <spinlock.h>
#include <modules.h>
#include <dcb.h>
#include <users.h>
#include <filter.h>
#include <dbusers.h>
#include <poll.h>
#include <skygw_utils.h>
#include <log_manager.h>

extern int lm_enabled_logfiles_bitmask;

static SPINLOCK	service_spin = SPINLOCK_INIT;
static SERVICE	*allServices = NULL;

static void service_add_qualified_param(
        SERVICE*          svc,
        CONFIG_PARAMETER* param);


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
                char* home = get_maxscale_home();
                char* ldpath = getenv("LD_LIBRARY_PATH");
                
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Unable to load %s module \"%s\".\n\t\t\t"
                        "      Ensure that lib%s.so exists in one of the "
                        "following directories :\n\t\t\t      "
                        "- %s/modules\n\t\t\t      - %s",
                        MODULE_ROUTER,
                        router,
                        router,
                        home,
                        ldpath)));
		free(service);
		return NULL;
	}
	service->name = strdup(servname);
	service->routerModule = strdup(router);
	service->version_string = NULL;
	memset(&service->stats, 0, sizeof(SERVICE_STATS));
	service->ports = NULL;
	service->stats.started = time(0);
	service->state = SERVICE_STATE_ALLOC;
	service->credentials.name = NULL;
	service->credentials.authdata = NULL;
	service->enable_root = 0;
	service->routerOptions = NULL;
	service->databases = NULL;
        service->svc_config_param = NULL;
        service->svc_config_version = 0;
	service->filters = NULL;
	service->n_filters = 0;
	service->weightby = 0;
	spinlock_init(&service->spin);
	spinlock_init(&service->users_table_spin);
	memset(&service->rate_limit, 0, sizeof(SERVICE_REFRESH_RATE));

	spinlock_acquire(&service_spin);
	service->next = allServices;
	allServices = service;
	spinlock_release(&service_spin);

	return service;
}

/**
 * Check to see if a service pointer is valid
 *
 * @param service	The poitner to check
 * @return 1 if the service is in the list of all services
 */
int
service_isvalid(SERVICE *service)
{
SERVICE		*ptr;
int		rval = 0;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	while (ptr)
	{
		if (ptr == service)
		{
			rval = 1;
			break;
		}
		ptr = ptr->next;
	}
	spinlock_release(&service_spin);
	return rval;
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
		int loaded;
		/* Allocate specific data for MySQL users */
		service->users = mysql_users_alloc();
		loaded = load_mysql_users(service);
		/* At service start last update is set to USERS_REFRESH_TIME seconds earlier.
 		 * This way MaxScale could try reloading users' just after startup
 		 */

		service->rate_limit.last=time(NULL) - USERS_REFRESH_TIME;
		service->rate_limit.nloads=1;

		LOGIF(LM, (skygw_log_write(
                        LOGFILE_MESSAGE,
                        "Loaded %d MySQL Users.",
                        loaded)));
	} else {
		/* Generic users table */
		service->users = users_alloc();
	}

	if ((funcs =
             (GWPROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) == NULL)
	{
		dcb_free(port->listener);
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
	if (port->address)
		sprintf(config_bind, "%s:%d", port->address, port->port);
	else
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
 * @param address	The address to listen with
 * @param port		The port to listen on
 * @return	TRUE if the protocol/port could be added
 */
int
serviceAddProtocol(SERVICE *service, char *protocol, char *address, unsigned short port)
{
SERV_PROTOCOL	*proto;

	if ((proto = (SERV_PROTOCOL *)malloc(sizeof(SERV_PROTOCOL))) == NULL)
	{
		return 0;
	}
	proto->protocol = strdup(protocol);
	if (address)
		proto->address = strdup(address);
	else
		proto->address = NULL;
	proto->port = port;
	spinlock_acquire(&service->spin);
	proto->next = service->ports;
	service->ports = proto;
	spinlock_release(&service->spin);

	return 1;
}

/**
 * Check if a protocol/port pair is part of the service
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
	if (service->routerOptions != NULL)
	{
		for (i = 0; service->routerOptions[i]; i++)
			free(service->routerOptions[i]);
		free(service->routerOptions);
		service->routerOptions = NULL;
	}
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
 * @return		0 on failure
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
 * Enable/Disable root user for this service
 * associated with this service.
 *
 * @param service	The service we are setting the data for
 * @param action	1 for root enable, 0 for disable access
 * @return		0 on failure
 */

int
serviceEnableRootUser(SERVICE *service, int action)
{
	if (action != 0 && action != 1)
		return 0;

	service->enable_root = action;

	return 1;
}

/**
 * Trim whitespace from the from an rear of a string
 *
 * @param str		String to trim
 * @return	Trimmed string, chanesg are done in situ
 */
static char *
trim(char *str)
{
char	*ptr;

	while (isspace(*str))
		str++;

	/* Point to last character of the string */
	ptr = str + strlen(str) - 1;
	while (ptr > str && isspace(*ptr))
		*ptr-- = 0;

	return str;
}

/**
 * Set the filters used by the service
 *
 * @param service	The service itself
 * @param filters	ASCII string of filters to use
 */
void
serviceSetFilters(SERVICE *service, char *filters)
{
FILTER_DEF	**flist;
char		*ptr, *brkt;
int		n = 0;

	if ((flist = (FILTER_DEF **)malloc(sizeof(FILTER_DEF *))) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"Out of memory adding filters to service.\n")));
		return;
	}
	ptr = strtok_r(filters, "|", &brkt);
	while (ptr)
	{
		n++;
		if ((flist = (FILTER_DEF **)realloc(flist,
				(n + 1) * sizeof(FILTER_DEF *))) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"Out of memory adding filters to service.\n")));
			return;
		}
		if ((flist[n-1] = filter_find(trim(ptr))) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
				"Unable to find filter '%s' for service '%s'\n",
					trim(ptr), service->name
					)));
		}
		flist[n] = NULL;
		ptr = strtok_r(NULL, "|", &brkt);
	}

	service->filters = flist;
	service->n_filters = n;
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
int	i;

	printf("Service %p\n", service);
	printf("\tService:				%s\n", service->name);
	printf("\tRouter:				%s (%p)\n", service->routerModule, service->router);
	printf("\tStarted:		%s", asctime(localtime(&service->stats.started)));
	printf("\tBackend databases\n");
	while (ptr)
	{
		printf("\t\t%s:%d  Protocol: %s\n", ptr->name, ptr->port, ptr->protocol);
		ptr = ptr->nextdb;
	}
	if (service->n_filters)
	{
		printf("\tFilter chain:		");
		for (i = 0; i < service->n_filters; i++)
		{
			printf("%s %s ", service->filters[i]->name,
				i + 1 < service->n_filters ? "|" : "");
		}
		printf("\n");
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
 * Designed to be called within a CLI command in order
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
		dprintService(dcb, ptr);
		ptr = ptr->next;
	}
	spinlock_release(&service_spin);
}

/**
 * Print details of a single service.
 *
 * @param dcb		DCB to print data to
 * @param service	The service to print
 */
void dprintService(DCB *dcb, SERVICE *service)
{
SERVER	*server = service->databases;
int	i;

	dcb_printf(dcb, "Service %p\n", service);
	dcb_printf(dcb, "\tService:				%s\n",
						service->name);
	dcb_printf(dcb, "\tRouter: 				%s (%p)\n",
			service->routerModule, service->router);
	if (service->router)
		service->router->diagnostics(service->router_instance, dcb);
	dcb_printf(dcb, "\tStarted:				%s",
					asctime(localtime(&service->stats.started)));
	dcb_printf(dcb, "\tRoot user access:			%s\n",
			service->enable_root ? "Enabled" : "Disabled");
	if (service->n_filters)
	{
		dcb_printf(dcb, "\tFilter chain:		");
		for (i = 0; i < service->n_filters; i++)
		{
			dcb_printf(dcb, "%s %s ", service->filters[i]->name,
				i + 1 < service->n_filters ? "|" : "");
		}
		dcb_printf(dcb, "\n");
	}
	dcb_printf(dcb, "\tBackend databases\n");
	while (server)
	{
		dcb_printf(dcb, "\t\t%s:%d  Protocol: %s\n", server->name, server->port,
								server->protocol);
		server = server->nextdb;
	}
	if (service->weightby)
		dcb_printf(dcb, "\tRouting weight parameter:		%s\n",
							service->weightby);
	dcb_printf(dcb, "\tUsers data:        			%p\n",
						service->users);
	dcb_printf(dcb, "\tTotal connections:			%d\n",
						service->stats.n_sessions);
	dcb_printf(dcb, "\tCurrently connected:			%d\n",
						service->stats.n_current);
}

/**
 * List the defined services in a tabular format.
 *
 * @param dcb		DCB to print the service list to.
 */
void
dListServices(DCB *dcb)
{
SERVICE	*ptr;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	if (ptr)
	{
		dcb_printf(dcb, "Services.\n");
		dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n");
		dcb_printf(dcb, "%-25s | %-20s | #Users | Total Sessions\n",
			"Service Name", "Router Module");
		dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n");
	}
	while (ptr)
	{
		dcb_printf(dcb, "%-25s | %-20s | %6d | %5d\n",
			ptr->name, ptr->routerModule,
			ptr->stats.n_current, ptr->stats.n_sessions);
		ptr = ptr->next;
	}
	if (allServices)
		dcb_printf(dcb, "--------------------------+----------------------+--------+---------------\n\n");
	spinlock_release(&service_spin);
}

/**
 * List the defined listeners in a tabular format.
 *
 * @param dcb		DCB to print the service list to.
 */
void
dListListeners(DCB *dcb)
{
SERVICE		*ptr;
SERV_PROTOCOL	*lptr;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	if (ptr)
	{
		dcb_printf(dcb, "Listeners.\n");
		dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n");
		dcb_printf(dcb, "%-20s | %-18s | %-15s | Port  | State\n",
			"Service Name", "Protocol Module", "Address");
		dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n");
	}
	while (ptr)
	{
		lptr = ptr->ports;
		while (lptr)
		{
			dcb_printf(dcb, "%-20s | %-18s | %-15s | %5d | %s\n",
				ptr->name, lptr->protocol, 
				(lptr && lptr->address) ? lptr->address : "*",
				lptr->port,
				(lptr->listener->session->state == SESSION_STATE_LISTENER_STOPPED) ? "Stopped" : "Running"
			);

			lptr = lptr->next;
		}
		ptr = ptr->next;
	}
	if (allServices)
		dcb_printf(dcb, "---------------------+--------------------+-----------------+-------+--------\n\n");
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


int service_refresh_users(SERVICE *service) {
	int ret = 1;
	/* check for another running getUsers request */
	if (! spinlock_acquire_nowait(&service->users_table_spin)) {
		LOGIF(LD, (skygw_log_write_flush(
			LOGFILE_DEBUG,
			"%lu [service_refresh_users] failed to get get lock for loading new users' table: another thread is loading users",
			pthread_self())));

		return 1;
	}

	
	/* check if refresh rate limit has exceeded */
	if ( (time(NULL) < (service->rate_limit.last + USERS_REFRESH_TIME)) || (service->rate_limit.nloads > USERS_REFRESH_MAX_PER_TIME)) { 
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"%lu [service_refresh_users] refresh rate limit exceeded loading new users' table",
			pthread_self())));

		spinlock_release(&service->users_table_spin);
 		return 1;
	}

	service->rate_limit.nloads++;	

	/* update time and counter */
	if (service->rate_limit.nloads > USERS_REFRESH_MAX_PER_TIME) {
		service->rate_limit.nloads = 1;
		service->rate_limit.last = time(NULL);
	}

	ret = replace_mysql_users(service);

	/* remove lock */
	spinlock_release(&service->users_table_spin);

	if (ret >= 0)
		return 0;
	else
		return 1;
}

bool service_set_param_value (
        SERVICE*            service,
        CONFIG_PARAMETER*   param,
        char*               valstr,
        count_spec_t        count_spec,
        config_param_type_t type)
{
        char* p;
        int   valint;
        bool  succp;
        
        /**
         * Find out whether the value is numeric and ends with '%' or '\0'
         */
        p = valstr;
        
        while(isdigit(*p)) p++;

        errno = 0;
        
        if (p == valstr || (*p != '%' && *p != '\0'))
        {
                succp = false;
        }
        else if (*p == '%')
        {
                if (*(p+1) == '\0')
                {
                        *p = '\0';
                        valint = (int) strtol(valstr, (char **)NULL, 10);
                        
                        if (valint == 0 && errno != 0)
                        {
                                succp = false;
                        }
                        else if (PARAM_IS_TYPE(type,PERCENT_TYPE))
                        {
                                succp   = true;
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
                else if (PARAM_IS_TYPE(type,COUNT_TYPE))
                {
                        succp = true;
                        config_set_qualified_param(param, (void *)&valint, COUNT_TYPE);
                }
                else
                {
                        /** Log error */
                }
        }
        
        if (succp)
        {
                service_add_qualified_param(service, param); /*< add param to svc */
        }
        return succp;
}

/**
 * Add qualified config parameter to SERVICE struct.
 */
static void service_add_qualified_param(
        SERVICE*          svc,
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
                CONFIG_PARAMETER*  p = svc->svc_config_param;
                CONFIG_PARAMETER*  prev = NULL;
                
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
                                free(old);
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
 * @param svc		The service
 */
char *
service_get_name(SERVICE *svc)
{
        return svc->name;
}

/**
 * Set the weighting parameter for the service
 *
 * @param	service		The service pointer
 * @param	weightby	The parameter name to weight the routing by
 */
void
serviceWeightBy(SERVICE *service, char *weightby)
{
	if (service->weightby)
		free(service->weightby);
	service->weightby = strdup(weightby);
}

/**
 * Return the parameter the wervice shoudl use to weight connections
 * by
 * @param service		The Service pointer
 */
char *
serviceGetWeightingParameter(SERVICE *service)
{
	return service->weightby;
}

/**
 * Iterate over the services, calling a function per call
 *
 * @param fcn	The function to call
 * @param data	The data to pass to each call
 */
void
serviceIterate(void (*fcn)(SERVICE *, void *), void *data)
{
SERVICE		*service, *next;

	spinlock_acquire(&service_spin);
	service = allServices;
	while (service)
	{
		next = service->next;
		spinlock_release(&service_spin);
		(*fcn)(service, data);
		spinlock_acquire(&service_spin);
		service = next;
	}
	spinlock_release(&service_spin);

}
