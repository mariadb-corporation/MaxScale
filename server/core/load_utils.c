/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>
#include <maxscale/version.h>
#include <maxscale/notification.h>
#include <curl/curl.h>
#include <sys/utsname.h>
#include <openssl/sha.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>

#include "maxscale/modules.h"

typedef struct loaded_module
{
    char    *module;       /**< The name of the module */
    char    *type;         /**< The module type */
    char    *version;      /**< Module version */
    void    *handle;       /**< The handle returned by dlopen */
    void    *modobj;       /**< The module "object" this is the set of entry points */
    MXS_MODULE *info;     /**< The module information */
    struct  loaded_module *next; /**< Next module in the linked list */
} LOADED_MODULE;

static LOADED_MODULE *registered = NULL;

static LOADED_MODULE *find_module(const char *module);
static LOADED_MODULE* register_module(const char *module,
                                      const char *type,
                                      void *dlhandle,
                                      MXS_MODULE *mod_info);
static void unregister_module(const char *module);
int module_create_feedback_report(GWBUF **buffer, LOADED_MODULE *modules, FEEDBACK_CONF *cfg);
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

    void *data = MXS_REALLOC(mem->data, mem->size + realsize + 1);

    if (data == NULL)
    {
        return 0;
    }

    mem->data = data;
    memcpy(&(mem->data[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->data[mem->size] = 0;

    return realsize;
}

static bool check_module(const MXS_MODULE *mod_info, const char *type, const char *module)
{
    bool success = true;

    if (strcmp(type, MODULE_PROTOCOL) == 0
        && mod_info->modapi != MXS_MODULE_API_PROTOCOL)
    {
        MXS_ERROR("Module '%s' does not implement the protocol API.", module);
        success = false;
    }
    if (strcmp(type, MODULE_AUTHENTICATOR) == 0
        && mod_info->modapi != MXS_MODULE_API_AUTHENTICATOR)
    {
        MXS_ERROR("Module '%s' does not implement the authenticator API.", module);
        success = false;
    }
    if (strcmp(type, MODULE_ROUTER) == 0
        && mod_info->modapi != MXS_MODULE_API_ROUTER)
    {
        MXS_ERROR("Module '%s' does not implement the router API.", module);
        success = false;
    }
    if (strcmp(type, MODULE_MONITOR) == 0
        && mod_info->modapi != MXS_MODULE_API_MONITOR)
    {
        MXS_ERROR("Module '%s' does not implement the monitor API.", module);
        success = false;
    }
    if (strcmp(type, MODULE_FILTER) == 0
        && mod_info->modapi != MXS_MODULE_API_FILTER)
    {
        MXS_ERROR("Module '%s' does not implement the filter API.", module);
        success = false;
    }
    if (strcmp(type, MODULE_QUERY_CLASSIFIER) == 0
        && mod_info->modapi != MXS_MODULE_API_QUERY_CLASSIFIER)
    {
        MXS_ERROR("Module '%s' does not implement the query classifier API.", module);
        success = false;
    }
    if (mod_info->version == NULL)
    {
        MXS_ERROR("Module '%s' does not define a version string", module);
        success = false;
    }

    if (mod_info->module_object == NULL)
    {
        MXS_ERROR("Module '%s' does not define a module object", module);
        success = false;
    }

    return success;
}

void *load_module(const char *module, const char *type)
{
    ss_dassert(module && type);
    LOADED_MODULE *mod;

    if ((mod = find_module(module)) == NULL)
    {
        /** The module is not already loaded, search for the shared object */
        char fname[MAXPATHLEN + 1];
        snprintf(fname, MAXPATHLEN + 1, "%s/lib%s.so", get_libdir(), module);

        if (access(fname, F_OK) == -1)
        {
            MXS_ERROR("Unable to find library for "
                      "module: %s. Module dir: %s",
                      module, get_libdir());
            return NULL;
        }

        void *dlhandle = dlopen(fname, RTLD_NOW | RTLD_LOCAL);

        if (dlhandle == NULL)
        {
            MXS_ERROR("Unable to load library for module: "
                      "%s\n\n\t\t      %s."
                      "\n\n",
                      module, dlerror());
            return NULL;
        }

        void *sym = dlsym(dlhandle, MXS_MODULE_SYMBOL_NAME);

        if (sym == NULL)
        {
            MXS_ERROR("Expected entry point interface missing "
                      "from module: %s\n\t\t\t      %s.",
                      module, dlerror());
            dlclose(dlhandle);
            return NULL;
        }

        void *(*entry_point)() = sym;
        MXS_MODULE *mod_info = entry_point();

        if (!check_module(mod_info, type, module) ||
            (mod = register_module(module, type, dlhandle, mod_info)) == NULL)
        {
            dlclose(dlhandle);
            return NULL;
        }

        MXS_NOTICE("Loaded module %s: %s from %s", module, mod_info->version, fname);
    }

    return mod->modobj;
}

void unload_module(const char *module)
{
    LOADED_MODULE *mod = find_module(module);

    if (mod)
    {
        void *handle = mod->handle;
        unregister_module(module);
        dlclose(handle);
    }
}

/**
 * Find a module that has been previously loaded and return the handle for that
 * library
 *
 * @param module        The name of the module
 * @return              The module handle or NULL if it was not found
 */
static LOADED_MODULE *
find_module(const char *module)
{
    LOADED_MODULE *mod = registered;

    if (module)
    {
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
 * @return The new registered module or NULL on memory allocation failure
 */
static LOADED_MODULE* register_module(const char *module,
                                      const char *type,
                                      void *dlhandle,
                                      MXS_MODULE *mod_info)
{
    module = MXS_STRDUP(module);
    type = MXS_STRDUP(type);
    char *version = MXS_STRDUP(mod_info->version);

    LOADED_MODULE *mod = (LOADED_MODULE *)MXS_MALLOC(sizeof(LOADED_MODULE));

    if (!module || !type || !version || !mod)
    {
        MXS_FREE((void*)module);
        MXS_FREE((void*)type);
        MXS_FREE(version);
        MXS_FREE(mod);
        return NULL;
    }

    mod->module = (char*)module;
    mod->type = (char*)type;
    mod->handle = dlhandle;
    mod->version = version;
    mod->modobj = mod_info->module_object;
    mod->next = registered;
    mod->info = mod_info;
    registered = mod;
    return mod;
}

/**
 * Unregister a module
 *
 * @param module        The name of the module to remove
 */
static void
unregister_module(const char *module)
{
    LOADED_MODULE *mod = find_module(module);
    LOADED_MODULE *ptr;

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

        /*<
         * Remove the module to be be freed from the list.
         */
        if (ptr && (ptr->next == mod))
        {
            ptr->next = ptr->next->next;
        }
    }

    /*<
     * The module is now not in the linked list and all
     * memory related to it can be freed
     */
    dlclose(mod->handle);
    MXS_FREE(mod->module);
    MXS_FREE(mod->type);
    MXS_FREE(mod->version);
    MXS_FREE(mod);
}

void unload_all_modules()
{
    while (registered)
    {
        unregister_module(registered->module);
    }
}

void printModules()
{
    LOADED_MODULE *ptr = registered;

    printf("%-15s | %-11s | Version\n", "Module Name", "Module Type");
    printf("-----------------------------------------------------\n");
    while (ptr)
    {
        printf("%-15s | %-11s | %s\n", ptr->module, ptr->type, ptr->version);
        ptr = ptr->next;
    }
}

void dprintAllModules(DCB *dcb)
{
    LOADED_MODULE *ptr = registered;

    dcb_printf(dcb, "Modules.\n");
    dcb_printf(dcb, "----------------+-----------------+---------+-------+-------------------------\n");
    dcb_printf(dcb, "%-15s | %-15s | Version | API   | Status\n", "Module Name", "Module Type");
    dcb_printf(dcb, "----------------+-----------------+---------+-------+-------------------------\n");
    while (ptr)
    {
        dcb_printf(dcb, "%-15s | %-15s | %-7s ", ptr->module, ptr->type, ptr->version);
        if (ptr->info)
            dcb_printf(dcb, "| %d.%d.%d | %s",
                       ptr->info->api_version.major,
                       ptr->info->api_version.minor,
                       ptr->info->api_version.patch,
                       ptr->info->status == MXS_MODULE_IN_DEVELOPMENT
                       ? "In Development"
                       : (ptr->info->status == MXS_MODULE_ALPHA_RELEASE
                          ? "Alpha"
                          : (ptr->info->status == MXS_MODULE_BETA_RELEASE
                             ? "Beta"
                             : (ptr->info->status == MXS_MODULE_GA
                                ? "GA"
                                : (ptr->info->status == MXS_MODULE_EXPERIMENTAL
                                   ? "Experimental" : "Unknown")))));
        dcb_printf(dcb, "\n");
        ptr = ptr->next;
    }
    dcb_printf(dcb, "----------------+-----------------+---------+-------+-------------------------\n\n");
}

void moduleShowFeedbackReport(DCB *dcb)
{
    GWBUF *buffer;
    LOADED_MODULE *modules_list = registered;
    FEEDBACK_CONF *feedback_config = config_get_feedback_data();

    if (!module_create_feedback_report(&buffer, modules_list, feedback_config))
    {
        MXS_ERROR("Error in module_create_feedback_report(): gwbuf_alloc() failed to allocate memory");

        return;
    }
    dcb_printf(dcb, "%s", (char *)GWBUF_DATA(buffer));
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
    LOADED_MODULE *ptr;

    ptr = registered;
    while (i < *rowno && ptr)
    {
        i++;
        ptr = ptr->next;
    }
    if (ptr == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    (*rowno)++;
    row = resultset_make_row(set);
    resultset_row_set(row, 0, ptr->module);
    resultset_row_set(row, 1, ptr->type);
    resultset_row_set(row, 2, ptr->version);
    snprintf(buf, 19, "%d.%d.%d", ptr->info->api_version.major,
             ptr->info->api_version.minor,
             ptr->info->api_version.patch);
    buf[19] = '\0';
    resultset_row_set(row, 3, buf);
    resultset_row_set(row, 4, ptr->info->status == MXS_MODULE_IN_DEVELOPMENT
                      ? "In Development"
                      : (ptr->info->status == MXS_MODULE_ALPHA_RELEASE
                         ? "Alpha"
                         : (ptr->info->status == MXS_MODULE_BETA_RELEASE
                            ? "Beta"
                            : (ptr->info->status == MXS_MODULE_GA
                               ? "GA"
                               : (ptr->info->status == MXS_MODULE_EXPERIMENTAL
                                  ? "Experimental" : "Unknown")))));
    return row;
}

RESULTSET *moduleGetList()
{
    RESULTSET       *set;
    int             *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(moduleRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Module Name", 18, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Module Type", 12, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Version", 10, COL_TYPE_VARCHAR);
    resultset_add_column(set, "API Version", 8, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Status", 15, COL_TYPE_VARCHAR);

    return set;
}

void module_feedback_send(void* data)
{
    LOADED_MODULE *modules_list = registered;
    CURL *curl = NULL;
    CURLcode res;
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;
    GWBUF *buffer = NULL;
    void *data_ptr = NULL;
    long http_code = 0;
    int last_action = _NOTIFICATION_SEND_PENDING;
    time_t now;
    struct tm *now_tm;
    int hour;
    int n_mod = 0;
    char hex_setup_info[2 * SHA_DIGEST_LENGTH + 1] = "";
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
module_create_feedback_report(GWBUF **buffer, LOADED_MODULE *modules, FEEDBACK_CONF *cfg)
{
    LOADED_MODULE *ptr = modules;
    int n_mod = 0;
    char *data_ptr = NULL;
    char hex_setup_info[2 * SHA_DIGEST_LENGTH + 1] = "";
    time_t now;
    struct tm *now_tm;
    int report_max_bytes = 0;

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
                     ptr->info->status == MXS_MODULE_IN_DEVELOPMENT
                     ? "In Development"
                     : (ptr->info->status == MXS_MODULE_ALPHA_RELEASE
                        ? "Alpha"
                        : (ptr->info->status == MXS_MODULE_BETA_RELEASE
                           ? "Beta"
                           : (ptr->info->status == MXS_MODULE_GA
                              ? "GA"
                              : (ptr->info->status == MXS_MODULE_EXPERIMENTAL
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
    struct curl_httppost *formpost = NULL;
    struct curl_httppost *lastptr = NULL;
    long http_code = 0;
    struct MemoryStruct chunk;
    int ret_code = 1;

    FEEDBACK_CONF *feedback_config = (FEEDBACK_CONF *) cfg;

    /* allocate first memory chunck for httpd servr reply */
    chunk.data = MXS_MALLOC(1);  /* will be grown as needed by the realloc above */
    MXS_ABORT_IF_NULL(chunk.data);
    chunk.size = 0;    /* no data at this point */

    /* Initializing curl library for data send via HTTP */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();

    if (curl)
    {
        char error_message[CURL_ERROR_SIZE] = "";

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
        MXS_FREE(chunk.data);
    }

    if (curl)
    {
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
    }

    curl_global_cleanup();

    return ret_code;
}

const MXS_MODULE *get_module(const char *name, const char *type)
{
    LOADED_MODULE *mod = find_module(name);

    if (mod == NULL && load_module(name, type))
    {
        mod = find_module(name);
    }

    return mod ? mod->info : NULL;
}

MXS_MODULE_ITERATOR mxs_module_iterator_get(const char* type)
{
    LOADED_MODULE* module = registered;

    while (module && type && (strcmp(module->type, type) != 0))
    {
        module = module->next;
    }

    MXS_MODULE_ITERATOR iterator;
    iterator.type = type;
    iterator.position = module;

    return iterator;
}

bool mxs_module_iterator_has_next(const MXS_MODULE_ITERATOR* iterator)
{
    return iterator->position != NULL;
}

MXS_MODULE* mxs_module_iterator_get_next(MXS_MODULE_ITERATOR* iterator)
{
    MXS_MODULE* module = NULL;
    LOADED_MODULE* loaded_module = (LOADED_MODULE*)iterator->position;

    if (loaded_module)
    {
        module = loaded_module->info;

        do
        {
            loaded_module = loaded_module->next;
        }
        while (loaded_module && iterator->type && (strcmp(loaded_module->type, iterator->type) != 0));

        iterator->position = loaded_module;
    }

    return module;
}
