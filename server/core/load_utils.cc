/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file load_utils.c Utility functions for loading of modules
 */

#include "internal/modules.h"

#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <algorithm>

#include <maxscale/modinfo.h>
#include <maxscale/log_manager.h>
#include <maxscale/version.h>
#include <maxscale/paths.h>
#include <maxscale/alloc.h>
#include <maxscale/json_api.h>
#include <maxscale/modulecmd.h>
#include <maxscale/protocol.h>
#include <maxscale/router.h>
#include <maxscale/filter.h>
#include <maxscale/authenticator.h>
#include <maxscale/monitor.h>
#include <maxscale/query_classifier.h>

#include "internal/modules.h"
#include "internal/config.h"

namespace
{

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

struct NAME_MAPPING
{
    const char* type;   // The type of the module.
    const char* from;   // Old module name.
    const char* to;     // What should be loaded instead.
    bool        warned; // Whether a warning has been logged.
};

}

static NAME_MAPPING name_mappings[] =
{
    { MODULE_MONITOR,  "mysqlmon",     "mariadbmon",     false },
    { MODULE_PROTOCOL, "mysqlclient",  "mariadbclient",  false },
    { MODULE_PROTOCOL, "mysqlbackend", "mariadbbackend", false }
};

static const size_t N_NAME_MAPPINGS = sizeof(name_mappings) / sizeof(name_mappings[0]);

static LOADED_MODULE *registered = NULL;

static LOADED_MODULE *find_module(const char *module);
static LOADED_MODULE* register_module(const char *module,
                                      const char *type,
                                      void *dlhandle,
                                      MXS_MODULE *mod_info);
static void unregister_module(const char *module);

static bool api_version_mismatch(const MXS_MODULE *mod_info, const char* module)
{
    bool rval = false;
    MXS_MODULE_VERSION api = {};

    switch (mod_info->modapi)
    {
        case MXS_MODULE_API_PROTOCOL:
            api = MXS_PROTOCOL_VERSION;
            break;

        case MXS_MODULE_API_AUTHENTICATOR:
            api = MXS_AUTHENTICATOR_VERSION;
            break;

        case MXS_MODULE_API_ROUTER:
            api = MXS_ROUTER_VERSION;
            break;

        case MXS_MODULE_API_MONITOR:
            api = MXS_MONITOR_VERSION;
            break;

        case MXS_MODULE_API_FILTER:
            api = MXS_FILTER_VERSION;
            break;

        case MXS_MODULE_API_QUERY_CLASSIFIER:
            api = MXS_QUERY_CLASSIFIER_VERSION;
            break;

        default:
            MXS_ERROR("Unknown module type: 0x%02hhx", mod_info->modapi);
            ss_dassert(!true);
            break;
    }

    if (api.major != mod_info->api_version.major ||
        api.minor != mod_info->api_version.minor ||
        api.patch != mod_info->api_version.patch)
    {
        MXS_ERROR("API version mismatch for '%s': Need version %d.%d.%d, have %d.%d.%d",
                  module, api.major, api.minor, api.patch, mod_info->api_version.major,
                  mod_info->api_version.minor, mod_info->api_version.patch);
        rval = true;
    }

    return rval;
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

    if (api_version_mismatch(mod_info, module))
    {
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

    module = mxs_module_get_effective_name(module);

    if ((mod = find_module(module)) == NULL)
    {
        size_t len = strlen(module);
        char lc_module[len + 1];
        lc_module[len] = 0;
        std::transform(module, module + len, lc_module, tolower);

        /** The module is not already loaded, search for the shared object */
        char fname[MAXPATHLEN + 1];
        snprintf(fname, MAXPATHLEN + 1, "%s/lib%s.so", get_libdir(), lc_module);

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

        void *(*entry_point)() = (void *(*)())sym;
        MXS_MODULE *mod_info = (MXS_MODULE*)entry_point();

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
    module = mxs_module_get_effective_name(module);

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
            if (strcasecmp(mod->module, module) == 0)
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

struct cb_param
{
    json_t* commands;
    const char* domain;
    const char* host;
};

bool modulecmd_cb(const MODULECMD *cmd, void *data)
{
    cb_param* d = static_cast<cb_param*>(data);

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(cmd->identifier));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE_COMMAND));

    json_t* attr = json_object();
    const char* method = MODULECMD_MODIFIES_DATA(cmd) ? "POST" : "GET";
    json_object_set_new(attr, CN_METHOD, json_string(method));
    json_object_set_new(attr, CN_ARG_MIN, json_integer(cmd->arg_count_min));
    json_object_set_new(attr, CN_ARG_MAX, json_integer(cmd->arg_count_max));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(cmd->description));

    json_t* param = json_array();

    for (int i = 0; i < cmd->arg_count_max; i++)
    {
        json_t* p = json_object();
        json_object_set_new(p, CN_DESCRIPTION, json_string(cmd->arg_types[i].description));
        json_object_set_new(p, CN_TYPE, json_string(modulecmd_argtype_to_str(&cmd->arg_types[i])));
        json_object_set_new(p, CN_REQUIRED, json_boolean(MODULECMD_ARG_IS_REQUIRED(&cmd->arg_types[i])));
        json_array_append_new(param, p);
    }

    std::string s = d->domain;
    s += "/";
    s += cmd->identifier;
    ss_dassert(strcmp(d->domain, cmd->domain) == 0);

    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(d->host, CN_MODULES, s.c_str()));
    json_object_set_new(attr, CN_PARAMETERS, param);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);

    json_array_append_new(d->commands, obj);

    return true;
}

static json_t* module_json_data(const LOADED_MODULE *mod, const char* host)
{
    json_t* obj = json_object();

    json_object_set_new(obj, CN_ID, json_string(mod->module));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE));

    json_t* attr = json_object();
    json_object_set_new(attr, "module_type", json_string(mod->type));
    json_object_set_new(attr, "version", json_string(mod->info->version));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(mod->info->description));
    json_object_set_new(attr, "api", json_string(mxs_module_api_to_string(mod->info->modapi)));
    json_object_set_new(attr, "maturity", json_string(mxs_module_status_to_string(mod->info->status)));

    json_t* commands = json_array();
    cb_param p = {commands, mod->module, host};
    modulecmd_foreach(mod->module, NULL, modulecmd_cb, &p);

    json_t* params = json_array();

    for (int i = 0; mod->info->parameters[i].name; i++)
    {
        json_t* p = json_object();

        json_object_set_new(p, CN_NAME, json_string(mod->info->parameters[i].name));
        json_object_set_new(p, CN_TYPE, json_string(mxs_module_param_type_to_string(mod->info->parameters[i].type)));

        if (mod->info->parameters[i].default_value)
        {
            json_object_set(p, "default_value", json_string(mod->info->parameters[i].default_value));
        }

        if (mod->info->parameters[i].type == MXS_MODULE_PARAM_ENUM &&
            mod->info->parameters[i].accepted_values)
        {
            json_t* arr = json_array();

            for (int x = 0; mod->info->parameters[i].accepted_values[x].name; x++)
            {
                json_array_append_new(arr, json_string(mod->info->parameters[i].accepted_values[x].name));
            }

            json_object_set_new(p, "enum_values", arr);
        }

        json_array_append_new(params, p);
    }

    json_object_set_new(attr, "commands", commands);
    json_object_set_new(attr, CN_PARAMETERS, params);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, mod->module));

    return obj;
}

json_t* module_to_json(const MXS_MODULE* module, const char* host)
{
    json_t* data = NULL;

    for (LOADED_MODULE *ptr = registered; ptr; ptr = ptr->next)
    {
        if (ptr->info == module)
        {
            data = module_json_data(ptr, host);
            break;
        }
    }

    // This should always be non-NULL
    ss_dassert(data);

    return mxs_json_resource(host, MXS_JSON_API_MODULES, data);
}

json_t* module_list_to_json(const char* host)
{
    json_t* arr = json_array();

    for (LOADED_MODULE *ptr = registered; ptr; ptr = ptr->next)
    {
        json_array_append_new(arr, module_json_data(ptr, host));
    }

    return mxs_json_resource(host, MXS_JSON_API_MODULES, arr);
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

const MXS_MODULE *get_module(const char *name, const char *type)
{
    name = mxs_module_get_effective_name(name);

    LOADED_MODULE *mod = find_module(name);

    if (mod == NULL && type && load_module(name, type))
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

const char* mxs_module_get_effective_name(const char* name)
{
    const char* effective_name = NULL;
    size_t i = 0;

    while (!effective_name && (i < N_NAME_MAPPINGS))
    {
        NAME_MAPPING& nm = name_mappings[i];

        if (strcasecmp(name, nm.from) == 0)
        {
            if (!nm.warned)
            {
                MXS_WARNING("%s module '%s' has been deprecated, use '%s' instead.",
                            nm.type, nm.from, nm.to);
                nm.warned = true;
            }
            effective_name = nm.to;
        }

        ++i;
    }

    if (!effective_name)
    {
        effective_name = name;
    }

    return effective_name;
}
