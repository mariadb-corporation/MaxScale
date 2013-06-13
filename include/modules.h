#ifndef _MODULES_H
#define _MODULES_H
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

/*
 * The module interface used within the gateway
 *
 * Revision History
 *
 * Date		Who		Description
 * 13/06/13	Mark Riddoch	Initial implementation
 *
 */

typedef struct modules {
	char	*module;	/* The name of the module */
	char	*type;		/* The module type */
	char	*version;	/* Module version */
	void	*handle;	/* The handle returned by dlopen */
	void	*modobj;	/* The module "object" this is the set of entry points */
	struct	modules
		*next;		/* Next module in the linked list */
} MODULES;

extern	void 	*load_module(const char *module, const char *type, const char *entry);
extern	void	unload_module(const char *module);
extern	void	printModules();
#endif
