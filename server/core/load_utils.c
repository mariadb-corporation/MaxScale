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
 * @file load_utils.c		Utility functions to aid the loading of dynamic
 *                             modules into the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 13/06/13	Mark Riddoch	Initial implementation
 * 14/06/13	Mark Riddoch	Updated to add call to ModuleInit if one is
 *                              defined in the loaded module.
 * 				Also updated to call fixed GetModuleObject
 * 02/06/14	Mark Riddoch	Addition of module info
 *
 * @endverbatim
 */
#include	<sys/param.h>
#include	<stdio.h>
#include	<stdlib.h>
#include	<unistd.h>
#include	<string.h>
#include	<dlfcn.h>
#include	<modules.h>
#include	<modinfo.h>
#include	<skygw_utils.h>
#include	<log_manager.h>

extern int lm_enabled_logfiles_bitmask;

static	MODULES	*registered = NULL;

static MODULES *find_module(const char *module);
static void register_module(const char *module,
                            const char  *type,
                            void        *dlhandle,
                            char        *version,
                            void        *modobj,
			    MODULE_INFO *info);
static void unregister_module(const char *module);

char* get_maxscale_home(void)
{
        char* home = getenv("MAXSCALE_HOME");
        if (home == NULL)
        {
                home = "/usr/local/skysql/MaxScale";
        }
        return home;
}
                

/**
 * Load the dynamic library related to a gateway module. The routine
 * will look for library files in the current directory, 
 * $MAXSCALE_HOME/modules and /usr/local/skysql/MaxScale/modules.
 *
 * @param module	Name of the module to load
 * @param type		Type of module, used purely for registration
 * @return		The module specific entry point structure or NULL
 */
void *
load_module(const char *module, const char *type)
{
char		*home, *version;
char		fname[MAXPATHLEN];
void		*dlhandle, *sym;
char		*(*ver)();
void		*(*ep)(), *modobj;
MODULES		*mod;
MODULE_INFO	*mod_info = NULL;

        if ((mod = find_module(module)) == NULL)
	{
                /*<
		 * The module is not already loaded
		 *
		 * Search of the shared object.
		 */
		sprintf(fname, "./lib%s.so", module);
		if (access(fname, F_OK) == -1)
		{
			home = get_maxscale_home ();
			sprintf(fname, "%s/modules/lib%s.so", home, module);

                        if (access(fname, F_OK) == -1)
			{
				LOGIF(LE, (skygw_log_write_flush(
                                        LOGFILE_ERROR,
					"Error : Unable to find library for "
                                        "module: %s.",
                                        module)));
				return NULL;
			}
		}

		if ((dlhandle = dlopen(fname, RTLD_NOW|RTLD_LOCAL)) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
				"Error : Unable to load library for module: "
                                "%s\n\n\t\t      %s."
                                "\n\n",
                                module,
                                dlerror())));
			return NULL;
		}

		if ((sym = dlsym(dlhandle, "version")) == NULL)
		{
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Version interface not supported by "
                                "module: %s\n\t\t\t      %s.",
                                module,
                                dlerror())));
			dlclose(dlhandle);
			return NULL;
		}
		ver = sym;
		version = ver();

		/*
		 * If the module has a ModuleInit function cal it now.
		 */
		if ((sym = dlsym(dlhandle, "ModuleInit")) != NULL)
		{
			void (*ModuleInit)() = sym;
			ModuleInit();
		}

		if ((sym = dlsym(dlhandle, "info")) != NULL)
		{
			int fatal = 0;
			mod_info = sym;
			if (strcmp(type, MODULE_PROTOCOL) == 0
				&& mod_info->modapi != MODULE_API_PROTOCOL)
			{
                        	LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Module '%s' does not implement "
					"the protocol API.\n",
					module)));
				fatal = 1;
			}
			if (strcmp(type, MODULE_ROUTER) == 0
				&& mod_info->modapi != MODULE_API_ROUTER)
			{
                        	LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Module '%s' does not implement "
					"the router API.\n",
					module)));
				fatal = 1;
			}
			if (strcmp(type, MODULE_MONITOR) == 0
				&& mod_info->modapi != MODULE_API_MONITOR)
			{
                        	LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Module '%s' does not implement "
					"the monitor API.\n",
					module)));
				fatal = 1;
			}
			if (strcmp(type, MODULE_FILTER) == 0
				&& mod_info->modapi != MODULE_API_FILTER)
			{
                        	LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Module '%s' does not implement "
					"the filter API.\n",
					module)));
				fatal = 1;
			}
			if (fatal)
			{
				dlclose(dlhandle);
				return NULL;
			}
		}

		if ((sym = dlsym(dlhandle, "GetModuleObject")) == NULL)
		{
			LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Expected entry point interface missing "
                                "from module: %s\n\t\t\t      %s.",
                                module,
                                dlerror())));
			dlclose(dlhandle);
			return NULL;
		}
		ep = sym;
		modobj = ep();

		LOGIF(LM, (skygw_log_write_flush(
                        LOGFILE_MESSAGE,
                        "Loaded module %s: %s from %s",
                        module,
                        version,
                        fname)));
		register_module(module, type, dlhandle, version, modobj, mod_info);
	}
	else
	{
		/*
		 * The module is already loaded, get the entry points again and
		 * return a reference to the already loaded module.
		 */
		modobj = mod->modobj;
	}

	return modobj;
}

/**
 * Unload a module.
 *
 * No errors are returned since it is not clear that much can be done
 * to fix issues relating to unloading modules.
 *
 * @param module	The name of the module
 */
void
unload_module(const char *module)
{
MODULES	*mod = find_module(module);
void	*handle;

	if (!mod)
		return;
	handle = mod->handle;
	unregister_module(module);
	dlclose(handle);
}

/**
 * Find a module that has been previously loaded and return the handle for that
 * library
 *
 * @param module	The name of the module
 * @return		The module handle or NULL if it was not found
 */
static MODULES *
find_module(const char *module)
{
MODULES	*mod = registered;

	while (mod)
		if (strcmp(mod->module, module) == 0)
			return mod;
		else
			mod = mod->next;
	return NULL;
}

/**
 * Register a newly loaded module. The registration allows for single copies
 * to be loaded and cached entry point information to be return.
 *
 * @param module	The name of the module loaded
 * @param type		The type of the module loaded
 * @param dlhandle	The handle returned by dlopen
 * @param version	The version string returned by the module
 * @param modobj	The module object
 * @param mod_info	The module information
 */
static void
register_module(const char *module, const char *type, void *dlhandle, char *version, void *modobj, MODULE_INFO *mod_info)
{
MODULES	*mod;

	if ((mod = malloc(sizeof(MODULES))) == NULL)
		return;
	mod->module = strdup(module);
	mod->type = strdup(type);
	mod->handle = dlhandle;
	mod->version = strdup(version);
	mod->modobj = modobj;
	mod->next = registered;
	mod->info = mod_info;
	registered = mod;
}

/**
 * Unregister a module
 *
 * @param module	The name of the module to remove
 */
static void
unregister_module(const char *module)
{
MODULES	*mod = find_module(module);
MODULES	*ptr;

	if (!mod)
		return;		// Module not found
	if (registered == mod)
		registered = mod->next;
	else
	{
		ptr = registered;
		while (ptr && ptr->next != mod)
			ptr = ptr->next;
	}

	/*<
	 * The module is now not in the linked list and all
	 * memory related to it can be freed
	 */
	free(mod->module);
	free(mod->type);
	free(mod->version);
	free(mod);
}

/**
 * Print Modules
 *
 * Diagnostic routine to display all the loaded modules
 */
void
printModules()
{
MODULES	*ptr = registered;

	printf("%-15s | %-11s | Version\n", "Module Name", "Module Type");
	printf("-----------------------------------------------------\n");
	while (ptr)
	{
		printf("%-15s | %-11s | %s\n", ptr->module, ptr->type, ptr->version);
		ptr = ptr->next;
	}
}

/**
 * Print Modules to a DCB
 *
 * Diagnostic routine to display all the loaded modules
 */
void
dprintAllModules(DCB *dcb)
{
MODULES	*ptr = registered;

	dcb_printf(dcb, "Modules.\n");
	dcb_printf(dcb, "----------------+-------------+---------+-------+-------------------------\n");
	dcb_printf(dcb, "%-15s | %-11s | Version | API   | Status\n", "Module Name", "Module Type");
	dcb_printf(dcb, "----------------+-------------+---------+-------+-------------------------\n");
	while (ptr)
	{
		dcb_printf(dcb, "%-15s | %-11s | %-7s ", ptr->module, ptr->type, ptr->version);
		if (ptr->info)
			dcb_printf(dcb, "| %d.%d.%d | %s",
					ptr->info->api_version.major,
					ptr->info->api_version.minor,
					ptr->info->api_version.patch,
				ptr->info->status == MODULE_IN_DEVELOPMENT
					? "In Development"
				: (ptr->info->status == MODULE_ALPHA_RELEASE
					? "Alpha"
				: (ptr->info->status == MODULE_BETA_RELEASE
					? "Beta"
				: (ptr->info->status == MODULE_GA
					? "GA"
				: (ptr->info->status == MODULE_EXPERIMENTAL
					? "Experimental" : "Unknown")))));
		dcb_printf(dcb, "\n");
		ptr = ptr->next;
	}
	dcb_printf(dcb, "----------------+-------------+---------+-------+-------------------------\n\n");
}
