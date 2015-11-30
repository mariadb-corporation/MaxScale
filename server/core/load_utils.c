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
 * @file load_utils.c           Utility functions to aid the loading of dynamic
 *                             modules into the gateway
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 13/06/13     Mark Riddoch            Initial implementation
 * 14/06/13     Mark Riddoch            Updated to add call to ModuleInit if one is
 *                                      defined in the loaded module.
 *                                      Also updated to call fixed GetModuleObject
 * 02/06/14     Mark Riddoch            Addition of module info
 * 26/02/15     Massimiliano Pinto      Addition of module_feedback_send
 *
 * @endverbatim
 */
#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <modules.h>
#include <modinfo.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <version.h>
#include <notification.h>
#include <curl/curl.h>
#include <sys/utsname.h>
#include <openssl/sha.h>
#include <gw.h>
#include <gwdirs.h>

static MODULES *registered = NULL;

static MODULES *find_module(const char *module);
static void register_module(const char *module,
                            const char  *type,
                            void        *dlhandle,
                            char        *version,
                            void        *modobj,
                            MODULE_INFO *info);
static void unregister_module(const char *module);
int module_create_feedback_report(GWBUF **buffer, MODULES *modules, FEEDBACK_CONF *cfg);
int do_http_post(GWBUF *buffer, void *cfg);

struct MemoryStruct
{
    char *data;
    size_t size;
};

/**
 * Callback write routine for curl library, getting remote server reply
 *
 * @param       contents        New data to add
 * @param       size            Data size
 * @param       nmemb           Elements in the buffer
 * @param       userp           Pointer to the buffer
 * @return      0 on failure, memory size on success
 *
 */
static size_t
WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    mem->data = realloc(mem->data, mem->size + realsize + 1);
    if (mem->data == NULL)
    {
        /* out of memory! */
        MXS_ERROR("Error in module_feedback_send(), not enough memory for realloc");
        return 0;
    }

    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

/**
 * Load the dynamic library related to a gateway module. The routine
 * will look for library files in the current directory,
 * the configured folder and /usr/lib64/maxscale.
 *
 * @param module        Name of the module to load
 * @param type          Type of module, used purely for registration
 * @return              The module specific entry point structure or NULL
 */
void *
load_module(const char *module, const char *type)
{
    char *home, *version;
    char fname[MAXPATHLEN+1];
    void *dlhandle, *sym;
    char *(*ver)();
    void *(*ep)(), *modobj;
    MODULES *mod;
    MODULE_INFO *mod_info = NULL;

    if ((mod = find_module(module)) == NULL)
    {
        /*<
         * The module is not already loaded
         *
         * Search of the shared object.
         */

        snprintf(fname, MAXPATHLEN+1,"%s/lib%s.so", get_libdir(), module);

        if (access(fname, F_OK) == -1)
        {
            MXS_ERROR("Unable to find library for "
                      "module: %s. Module dir: %s",
                      module, get_libdir());
            return NULL;
        }

        if ((dlhandle = dlopen(fname, RTLD_NOW|RTLD_LOCAL)) == NULL)
        {
            MXS_ERROR("Unable to load library for module: "
                      "%s\n\n\t\t      %s."
                      "\n\n",
                      module,
                      dlerror());
            return NULL;
        }

        if ((sym = dlsym(dlhandle, "version")) == NULL)
        {
            MXS_ERROR("Version interface not supported by "
                      "module: %s\n\t\t\t      %s.",
                      module,
                      dlerror());
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
                MXS_ERROR("Module '%s' does not implement the protocol API.", module);
                fatal = 1;
            }
            if (strcmp(type, MODULE_ROUTER) == 0
                && mod_info->modapi != MODULE_API_ROUTER)
            {
                MXS_ERROR("Module '%s' does not implement the router API.", module);
                fatal = 1;
            }
            if (strcmp(type, MODULE_MONITOR) == 0
                && mod_info->modapi != MODULE_API_MONITOR)
            {
                MXS_ERROR("Module '%s' does not implement the monitor API.", module);
                fatal = 1;
            }
            if (strcmp(type, MODULE_FILTER) == 0
                && mod_info->modapi != MODULE_API_FILTER)
            {
                MXS_ERROR("Module '%s' does not implement the filter API.", module);
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
            MXS_ERROR("Expected entry point interface missing "
                      "from module: %s\n\t\t\t      %s.",
                      module,
                      dlerror());
            dlclose(dlhandle);
            return NULL;
        }
        ep = sym;
        modobj = ep();

        MXS_NOTICE("Loaded module %s: %s from %s",
                   module,
                   version,
                   fname);
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
 * @param module        The name of the module
 */
void
unload_module(const char *module)
{
    MODULES *mod = find_module(module);
    void *handle;

    if (!mod)
    {
        return;
    }
    handle = mod->handle;
    unregister_module(module);
    dlclose(handle);
}

/**
 * Find a module that has been previously loaded and return the handle for that
 * library
 *
 * @param module        The name of the module
 * @return              The module handle or NULL if it was not found
 */
static MODULES *
find_module(const char *module)
{
    MODULES *mod = registered;

    while (mod)
    {
        if (strcmp(mod->module, module) == 0)
        {
            return mod;
        }
        else
        {
            mod = mod->next;
        }
    }
    return NULL;
}

/**
 * Register a newly loaded module. The registration allows for single copies
 * to be loaded and cached entry point information to be return.
 *
 * @param module        The name of the module loaded
 * @param type          The type of the module loaded
 * @param dlhandle      The handle returned by dlopen
 * @param version       The version string returned by the module
 * @param modobj        The module object
 * @param mod_info      The module information
 */
static void
register_module(const char *module,
                const char *type,
                void *dlhandle,
                char *version,
                void *modobj,
                MODULE_INFO *mod_info)
{
    MODULES *mod;

    if ((mod = malloc(sizeof(MODULES))) == NULL)
    {
        return;
    }
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
 * @param module        The name of the module to remove
 */
static void
unregister_module(const char *module)
{
    MODULES *mod = find_module(module);
    MODULES *ptr;

    if (!mod)
    {
        return;         // Module not found
    }
    if (registered == mod)
    {
        registered = mod->next;
    }
    else
    {
        ptr = registered;
        while (ptr && ptr->next != mod)
        {
            ptr = ptr->next;
        }
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
    MODULES *ptr = registered;

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
    MODULES *ptr = registered;

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
 * Print Modules to a DCB
 *
 * Diagnostic routine to display all the loaded modules
 */
void
moduleShowFeedbackReport(DCB *dcb)
{
    GWBUF *buffer;
    MODULES *modules_list = registered;
    FEEDBACK_CONF *feedback_config = config_get_feedback_data();

    if (!module_create_feedback_report(&buffer, modules_list, feedback_config))
    {
        MXS_ERROR("Error in module_create_feedback_report(): gwbuf_alloc() failed to allocate memory");

        return;
    }
    dcb_printf(dcb, (char *)GWBUF_DATA(buffer));
    gwbuf_free(buffer);
}

/**
 * Provide a row to the result set that defines the set of modules
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
moduleRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    int i = 0;;
    char *stat, buf[20];
    RESULT_ROW *row;
    MODULES *ptr;

    ptr = registered;
    while (i < *rowno && ptr)
    {
        i++;
        ptr = ptr->next;
    }
    if (ptr == NULL)
    {
        free(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, ptr->module);
    resultset_row_set(row, 1, ptr->type);
    resultset_row_set(row, 2, ptr->version);
    snprintf(buf,19, "%d.%d.%d", ptr->info->api_version.major,
             ptr->info->api_version.minor,
             ptr->info->api_version.patch);
    buf[19] = '\0';
    resultset_row_set(row, 3, buf);
    resultset_row_set(row, 4, ptr->info->status == MODULE_IN_DEVELOPMENT
                      ? "In Development"
                      : (ptr->info->status == MODULE_ALPHA_RELEASE
                         ? "Alpha"
                         : (ptr->info->status == MODULE_BETA_RELEASE
                            ? "Beta"
                            : (ptr->info->status == MODULE_GA
                               ? "GA"
                               : (ptr->info->status == MODULE_EXPERIMENTAL
                                  ? "Experimental" : "Unknown")))));
    return row;
}

/**
 * Return a resultset that has the current set of modules in it
 *
 * @return A Result set
 */
RESULTSET *
moduleGetList()
{
    RESULTSET       *set;
    int             *data;

    if ((data = (int *)malloc(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(moduleRowCallback, data)) == NULL)
    {
        free(data);
        return NULL;
    }
    resultset_add_column(set, "Module Name", 18, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Module Type", 12, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Version", 10, COL_TYPE_VARCHAR);
    resultset_add_column(set, "API Version", 8, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Status", 15, COL_TYPE_VARCHAR);

    return set;
}

/**
 * Send loaded modules info to notification service
 *
 *  @param data The configuration details of notification service
 */
void
module_feedback_send(void* data)
{
    MODULES *modules_list = registered;
    CURL *curl = NULL;
    CURLcode res;
    struct curl_httppost *formpost=NULL;
    struct curl_httppost *lastptr=NULL;
    GWBUF *buffer = NULL;
    void *data_ptr=NULL;
    long http_code = 0;
    int last_action = _NOTIFICATION_SEND_PENDING;
    time_t now;
    struct tm *now_tm;
    int hour;
    int n_mod=0;
    char hex_setup_info[2 * SHA_DIGEST_LENGTH + 1]="";
    int http_send = 0;

    now = time(NULL);
    struct tm now_result;
    now_tm = localtime_r(&now, &now_result);
    hour = now_tm->tm_hour;

    FEEDBACK_CONF *feedback_config = (FEEDBACK_CONF *) data;

    /* Configuration check */

    if (feedback_config->feedback_enable == 0 ||
        feedback_config->feedback_url == NULL ||
        feedback_config->feedback_user_info == NULL)
    {
        MXS_ERROR("Error in module_feedback_send(): some mandatory parameters are not set"
                  " feedback_enable=%u, feedback_url=%s, feedback_user_info=%s",
                  feedback_config->feedback_enable,
                  feedback_config->feedback_url == NULL ? "NULL" : feedback_config->feedback_url,
                  feedback_config->feedback_user_info == NULL ?
                  "NULL" : feedback_config->feedback_user_info);

        feedback_config->feedback_last_action = _NOTIFICATION_SEND_ERROR;

        return;
    }

    /**
     * Task runs nightly, from 2 AM to 4 AM
     *
     * If it's done in that time interval, it will be skipped
     */

    if (hour > 4 || hour < 2)
    {
        /* It's not the rigt time, mark it as to be done and return */
        feedback_config->feedback_last_action = _NOTIFICATION_SEND_PENDING;

        MXS_INFO("module_feedback_send(): execution skipped, current hour [%d]"
                 " is not within the proper interval (from 2 AM to 4 AM)",
                 hour);

        return;
    }

    /* Time to run the task: if a previous run was succesfull skip next runs */
    if (feedback_config->feedback_last_action == _NOTIFICATION_SEND_OK)
    {
        /* task was done before, return */

        MXS_INFO("module_feedback_send(): execution skipped because of previous "
                 "succesful run: hour is [%d], last_action [%d]",
                 hour, feedback_config->feedback_last_action);

        return;
    }

    MXS_INFO("module_feedback_send(): task now runs: hour is [%d], last_action [%d]",
             hour, feedback_config->feedback_last_action);

    if (!module_create_feedback_report(&buffer, modules_list, feedback_config))
    {
        MXS_ERROR("Error in module_create_feedback_report(): gwbuf_alloc() failed to allocate memory");

        feedback_config->feedback_last_action = _NOTIFICATION_SEND_ERROR;

        return;
    }

    /* try sending data via http/https post */
    http_send = do_http_post(buffer, feedback_config);

    if (http_send == 0)
    {
        feedback_config->feedback_last_action = _NOTIFICATION_SEND_OK;
    }
    else
    {
        feedback_config->feedback_last_action = _NOTIFICATION_SEND_ERROR;

        MXS_INFO("Error in module_create_feedback_report(): do_http_post ret_code is %d", http_send);
    }

    MXS_INFO("module_feedback_send(): task completed: hour is [%d], last_action [%d]",
             hour,
             feedback_config->feedback_last_action);

    gwbuf_free(buffer);

}

/**
 * Create the feedback report as string.
 * I t could be sent to notification service
 * or just printed via maxadmin/telnet
 *
 * @param buffe         The pointr for GWBUF allocation, to be freed by the caller
 * @param modules       The mouleds list
 * @param cfg           The feedback configuration
 * @return              0 on failure, 1 on success
 *
 */

int
module_create_feedback_report(GWBUF **buffer, MODULES *modules, FEEDBACK_CONF *cfg)
{
    MODULES *ptr = modules;
    int n_mod = 0;
    char *data_ptr=NULL;
    char hex_setup_info[2 * SHA_DIGEST_LENGTH + 1]="";
    time_t now;
    struct tm *now_tm;
    int report_max_bytes=0;

    if (buffer == NULL)
    {
        return 0;
    }

    now = time(NULL);

    /* count loaded modules */
    while (ptr)
    {
        ptr = ptr->next;
        n_mod++;
    }

    /* module lists pointer is set back to the head */
    ptr = modules;

    /**
     * allocate gwbuf for data to send
     *
     * each module gives 4 rows
     * product and release rows add 7 rows
     * row is _NOTIFICATION_REPORT_ROW_LEN bytes long
     */

    report_max_bytes = ((n_mod * 4) + 7) * (_NOTIFICATION_REPORT_ROW_LEN + 1);
    *buffer = gwbuf_alloc(report_max_bytes);

    if (*buffer == NULL)
    {
        return 0;
    }

    /* encode MAC-sha1 to HEX */
    gw_bin2hex(hex_setup_info, cfg->mac_sha1, SHA_DIGEST_LENGTH);

    data_ptr = (char *)GWBUF_DATA(*buffer);

    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "FEEDBACK_SERVER_UID\t%s\n", hex_setup_info);
    data_ptr += strlen(data_ptr);
    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "FEEDBACK_USER_INFO\t%s\n",
             cfg->feedback_user_info == NULL ? "not_set" : cfg->feedback_user_info);
    data_ptr += strlen(data_ptr);
    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "VERSION\t%s\n", MAXSCALE_VERSION);
    data_ptr += strlen(data_ptr);
    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN * 2, "NOW\t%lu\nPRODUCT\t%s\n", now, "maxscale");
    data_ptr += strlen(data_ptr);
    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "Uname_sysname\t%s\n", cfg->sysname);
    data_ptr += strlen(data_ptr);
    snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "Uname_distribution\t%s\n", cfg->release_info);
    data_ptr += strlen(data_ptr);

    while (ptr)
    {
        snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN * 2,
                 "module_%s_type\t%s\nmodule_%s_version\t%s\n",
                 ptr->module, ptr->type, ptr->module, ptr->version);
        data_ptr += strlen(data_ptr);

        if (ptr->info)
        {
            snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "module_%s_api\t%d.%d.%d\n",
                     ptr->module,
                     ptr->info->api_version.major,
                     ptr->info->api_version.minor,
                     ptr->info->api_version.patch);

            data_ptr += strlen(data_ptr);
            snprintf(data_ptr, _NOTIFICATION_REPORT_ROW_LEN, "module_%s_releasestatus\t%s\n",
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
            data_ptr += strlen(data_ptr);
        }
        ptr = ptr->next;
    }

    return 1;
}

/**
 * Send data to notification service via http/https
 *
 * @param buffer        The GWBUF with data to send
 * @param cfg           The configuration details of notification service
 * @return              0 on success, != 0 on failure
 */
int
do_http_post(GWBUF *buffer, void *cfg)
{
    CURL *curl = NULL;
    CURLcode res;
    struct curl_httppost *formpost=NULL;
    struct curl_httppost *lastptr=NULL;
    long http_code = 0;
    struct MemoryStruct chunk;
    int ret_code = 1;

    FEEDBACK_CONF *feedback_config = (FEEDBACK_CONF *) cfg;

    /* allocate first memory chunck for httpd servr reply */
    chunk.data = malloc(1);  /* will be grown as needed by the realloc above */
    chunk.size = 0;    /* no data at this point */

    /* Initializing curl library for data send via HTTP */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();

    if (curl) {
        char error_message[CURL_ERROR_SIZE]="";

        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_message);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, feedback_config->feedback_connect_timeout);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, feedback_config->feedback_timeout);

        /* curl API call for data send via HTTP POST using a "file" type input */
        curl_formadd(&formpost,
                     &lastptr,
                     CURLFORM_COPYNAME, "data",
                     CURLFORM_BUFFER, "report.txt",
                     CURLFORM_BUFFERPTR, (char *)GWBUF_DATA(buffer),
                     CURLFORM_BUFFERLENGTH, strlen((char *)GWBUF_DATA(buffer)),
                     CURLFORM_CONTENTTYPE, "text/plain",
                     CURLFORM_END);

        curl_easy_setopt(curl, CURLOPT_HEADER, 1);

        /* some servers don't like requests that are made without a user-agent field, so we provide one */
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "MaxScale-agent/http-1.0");
        /* Force HTTP/1.0 */
        curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);

        curl_easy_setopt(curl, CURLOPT_URL, feedback_config->feedback_url);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);

        /* send all data to this function  */
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);

        /* we pass our 'chunk' struct to the callback function */
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if (res != CURLE_OK)
        {
            ret_code = 2;
            MXS_ERROR("do_http_post(), curl call for [%s] failed due: %s, %s",
                      feedback_config->feedback_url,
                      curl_easy_strerror(res),
                      error_message);
            goto cleanup;
        }
        else
        {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        if (http_code == 302)
        {
            char *from = strstr(chunk.data, "<h1>ok</h1>");
            if (from)
            {
                ret_code = 0;
            }
            else
            {
                ret_code = 3;
                goto cleanup;
            }
        }
        else
        {
            MXS_ERROR("do_http_post(), Bad HTTP Code from remote server: %lu", http_code);
            ret_code = 4;
            goto cleanup;
        }
    }
    else
    {
        MXS_ERROR("do_http_post(), curl object not initialized");
        ret_code = 1;
        goto cleanup;
    }

    MXS_INFO("do_http_post() ret_code [%d], HTTP code [%ld]",
             ret_code, http_code);
cleanup:

    if (chunk.data)
    {
        free(chunk.data);
    }

    if (curl)
    {
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
    }

    curl_global_cleanup();

    return ret_code;
}

