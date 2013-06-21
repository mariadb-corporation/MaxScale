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
 * Date		Who		Description
 * 21/06/13	Mark Riddoch	Initial implementation
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


static	int	process_config_context(CONFIG_CONTEXT	*);
static char 	*config_get_value(CONFIG_PARAMETER *, const char *);


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
 *
 * @param file	The filename of the configuration file
 */
int
load_config(char *file)
{
CONFIG_CONTEXT	config;

	config.object = "";
	config.next = NULL;

	if (ini_parse(file, handler, &config) < 0)
		return 0;

	return process_config_context(config.next);
}

/**
 * Process a configuration context and turn it into the set of object
 * we need.
 *
 * @param context	The configuration data
 */
static	int
process_config_context(CONFIG_CONTEXT *context)
{
CONFIG_CONTEXT		*obj;

	/**
	 * Process the data and create the services and servers defined
	 * in the data.
	 */
	obj = context;
	while (obj)
	{
		char *type = config_get_value(obj->parameters, "type");
		if (type == NULL)
			fprintf(stderr, "Object %s has no type\n", obj->object);
		else if (!strcmp(type, "service"))
		{
			char *router = config_get_value(obj->parameters, "router");
			if (router)
				obj->element = service_alloc(obj->object, router);
			else
				fprintf(stderr, "No router define for service '%s'\n",
							obj->object);
		}
		else if (!strcmp(type, "server"))
		{
			char *address = config_get_value(obj->parameters, "address");
			char *port = config_get_value(obj->parameters, "port");
			char *protocol = config_get_value(obj->parameters, "protocol");
			if (address && port && protocol)
				obj->element = server_alloc(address, protocol, atoi(port));
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
			char *servers = config_get_value(obj->parameters, "servers");
			if (servers)
			{
				char *s = strtok(servers, ",");
				while (s)
				{
					CONFIG_CONTEXT *obj1 = context;
					while (obj1)
					{
						if (strcmp(s, obj1->object) == 0 && obj->element && obj1->element)
							serviceAddBackend(obj->element, obj1->element);
						obj1 = obj1->next;
					}
					s = strtok(NULL, ",");
				}
			}
		}
		else if (!strcmp(type, "listener"))
		{
			char *service = config_get_value(obj->parameters, "service");
			char *port = config_get_value(obj->parameters, "port");
			char *protocol = config_get_value(obj->parameters, "protocol");
			if (service && port && protocol)
			{
				CONFIG_CONTEXT *ptr = context;
				while (ptr && strcmp(ptr->object, service) != 0)
					ptr = ptr->next;
				if (ptr && ptr->element)
					serviceAddProtocol(ptr->element, protocol, atoi(port));
			}
		}

		obj = obj->next;
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
