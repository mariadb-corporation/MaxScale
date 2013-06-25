#ifndef _CONFIG_H
#define _CONFIG_H
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
 * @file config.h The configuration handling elements
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 21/06/13	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */

/**
 * The config parameter
 */
typedef struct config_parameter {
	char			*name;		/**< The name of the parameter */
	char			*value;		/**< The value of the parameter */
	struct config_parameter	*next;		/**< Next pointer in the linked list */
} CONFIG_PARAMETER;

/**
 * The config context structure, used to build the configuration
 * data during the parse process
 */
typedef struct	config_context {
	char			*object;	/**< The name of the object being configured */
	CONFIG_PARAMETER	*parameters;	/**< The list of parameter values */
	void			*element;	/**< The element created from the data */
	struct config_context	*next;		/**< Next pointer in the linked list */
} CONFIG_CONTEXT;

/**
 * The gateway global configuration data
 */
typedef struct {
	int			n_threads;	/**< Number of polling threads */
} GATEWAY_CONF;

extern int	config_load(char *);
extern int	config_threadcount();
#endif
