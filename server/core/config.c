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
 * @file config.c  - Read the gateway.cnf configuration file
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 21/06/13	Mark Riddoch		Initial implementation
 * 08/07/13	Mark Riddoch		Addition on monitor module support
 * 23/07/13	Mark Riddoch		Addition on default monitor password
 * 06/02/14	Massimiliano Pinto	Added support for enable/disable root user in services
 * 14/02/14	Massimiliano Pinto	Added enable_root_user in the service_params list
 * 11/03/14	Massimiliano Pinto	Added Unix socket support
 * 11/05/14	Massimiliano Pinto	Added version_string support to service
 * 19/05/14	Mark Riddoch		Added unique names from section headers
 *
 * @endverbatim
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ini.h>
#include <config.h>
#include <service.h>
#include <server.h>
#include <users.h>
#include <monitor.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <mysql.h>

extern int lm_enabled_logfiles_bitmask;

static	int	process_config_context(CONFIG_CONTEXT	*);
static	int	process_config_update(CONFIG_CONTEXT *);
static	void	free_config_context(CONFIG_CONTEXT	*);
static	char 	*config_get_value(CONFIG_PARAMETER *, const char *);
static	int	handle_global_item(const char *, const char *);
static	void	global_defaults();
static	void	check_config_objects(CONFIG_CONTEXT *context);

static	char		*config_file = NULL;
static	GATEWAY_CONF	gateway;
char 			*version_string = NULL;

/**
 * Config item handler for the ini file reader
 *
 * @param userdata	The config context element
 * @param section	The config file section
 * @param name		The Parameter name
 * @param value		The Parameter value
 * @return zero on error
 */
static int
handler(void *userdata, const char *section, const char *name, const char *value)
{
CONFIG_CONTEXT		*cntxt = (CONFIG_CONTEXT *)userdata;
CONFIG_CONTEXT		*ptr = cntxt;
CONFIG_PARAMETER	*param;

	if (strcmp(section, "gateway") == 0 || strcasecmp(section, "MaxScale") == 0)
	{
		return handle_global_item(name, value);
	}
	/*
	 * If we already have some parameters for the object
	 * add the parameters to that object. If not create
	 * a new object.
	 */
	while (ptr && strcmp(ptr->object, section) != 0)
		ptr = ptr->next;
	if (!ptr)
	{
		if ((ptr = (CONFIG_CONTEXT *)malloc(sizeof(CONFIG_CONTEXT))) == NULL)
			return 0;
		ptr->object = strdup(section);
		ptr->parameters = NULL;
		ptr->next = cntxt->next;
		ptr->element = NULL;
		cntxt->next = ptr;
	}
	if ((param = (CONFIG_PARAMETER *)malloc(sizeof(CONFIG_PARAMETER))) == NULL)
		return 0;
	param->name = strdup(name);
	param->value = strdup(value);
	param->next = ptr->parameters;
	ptr->parameters = param;

	return 1;
}

/**
 * Load the configuration file for the MaxScale
 *
 * @param file	The filename of the configuration file
 * @return A zero return indicates a fatal error reading the configuration
 */
int
config_load(char *file)
{
CONFIG_CONTEXT	config;
int		rval;

	MYSQL *conn;
	conn = mysql_init(NULL);
	if (conn) {
		if (mysql_real_connect(conn, NULL, NULL, NULL, NULL, 0, NULL, 0)) {
			char *ptr;
			version_string = (char *)mysql_get_server_info(conn);
			ptr = strstr(version_string, "-embedded");
			if (ptr) {
				*ptr = '\0';
			}
				
		}
		mysql_close(conn);
	}

	global_defaults();

	config.object = "";
	config.next = NULL;

	if (ini_parse(file, handler, &config) < 0)
		return 0;

	config_file = file;

	check_config_objects(config.next);
	rval = process_config_context(config.next);
	free_config_context(config.next);

	return rval;
}

/**
 * Reload the configuration file for the MaxScale
 *
 * @return A zero return indicates a fatal error reading the configuration
 */
int
config_reload()
{
CONFIG_CONTEXT	config;
int		rval;

	if (!config_file)
		return 0;


	if (gateway.version_string)
		free(gateway.version_string);

	global_defaults();

	config.object = "";
	config.next = NULL;

	if (ini_parse(config_file, handler, &config) < 0)
		return 0;

	rval = process_config_update(config.next);
	free_config_context(config.next);

	return rval;
}

/**
 * Process a configuration context and turn it into the set of object
 * we need.
 *
 * @param context	The configuration data
 * @return A zero result indicates a fatal error
 */
static	int
process_config_context(CONFIG_CONTEXT *context)
{
CONFIG_CONTEXT		*obj;
int			error_count = 0;

	/**
	 * Process the data and create the services and servers defined
	 * in the data.
	 */
	obj = context;
	while (obj)
	{
		char *type = config_get_value(obj->parameters, "type");
		if (type == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Configuration object '%s' has no type.",
                                obj->object)));
			error_count++;
		}
		else if (!strcmp(type, "service"))
		{
                        char *router = config_get_value(obj->parameters,
                                                        "router");
                        if (router)
                        {
				obj->element = service_alloc(obj->object, router);
				char *user =
                                        config_get_value(obj->parameters, "user");
				char *auth =
                                        config_get_value(obj->parameters, "passwd");
				char *enable_root_user =
					config_get_value(obj->parameters, "enable_root_user");
			
				char *version_string = config_get_value(obj->parameters, "version_string");

                                if (obj->element == NULL) /*< if module load failed */
                                {
                                        LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : Reading configuration "
                                                "for router service '%s' failed. "
                                                "Router %s is not loaded.",
                                                obj->object,
                                                obj->object)));
                                        obj = obj->next;
                                        continue; /*< process next obj */
                                }

				if (version_string) {
					((SERVICE *)(obj->element))->version_string = strdup(version_string);
				} else {
					if (gateway.version_string)
						((SERVICE *)(obj->element))->version_string = strdup(gateway.version_string);
				}
                                
				if (enable_root_user)
					serviceEnableRootUser(obj->element, atoi(enable_root_user));

				if (!auth)
					auth = config_get_value(obj->parameters, "auth");

				if (obj->element && user && auth)
				{
					serviceSetUser(obj->element, user, auth);
				}
				else if (user && auth == NULL)
				{
					LOGIF(LE, (skygw_log_write_flush(
		                                LOGFILE_ERROR,
               			                "Error : Service '%s' has a "
						"user defined but no "
						"corresponding password.",
		                                obj->object)));
				}
			}
			else
			{
				obj->element = NULL;
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : No router defined for service "
                                        "'%s'\n",
                                        obj->object)));
				error_count++;
			}
		}
		else if (!strcmp(type, "server"))
		{
                        char *address;
			char *port;
			char *protocol;
			char *monuser;
			char *monpw;

                        address = config_get_value(obj->parameters, "address");
			port = config_get_value(obj->parameters, "port");
			protocol = config_get_value(obj->parameters, "protocol");
			monuser = config_get_value(obj->parameters,
                                                   "monitoruser");
			monpw = config_get_value(obj->parameters, "monitorpw");

			if (address && port && protocol)
			{
				obj->element = server_alloc(address,
                                                            protocol,
                                                            atoi(port));
				server_set_unique_name(obj->element, obj->object);
			}
			else
			{
				obj->element = NULL;
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Server '%s' is missing a "
                                        "required configuration parameter. A "
                                        "server must "
                                        "have address, port and protocol "
                                        "defined.",
                                        obj->object)));
				error_count++;
			}
			if (obj->element && monuser && monpw)
				serverAddMonUser(obj->element, monuser, monpw);
			else if (monuser && monpw == NULL)
			{
				LOGIF(LE, (skygw_log_write_flush(
	                                LOGFILE_ERROR,
					"Error : Server '%s' has a monitoruser"
					"defined but no corresponding password.",
                                        obj->object)));
			}
		}
		obj = obj->next;
	}

	/*
	 * Now we have the services we can add the servers to the services
	 * add the protocols to the services
	 */
	obj = context;
	while (obj)
	{
		char *type = config_get_value(obj->parameters, "type");
		if (type == NULL)
			;
		else if (!strcmp(type, "service"))
		{
                        char *servers;
			char *roptions;
                        
			servers = config_get_value(obj->parameters, "servers");
			roptions = config_get_value(obj->parameters,
                                                    "router_options");
			if (servers && obj->element)
			{
				char *s = strtok(servers, ",");
				while (s)
				{
					CONFIG_CONTEXT *obj1 = context;
					while (obj1)
					{
						if (strcmp(s, obj1->object) == 0 &&
                                                    obj->element && obj1->element)
                                                {
							serviceAddBackend(
                                                                obj->element,
                                                                obj1->element);
                                                }
						obj1 = obj1->next;
					}
					s = strtok(NULL, ",");
				}
			}
			else if (servers == NULL)
			{
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : The service '%s' is missing a "
                                        "definition of the servers that provide "
                                        "the service.",
                                        obj->object)));
			}
			if (roptions && obj->element)
			{
				char *s = strtok(roptions, ",");
				while (s)
				{
					serviceAddRouterOption(obj->element, s);
					s = strtok(NULL, ",");
				}
			}
		}
		else if (!strcmp(type, "listener"))
		{
                        char *service;
			char *address;
			char *port;
			char *protocol;
			char *socket;

                        service = config_get_value(obj->parameters, "service");
			port = config_get_value(obj->parameters, "port");
			address = config_get_value(obj->parameters, "address");
			protocol = config_get_value(obj->parameters, "protocol");
			socket = config_get_value(obj->parameters, "socket");
                
			if (service && socket && protocol) {        
				CONFIG_CONTEXT *ptr = context;
				while (ptr && strcmp(ptr->object, service) != 0)
					ptr = ptr->next;
				if (ptr && ptr->element)
				{
					serviceAddProtocol(ptr->element,
                                                           protocol,
							   socket,
                                                           0);
				} else {
					LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
                                        	"Error : Listener '%s', "
                                        	"service '%s' not found. "
						"Listener will not execute for socket %s.",
	                                        obj->object, service, socket)));
					error_count++;
				}
			}

			if (service && port && protocol) {
				CONFIG_CONTEXT *ptr = context;
				while (ptr && strcmp(ptr->object, service) != 0)
					ptr = ptr->next;
				if (ptr && ptr->element)
				{
					serviceAddProtocol(ptr->element,
                                                           protocol,
							   address,
                                                           atoi(port));
				}
				else
				{
					LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
                                        	"Error : Listener '%s', "
                                        	"service '%s' not found. "
						"Listener will not execute.",
	                                        obj->object, service)));
					error_count++;
				}
			}
			else
			{
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Listener '%s' is misisng a "
                                        "required "
                                        "parameter. A Listener must have a "
                                        "service, port and protocol defined.",
                                        obj->object)));
				error_count++;
			}
		}
		else if (!strcmp(type, "monitor"))
		{
                        char *module;
			char *servers;
			char *user;
			char *passwd;

                        module = config_get_value(obj->parameters, "module");
			servers = config_get_value(obj->parameters, "servers");
			user = config_get_value(obj->parameters, "user");
			passwd = config_get_value(obj->parameters, "passwd");

                        if (module)
			{
				obj->element = monitor_alloc(obj->object, module);
				if (servers && obj->element)
				{
					char *s = strtok(servers, ",");
					while (s)
					{
						CONFIG_CONTEXT *obj1 = context;
						while (obj1)
						{
							if (strcmp(s, obj1->object) == 0 &&
                                                            obj->element && obj1->element)
                                                        {
								monitorAddServer(
                                                                        obj->element,
                                                                        obj1->element);
                                                        }
							obj1 = obj1->next;
						}
						s = strtok(NULL, ",");
					}
				}
				if (obj->element && user && passwd)
				{
					monitorAddUser(obj->element,
                                                       user,
                                                       passwd);
				}
				else if (obj->element && user)
				{
					LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR, "Error: "
						"Monitor '%s' defines a "
						"username with no password.",
						obj->object)));
					error_count++;
				}
			}
			else
			{
				obj->element = NULL;
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Monitor '%s' is missing a "
                                        "require module parameter.",
                                        obj->object)));
				error_count++;
			}
		}
		else if (strcmp(type, "server") != 0)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Configuration object '%s' has an "
                                "invalid type specified.",
                                obj->object)));
			error_count++;
		}

		obj = obj->next;
	}

	if (error_count)
	{
		LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : %d errors where encountered processing the "
                        "configuration file '%s'.",
                        error_count,
                        config_file)));
		return 0;
	}
	return 1;
}

/**
 * Get the value of a config parameter
 *
 * @param params	The linked list of config parameters
 * @param name		The parameter to return
 * @return the parameter value or NULL if not found
 */
static char *
config_get_value(CONFIG_PARAMETER *params, const char *name)
{
	while (params)
	{
		if (!strcmp(params->name, name))
			return params->value;
		params = params->next;
	}
	return NULL;
}

/**
 * Free a config tree
 *
 * @param context	The configuration data
 */
static	void
free_config_context(CONFIG_CONTEXT *context)
{
CONFIG_CONTEXT		*obj;
CONFIG_PARAMETER	*p1, *p2;

	while (context)
	{
		free(context->object);
		p1 = context->parameters;
		while (p1)
		{
			free(p1->name);
			free(p1->value);
			p2 = p1->next;
			free(p1);
			p1 = p2;
		}
		obj = context->next;
		free(context);
		context = obj;
	}
}

/**
 * Return the number of configured threads
 *
 * @return The number of threads configured in the config file
 */
int
config_threadcount()
{
	return gateway.n_threads;
}

/**
 * Configuration handler for items in the global [MaxScale] section
 *
 * @param name	The item name
 * @param value	The item value
 * @return 0 on error
 */
static	int
handle_global_item(const char *name, const char *value)
{
	if (strcmp(name, "threads") == 0) {
		gateway.n_threads = atoi(value);
        } else {
                return 0;
        }
	return 1;
}

/**
 * Set the defaults for the global configuration options
 */
static void
global_defaults()
{
	gateway.n_threads = 1;
	if (version_string != NULL)
		gateway.version_string = strdup(version_string);
	else
		gateway.version_string = NULL;
}

/**
 * Process a configuration context update and turn it into the set of object
 * we need.
 *
 * @param context	The configuration data
 */
static	int
process_config_update(CONFIG_CONTEXT *context)
{
CONFIG_CONTEXT		*obj;
SERVICE			*service;
SERVER			*server;

	/**
	 * Process the data and create the services and servers defined
	 * in the data.
	 */
	obj = context;
	while (obj)
	{
		char *type = config_get_value(obj->parameters, "type");
		if (type == NULL)
                {
                    LOGIF(LE,
                          (skygw_log_write_flush(
                                  LOGFILE_ERROR,
                                  "Error : Configuration object %s has no type.",
                                  obj->object)));
                }
		else if (!strcmp(type, "service"))
		{
			char *router = config_get_value(obj->parameters,
                                                        "router");
			if (router)
			{
				if ((service = service_find(obj->object)) != NULL)
				{
                                        char *user;
					char *auth;
					char *enable_root_user;
					char *version_string;

					enable_root_user = config_get_value(obj->parameters, "enable_root_user");

                                        user = config_get_value(obj->parameters,
                                                                "user");
					auth = config_get_value(obj->parameters,
                                                                "passwd");

					version_string = config_get_value(obj->parameters, "version_string");

					if (version_string) {
						if (service->version_string)
							free(service->version_string);
						service->version_string = strdup(version_string);
					}

					if (user && auth) {
						service_update(service, router,
                                                               user,
                                                               auth);
						if (enable_root_user)
							serviceEnableRootUser(service, atoi(enable_root_user));
					}

					obj->element = service;
				}
				else
				{
                                        char *user;
					char *auth;
					char *enable_root_user;

					enable_root_user = config_get_value(obj->parameters, "enable_root_user");

                                        user = config_get_value(obj->parameters,
                                                                "user");
					auth = config_get_value(obj->parameters,
                                                                "passwd");
					obj->element = service_alloc(obj->object,
                                                                     router);

					if (obj->element && user && auth)
                                        {
						serviceSetUser(obj->element,
                                                               user,
                                                               auth);
						if (enable_root_user)
							serviceEnableRootUser(service, atoi(enable_root_user));
                                        }
				}
			}
			else
			{
				obj->element = NULL;
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : No router defined for service "
                                        "'%s'.",
                                        obj->object)));
			}
		}
		else if (!strcmp(type, "server"))
		{
                        char *address;
			char *port;
			char *protocol;
			char *monuser;
			char *monpw;
                        
			address = config_get_value(obj->parameters, "address");
			port = config_get_value(obj->parameters, "port");
			protocol = config_get_value(obj->parameters, "protocol");
			monuser = config_get_value(obj->parameters,
                                                   "monitoruser");
			monpw = config_get_value(obj->parameters, "monitorpw");

                        if (address && port && protocol)
			{
				if ((server =
                                     server_find(address, atoi(port))) != NULL)
				{
					server_update(server,
                                                      protocol,
                                                      monuser,
                                                      monpw);
					obj->element = server;
				}
				else
				{
					obj->element = server_alloc(address,
                                                                    protocol,
                                                                    atoi(port));
					if (obj->element && monuser && monpw)
                                        {
						serverAddMonUser(obj->element,
                                                                 monuser,
                                                                 monpw);
                                        }
				}
			}
			else
                        {
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
                                        "Error : Server '%s' is missing a "
                                        "required "
                                        "configuration parameter. A server must "
                                        "have address, port and protocol "
                                        "defined.",
                                        obj->object)));
                        }
		}
		obj = obj->next;
	}

	/*
	 * Now we have the services we can add the servers to the services
	 * add the protocols to the services
	 */
	obj = context;
	while (obj)
	{
		char *type = config_get_value(obj->parameters, "type");
		if (type == NULL)
			;
		else if (!strcmp(type, "service"))
		{
                        char *servers;
			char *roptions;
                        
			servers = config_get_value(obj->parameters, "servers");
			roptions = config_get_value(obj->parameters,
                                                    "router_options");
			if (servers && obj->element)
			{
				char *s = strtok(servers, ",");
				while (s)
				{
					CONFIG_CONTEXT *obj1 = context;
					while (obj1)
					{
						if (strcmp(s, obj1->object) == 0 &&
                                                    obj->element && obj1->element)
                                                {
							if (!serviceHasBackend(obj->element, obj1->element))
                                                        {
								serviceAddBackend(
                                                                        obj->element,
                                                                        obj1->element);
                                                        }
                                                }
						obj1 = obj1->next;
					}
					s = strtok(NULL, ",");
				}
			}
			if (roptions && obj->element)
			{
				char *s = strtok(roptions, ",");
				serviceClearRouterOptions(obj->element);
				while (s)
				{
					serviceAddRouterOption(obj->element, s);
					s = strtok(NULL, ",");
				}
			}
		}
		else if (!strcmp(type, "listener"))
		{
                        char *service;
			char *port;
			char *protocol;
			char *address;
			char *socket;

                        service = config_get_value(obj->parameters, "service");
			address = config_get_value(obj->parameters, "address");
			port = config_get_value(obj->parameters, "port");
			protocol = config_get_value(obj->parameters, "protocol");
			socket = config_get_value(obj->parameters, "socket");

                        if (service && socket && protocol)
			{
				CONFIG_CONTEXT *ptr = context;
				while (ptr && strcmp(ptr->object, service) != 0)
					ptr = ptr->next;
                                
				if (ptr &&
                                    ptr->element &&
                                    serviceHasProtocol(ptr->element,
                                                       protocol,
                                                       0) == 0)
				{
					serviceAddProtocol(ptr->element,
                                                           protocol,
							   socket,
                                                           0);
					serviceStartProtocol(ptr->element,
                                                             protocol,
                                                             0);
				}
			}

                        if (service && port && protocol)
			{
				CONFIG_CONTEXT *ptr = context;
				while (ptr && strcmp(ptr->object, service) != 0)
					ptr = ptr->next;
                                
				if (ptr &&
                                    ptr->element &&
                                    serviceHasProtocol(ptr->element,
                                                       protocol,
                                                       atoi(port)) == 0)
				{
					serviceAddProtocol(ptr->element,
                                                           protocol,
							   address,
                                                           atoi(port));
					serviceStartProtocol(ptr->element,
                                                             protocol,
                                                             atoi(port));
				}
			}
		}
		else if (strcmp(type, "server") != 0 &&
                         strcmp(type, "monitor") != 0)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Configuration object %s has an invalid "
                                "type specified.",
                                obj->object)));
		}
		obj = obj->next;
	}
	return 1;
}

static char *service_params[] =
	{
                "type",
                "router",
                "router_options",
                "servers",
                "user",
                "passwd",
		"enable_root_user",
		"version_string",
                NULL
        };

static char *server_params[] =
	{
                "type",
                "address",
                "port",
                "protocol",
                "monitorpw",
                "monitoruser",
                NULL
        };

static char *listener_params[] =
	{
                "type",
                "service",
                "protocol",
                "port",
                "address",
                "socket",
                NULL
        };

static char *monitor_params[] =
	{
                "type",
                "module",
                "servers",
                "user",
                "passwd",
                NULL
        };
/**
 * Check the configuration objects have valid parameters
 */
static void
check_config_objects(CONFIG_CONTEXT *context)
{
CONFIG_CONTEXT		*obj;
CONFIG_PARAMETER 	*params;
char			*type, **param_set;
int			i;

	/**
	 * Process the data and create the services and servers defined
	 * in the data.
	 */
	obj = context;
	while (obj)
	{
		param_set = NULL;
		if (obj->parameters &&
			(type = config_get_value(obj->parameters, "type")))
		{
			if (!strcmp(type, "service"))
				param_set = service_params;
			else if (!strcmp(type, "server"))
				param_set = server_params;
			else if (!strcmp(type, "listener"))
				param_set = listener_params;
			else if (!strcmp(type, "monitor"))
				param_set = monitor_params;
		}
		if (param_set != NULL)
		{
			params = obj->parameters;
			while (params)
			{
				int found = 0;
				for (i = 0; param_set[i]; i++)
					if (!strcmp(params->name, param_set[i]))
						found = 1;
				if (found == 0)
					LOGIF(LE, (skygw_log_write_flush(
                                                LOGFILE_ERROR,
                                                "Error : Unexpected parameter "
                                                "'%s' for object '%s' of type "
                                                "'%s'.",
						params->name,
                                                obj->object,
                                                type)));
				params = params->next;
			}
		}
		obj = obj->next;
	}
}
