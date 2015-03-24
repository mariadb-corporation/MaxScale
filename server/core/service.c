/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
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
 * 09/09/14	Massimiliano Pinto	Added service option for localhost authentication
 * 13/10/14	Massimiliano Pinto	Added hashtable for resources (i.e database names for MySQL services)
 * 06/02/15	Mark Riddoch		Added caching of authentication data
 * 18/02/15	Mark Riddoch		Added result set management
 * 03/03/15	Massimiliano Pinto	Added config_enable_feedback_task() call in serviceStartAll
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
#include <sys/stat.h>
#include <sys/types.h>
#include <housekeeper.h>
#include <resultset.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

/** To be used with configuration type checks */
typedef struct typelib_st {
	int          tl_nelems;
	const char*  tl_name;
	const char** tl_p_elems;
} typelib_t;
/** Set of subsequent false,true pairs */
static const char* bool_strings[11]  = {"FALSE", "TRUE", "OFF", "ON", "N", "Y", "0", "1", "NO", "YES", 0};
typelib_t bool_type   = {array_nelems(bool_strings)-1, "bool_type", bool_strings};

/** List of valid values */
static const char* sqlvar_target_strings[4] = {"MASTER", "ALL", 0};
typelib_t sqlvar_target_type = {
	array_nelems(sqlvar_target_strings)-1, 
	"sqlvar_target_type", 
	sqlvar_target_strings
};

static SPINLOCK	service_spin = SPINLOCK_INIT;
static SERVICE	*allServices = NULL;

static int find_type(typelib_t* tl, const char* needle, int maxlen);

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
 * @return		The newly created service or NULL if an error occurred
 */
SERVICE *
service_alloc(const char *servname, const char *router)
{
SERVICE 	*service;

	if ((service = (SERVICE *)calloc(1, sizeof(SERVICE))) == NULL)
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
	service->users_from_all = false;
	service->resources = NULL;
	
	if (service->name == NULL || service->routerModule == NULL)
	{
		if (service->name)
			free(service->name);
		free(service);
		return NULL;
	}
	service->stats.started = time(0);
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
 * @param service	The pointer to check
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
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Error : Failed to create listener for service %s.",
			service->name)));
		goto retblock;
	}
	
	if (strcmp(port->protocol, "MySQLClient") == 0) {
		int loaded;

		if (service->users == NULL) {
			/*
			 * Allocate specific data for MySQL users
			 * including hosts and db names
			 */
			service->users = mysql_users_alloc();
	
			if ((loaded = load_mysql_users(service)) < 0)
			{
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Unable to load users from %s:%d for "
					"service %s.",
					(port->address == NULL ? "0.0.0.0" : port->address),
					port->port,
					service->name)));
				
				{
					/* Try loading authentication data from file cache */
					char	*ptr, path[4097];
					strcpy(path, "/usr/local/mariadb-maxscale");
					if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
					{
						strncpy(path, ptr, 4096);
					}
					strncat(path, "/", 4096);
					strncat(path, service->name, 4096);
					strncat(path, "/.cache/dbusers", 4096);
					loaded = dbusers_load(service->users, path);
					if (loaded != -1)
					{
						LOGIF(LE, (skygw_log_write_flush(
							LOGFILE_ERROR,
							"Using cached credential information.")));
					}
				}
				if (loaded == -1)
				{
					hashtable_free(service->users->data);
					free(service->users);
					dcb_free(port->listener);
					port->listener = NULL;
					goto retblock;
				}
			}
			else
			{
				/* Save authentication data to file cache */
				char	*ptr, path[4097];
                                int mkdir_rval = 0;
				strcpy(path, "/usr/local/mariadb-maxscale");
				if ((ptr = getenv("MAXSCALE_HOME")) != NULL)
				{
					strncpy(path, ptr, 4096);
				}
				strncat(path, "/", 4096);
				strncat(path, service->name, 4096);
				if (access(path, R_OK) == -1)
                                {
					mkdir_rval = mkdir(path, 0777);
                                }

                                if(mkdir_rval)
                                {
                                    skygw_log_write(LOGFILE_ERROR,"Error : Failed to create directory '%s': [%d] %s",
                                                    path,
                                                    errno,
                                                    strerror(errno));
                                    mkdir_rval = 0;
                                }

				strncat(path, "/.cache", 4096);
				if (access(path, R_OK) == -1)
                                {
					mkdir_rval = mkdir(path, 0777);
                                }

                                if(mkdir_rval)
                                {
                                    skygw_log_write(LOGFILE_ERROR,"Error : Failed to create directory '%s': [%d] %s",
                                                    path,
                                                    errno,
                                                    strerror(errno));
                                    mkdir_rval = 0;
                                }
				strncat(path, "/dbusers", 4096);
				dbusers_save(service->users, path);
			}
			if (loaded == 0)
			{
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Service %s: failed to load any user "
					"information. Authentication will "
					"probably fail as a result.",
					service->name)));
			}

			/* At service start last update is set to USERS_REFRESH_TIME seconds earlier.
 			 * This way MaxScale could try reloading users' just after startup
 			 */
			service->rate_limit.last=time(NULL) - USERS_REFRESH_TIME;
			service->rate_limit.nloads=1;

			LOGIF(LM, (skygw_log_write(
				LOGFILE_MESSAGE,
				"Loaded %d MySQL Users for service [%s].",
				loaded, service->name)));
		}
	} 
	else 
	{
		if (service->users == NULL) {
			/* Generic users table */
			service->users = users_alloc();
		}
	}

	if ((funcs=(GWPROTOCOL *)load_module(port->protocol, MODULE_PROTOCOL)) 
		== NULL)
	{
		if (service->users->data)
		{
			hashtable_free(service->users->data);
		}
		free(service->users);
		dcb_free(port->listener);
		port->listener = NULL;
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
			"Error : Unable to load protocol module %s. Listener "
                        "for service %s not started.",
			port->protocol,
                        service->name)));
		goto retblock;
	}
	memcpy(&(port->listener->func), funcs, sizeof(GWPROTOCOL));
	port->listener->session = NULL;
	
	if (port->address)
		sprintf(config_bind, "%s:%d", port->address, port->port);
	else
		sprintf(config_bind, "0.0.0.0:%d", port->port);

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
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Error : Failed to create session to service %s.",
				service->name)));
			
			if (service->users->data)
			{
				hashtable_free(service->users->data);
			}
			free(service->users);
                        dcb_close(port->listener);
			port->listener = NULL;
			goto retblock;
                }
        } 
        else 
	{       
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
			"Error : Unable to start to listen port %d for %s %s.",
			port->port,
                        port->protocol,
                        service->name)));
		if (service->users->data)
		{
			hashtable_free(service->users->data);
		}
		free(service->users);
		dcb_close(port->listener);
		port->listener = NULL;
        }
        
retblock:
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

	if ((service->router_instance = service->router->createInstance(service,
					service->routerOptions)) == NULL)
	{
		LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
			"%s: Failed to create router instance for service. Service not started.",
				service->name)));
		service->state = SERVICE_STATE_FAILED;
		return 0;
	}

	port = service->ports;
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

	/** Add the task that monitors session timeouts */
	if(service->conn_timeout > 0)
	{
	    hktask_add("connection_timeout",session_close_timeouts,NULL,5);
	}

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
int	n = 0,i;

	config_enable_feedback_task();

	ptr = allServices;
	while (ptr && !ptr->svc_do_shutdown)
	{
		n += (i = serviceStart(ptr));

		if(i == 0)
		{
			LOGIF(LE, (skygw_log_write(
				LOGFILE_ERROR,
				"Error : Failed to start service '%s'.",
				ptr->name)));
		}

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
	service->state = SERVICE_STATE_STOPPED;

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
SERVER_REF *srv;
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
        
        while(service->dbref){
            srv = service->dbref;
            service->dbref = service->dbref->next;
            free(srv);
        }
        
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
        SERVER_REF *sref;
        if((sref = calloc(1,sizeof(SERVER_REF))) != NULL)
        {
            sref->next = service->dbref;
            sref->server = server;
            service->dbref = sref;
        }
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
SERVER_REF	*ptr;

	spinlock_acquire(&service->spin);
	ptr = service->dbref;
	while (ptr && ptr->server != server)
		ptr = ptr->next;
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
 * Enable/Disable loading the user data from only one server or all of them
 *
 * @param service	The service we are setting the data for
 * @param action	1 for root enable, 0 for disable access
 * @return		0 on failure
 */

int
serviceAuthAllServers(SERVICE *service, int action)
{
	if (action != 0 && action != 1)
		return 0;

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
		return 0;

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

    if(val < 0)
	return 0;
    service->conn_timeout = val;

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
			"Error : Out of memory adding filters to service.\n")));
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
				"Error : Out of memory adding filters to service.\n")));
			return;
		}
		if ((flist[n-1] = filter_find(trim(ptr))) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
				"Warning : Unable to find filter '%s' for service '%s'\n",
					trim(ptr), service->name
					)));
			n--;
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
SERVER_REF		*ptr = service->dbref;
struct tm	result;
char		time_buf[30];
int		i;

	printf("Service %p\n", (void *)service);
	printf("\tService:				%s\n", service->name);
	printf("\tRouter:				%s (%p)\n", service->routerModule, (void *)service->router);
	printf("\tStarted:		%s",
			asctime_r(localtime_r(&service->stats.started, &result), time_buf));
	printf("\tBackend databases\n");
	while (ptr)
	{
		printf("\t\t%s:%d  Protocol: %s\n", ptr->server->name, ptr->server->port, ptr->server->protocol);
		ptr = ptr->next;
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
	printf("\tUsers data:        	%p\n", (void *)service->users);
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
SERVER_REF		*server = service->dbref;
struct tm	result;
char		timebuf[30];
int		i;

	dcb_printf(dcb, "Service %p\n", service);
	dcb_printf(dcb, "\tService:				%s\n",
						service->name);
	dcb_printf(dcb, "\tRouter: 				%s (%p)\n",
			service->routerModule, service->router);
	switch (service->state)
	{
	case SERVICE_STATE_STARTED:
		dcb_printf(dcb, "\tState: 					Started\n");
		break;
	case SERVICE_STATE_STOPPED:
		dcb_printf(dcb, "\tState: 					Stopped\n");
		break;
	case SERVICE_STATE_FAILED:
		dcb_printf(dcb, "\tState: 					Failed\n");
		break;
	case SERVICE_STATE_ALLOC:
		dcb_printf(dcb, "\tState: 					Allocated\n");
		break;
	}
	if (service->router && service->router_instance)
		service->router->diagnostics(service->router_instance, dcb);
	dcb_printf(dcb, "\tStarted:				%s",
			asctime_r(localtime_r(&service->stats.started, &result), timebuf));
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
		dcb_printf(dcb, "\t\t%s:%d  Protocol: %s\n", server->server->name, server->server->port,
								server->server->protocol);
		server = server->next;
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
		ss_dassert(ptr->stats.n_current >= 0);
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
				(!lptr->listener || 
				!lptr->listener->session ||
				lptr->listener->session->state == SESSION_STATE_LISTENER_STOPPED) ? 
				"Stopped" : "Running"
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
			"%s: [service_refresh_users] failed to get get lock for loading new users' table: another thread is loading users",
			service->name)));

		return 1;
	}

	
	/* check if refresh rate limit has exceeded */
	if ( (time(NULL) < (service->rate_limit.last + USERS_REFRESH_TIME)) || (service->rate_limit.nloads > USERS_REFRESH_MAX_PER_TIME)) { 
		spinlock_release(&service->users_table_spin);
		LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"%s: Refresh rate limit exceeded for load of users' table.",
			service->name)));

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
        char*    p;
        int      valint;
	bool     valbool;
	target_t valtarget;
	bool     succp = true;
                
	if (PARAM_IS_TYPE(type,PERCENT_TYPE) ||PARAM_IS_TYPE(type,COUNT_TYPE))
	{
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
	}
	else if (type == BOOL_TYPE)
	{
		unsigned int rc;

		rc = find_type(&bool_type, valstr, strlen(valstr)+1);
		
		if (rc > 0)
		{
			succp = true;
			if (rc%2 == 1)
			{
				valbool = false;
			}
			else if (rc%2 == 0)
			{
				valbool = true;
			}
			/** add param to config */
			config_set_qualified_param(param, 
						   (void *)&valbool, 
						   BOOL_TYPE); 
		}
		else
		{
			succp = false;
		}
	}
	else if (type == SQLVAR_TARGET_TYPE)
	{
		unsigned int rc;
		
		rc = find_type(&sqlvar_target_type, valstr, strlen(valstr)+1);
		
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
			config_set_qualified_param(param, 
						   (void *)&valtarget, 
						   SQLVAR_TARGET_TYPE);
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
 *	 SYNOPSIS
 *	 find_type()
 *	 lib                  typelib_t
 *	 find                 String to find
 *	 length               Length of string to find
 *	 part_match           Allow part matching of value
 *	 
 *	 RETURN
 *	 0 error
 *	 > 0 position in TYPELIB->type_names +1
 */
static int find_type(
	typelib_t*  tl,
	const char* needle,
	int         maxlen)
{
	int i;
	
	if (tl == NULL || needle == NULL || maxlen <= 0)
	{
		return -1;
	}
	
	for (i=0; i<tl->tl_nelems; i++)
	{
		if (strncasecmp(tl->tl_p_elems[i], needle, maxlen) == 0)
		{
			return i+1;
		}
	}
	return 0;
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
		return 0;

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
SERVICE	*ptr;
int	rval = 0;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	while (ptr)
	{
		rval += ptr->stats.n_current;
		ptr = ptr->next;
	}
	spinlock_release(&service_spin);
	return rval;
}

/**
 * Provide a row to the result set that defines the set of service
 * listeners
 *
 * @param set	The result set
 * @param data	The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
serviceListenerRowCallback(RESULTSET *set, void *data)
{
int		*rowno = (int *)data;
int		i = 0;;
char		buf[20];
RESULT_ROW	*row;
SERVICE		*ptr;
SERV_PROTOCOL	*lptr = NULL;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	if (ptr)
		lptr = ptr->ports;
	while (i < *rowno && ptr)
	{
		lptr = ptr->ports;
		while (i < *rowno && lptr)
		{
			if ((lptr = lptr->next) != NULL)
				i++;
		}
		if (i < *rowno)
		{
			ptr = ptr->next;
			if (ptr && (lptr = ptr->ports) != NULL)
				i++;
		}
	}
	if (lptr == NULL)
	{
		spinlock_release(&service_spin);
		free(data);
		return NULL;
	}
	(*rowno)++;
	row = resultset_make_row(set);
	resultset_row_set(row, 0, ptr->name);
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
RESULTSET	*set;
int		*data;

	if ((data = (int *)malloc(sizeof(int))) == NULL)
		return NULL;
	*data = 0;
	if ((set = resultset_create(serviceListenerRowCallback, data)) == NULL)
	{
		free(data);
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
 * @param set	The result set
 * @param data	The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
serviceRowCallback(RESULTSET *set, void *data)
{
int		*rowno = (int *)data;
int		i = 0;;
char		buf[20];
RESULT_ROW	*row;
SERVICE		*ptr;

	spinlock_acquire(&service_spin);
	ptr = allServices;
	while (i < *rowno && ptr)
	{
		i++;
		ptr = ptr->next;
	}
	if (ptr == NULL)
	{
		spinlock_release(&service_spin);
		free(data);
		return NULL;
	}
	(*rowno)++;
	row = resultset_make_row(set);
	resultset_row_set(row, 0, ptr->name);
	resultset_row_set(row, 1, ptr->routerModule);
	sprintf(buf, "%d", ptr->stats.n_current);
	resultset_row_set(row, 2, buf);
	sprintf(buf, "%d", ptr->stats.n_sessions);
	resultset_row_set(row, 3, buf);
	spinlock_release(&service_spin);
	return row;
}

/**
 * Return a resultset that has the current set of services in it
 *
 * @return A Result set
 */
RESULTSET *
serviceGetList()
{
RESULTSET	*set;
int		*data;

	if ((data = (int *)malloc(sizeof(int))) == NULL)
		return NULL;
	*data = 0;
	if ((set = resultset_create(serviceRowCallback, data)) == NULL)
	{
		free(data);
		return NULL;
	}
	resultset_add_column(set, "Service Name", 25, COL_TYPE_VARCHAR);
	resultset_add_column(set, "Router Module", 20, COL_TYPE_VARCHAR);
	resultset_add_column(set, "No. Sessions", 10, COL_TYPE_VARCHAR);
	resultset_add_column(set, "Total Sessions", 10, COL_TYPE_VARCHAR);

	return set;
}
