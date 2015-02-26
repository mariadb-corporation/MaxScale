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
 * @file load_utils.c		Utility functions to aid the loading of dynamic
 *                             modules into the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 13/06/13	Mark Riddoch		Initial implementation
 * 14/06/13	Mark Riddoch		Updated to add call to ModuleInit if one is
 *                             	 	defined in the loaded module.
 * 					Also updated to call fixed GetModuleObject
 * 02/06/14	Mark Riddoch		Addition of module info
 * 26/02/15	Massimiliano	Pinto	Addition of module_feedback_send
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
#include	<version.h>
#include	<curl/curl.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static	MODULES	*registered = NULL;

static MODULES *find_module(const char *module);
static void register_module(const char *module,
                            const char  *type,
                            void        *dlhandle,
                            char        *version,
                            void        *modobj,
			    MODULE_INFO *info);
static void unregister_module(const char *module);

struct MemoryStruct {
  char *data;
  size_t size;
};

static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct MemoryStruct *mem = (struct MemoryStruct *)userp;

  mem->data = realloc(mem->data, mem->size + realsize + 1);
  if(mem->data == NULL) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }

  memcpy(&(mem->data[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->data[mem->size] = 0;

  return realsize;
}

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
char		fname[MAXPATHLEN+1];
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
		snprintf(fname,MAXPATHLEN+1, "./lib%s.so", module);
		
		if (access(fname, F_OK) == -1)
		{
			home = get_maxscale_home ();
			snprintf(fname, MAXPATHLEN+1,"%s/modules/lib%s.so", home, module);

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
	dlclose(mod->handle);
	free(mod->module);
	free(mod->type);
	free(mod->version);
	free(mod);
}

/**
 * Unload all modules
 *
 * Remove all the modules from the system, called during shutdown
 * to allow termination hooks to be called.
 */
void
unload_all_modules()
{
	while (registered)
	{
		unregister_module(registered->module);
	}
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

/**
 * Send loaded modules info to notification service
 *
 *  @param data The configuration details of notification service
 */
void
module_feedback_send(void* data)
{
	MODULES *ptr = registered;
	CURL *curl;
	CURLcode res;
	struct curl_httppost *formpost=NULL;
	struct curl_httppost *lastptr=NULL;
	GWBUF *buffer = NULL;
	char  *data_ptr = NULL;
	int   n_mod=0;
	struct MemoryStruct chunk;

        chunk.data = malloc(1);		/* will be grown as needed by the realloc above */
	chunk.size = 0;			/* no data at this point */
	
	/* count loaded modules */
	while (ptr)
        {
		ptr = ptr->next;
		n_mod++;
	}
	ptr = registered;

	buffer = gwbuf_alloc(n_mod * 256);
	data_ptr = GWBUF_DATA(buffer);

	while (ptr)
	{
		/* current maxscale setup */
		sprintf(data_ptr, "FEEDBACK_SERVER_UID\t%s\n", "xxxfcBRIvkRlxyGdoJL0bWy+TmY");
		data_ptr+=strlen(data_ptr);
		sprintf(data_ptr, "FEEDBACK_USER_INFO\t%s\n", "0467009f-xxxx-yyyy-zzzz-b6b2ec9c6cf4");
		data_ptr+=strlen(data_ptr);
		sprintf(data_ptr, "VERSION\t%s\n", MAXSCALE_VERSION);
		data_ptr+=strlen(data_ptr);
		sprintf(data_ptr, "NOW\t%lu\nPRODUCT\t%s\n", time(NULL), "maxscale");
		data_ptr+=strlen(data_ptr);
		sprintf(data_ptr, "Uname_sysname\t%s\n", "linux");
		data_ptr+=strlen(data_ptr);
		sprintf(data_ptr, "Uname_distribution\t%s\n", "centos");
		data_ptr+=strlen(data_ptr);

		/* modules data */
		sprintf(data_ptr, "module_%s_type\t%s\nmodule_%s_version\t%s\n", ptr->module, ptr->type, ptr->module, ptr->version);
		data_ptr+=strlen(data_ptr);

		if (ptr->info) {
			sprintf(data_ptr, "module_%s_api\t%d.%d.%d\n",
				ptr->module,
				ptr->info->api_version.major,
				ptr->info->api_version.minor,
				ptr->info->api_version.patch);

			data_ptr+=strlen(data_ptr);
			sprintf(data_ptr, "module_%s_releasestatus\t%s\n",
				ptr->module,
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
					data_ptr+=strlen(data_ptr);
		}
		ptr = ptr->next;
	}	

	/* curl API call for data send via HTTP POST using a "file" type input */
	curl = curl_easy_init();

	if(curl) {
		curl_formadd(&formpost,
			&lastptr,
			CURLFORM_COPYNAME, "data",
			CURLFORM_BUFFER, "report.txt",
			CURLFORM_BUFFERPTR, (char *)GWBUF_DATA(buffer),
			CURLFORM_BUFFERLENGTH, strlen((char *)GWBUF_DATA(buffer)),
			CURLFORM_CONTENTTYPE, "text/plain",
			CURLFORM_END);

		curl_easy_setopt(curl, CURLOPT_URL, "http://127.0.0.1/post.php");
		curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

		/* send all received data to this function  */
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

		/* we pass our 'chunk' struct to the callback function */
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

		/* some servers don't like requests that are made without a user-agent field, so we provide one */
		curl_easy_setopt(curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");

		/* Perform the request, res will get the return code */
		res = curl_easy_perform(curl);

		/* Check for errors */
		if(res != CURLE_OK)
			fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		else
			fprintf(stderr, "Reply from remote server is\n[%s]\n", chunk.data);
	}

	if(chunk.data)
		free(chunk.data);

	gwbuf_free(buffer);
	curl_easy_cleanup(curl);
	curl_formfree(formpost);
}
