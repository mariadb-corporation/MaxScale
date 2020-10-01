/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-09-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file load_utils.c Utility functions for loading of modules
 */

#include <sys/param.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dlfcn.h>
#include <ftw.h>
#include <algorithm>
#include <string>

#include <maxscale/modinfo.hh>
#include <maxscale/version.h>
#include <maxscale/paths.hh>
#include <maxbase/alloc.h>
#include <maxscale/json_api.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/protocol.hh>
#include <maxscale/router.hh>
#include <maxscale/filter.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/monitor.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/routingworker.hh>
#include <maxbase/format.hh>

#include "internal/modules.hh"
#include "internal/config.hh"
#include "internal/monitor.hh"
#include "internal/server.hh"
#include "internal/service.hh"
#include "internal/listener.hh"

using std::string;
using mxs::ModuleStatus;
using mxs::ModuleType;

namespace
{
const char CN_ARG_MAX[] = "arg_max";
const char CN_ARG_MIN[] = "arg_min";
const char CN_METHOD[] = "method";
const char CN_MODULES[] = "modules";
const char CN_MODULE_COMMAND[] = "module_command";
const char wrong_mod_type[] = "Module '%s' is a %s, not a %s.";

struct LOADED_MODULE
{
    string      name;               /**< The name of the module */
    MXS_MODULE* info {nullptr};     /**< The module information */
    void*       handle {nullptr};   /**< The handle returned by dlopen */

    LOADED_MODULE(void* dlhandle, MXS_MODULE* info)
        : info(info)
        , handle(dlhandle)
    {
    }
};

/**
 * Module name to module mapping. Stored alphabetically, names in lowercase. Only accessed from the main
 * thread. */
std::map<string, LOADED_MODULE*> loaded_modules;

struct NAME_MAPPING
{
    ModuleType  type;   // The type of the module.
    const char* from;   // Old module name.
    const char* to;     // What should be loaded instead.
    bool        warned; // Whether a warning has been logged.
};

LOADED_MODULE* find_module(const string& name);
const char*    module_type_to_string(ModuleType type);
const char*    module_maturity_to_string(ModuleStatus type);
const char*    mxs_module_param_type_to_string(mxs_module_param_type type);
}

static NAME_MAPPING name_mappings[] =
{
    {ModuleType::MONITOR,       "mysqlmon",    "mariadbmon",    false},
    {ModuleType::PROTOCOL,      "mysqlclient", "mariadbclient", false},
    {ModuleType::PROTOCOL,      "mariadb",     "mariadbclient", true },
    {ModuleType::AUTHENTICATOR, "mysqlauth",   "mariadbauth",   false},
};

static const size_t N_NAME_MAPPINGS = sizeof(name_mappings) / sizeof(name_mappings[0]);


static bool api_version_mismatch(const MXS_MODULE* mod_info, const string& name)
{
    bool rval = false;
    MXS_MODULE_VERSION api = {};

    switch (mod_info->modapi)
    {
    case ModuleType::PROTOCOL:
        api = MXS_PROTOCOL_VERSION;
        break;

    case ModuleType::AUTHENTICATOR:
        api = MXS_AUTHENTICATOR_VERSION;
        break;

    case ModuleType::ROUTER:
        api = MXS_ROUTER_VERSION;
        break;

    case ModuleType::MONITOR:
        api = MXS_MONITOR_VERSION;
        break;

    case ModuleType::FILTER:
        api = MXS_FILTER_VERSION;
        break;

    case ModuleType::QUERY_CLASSIFIER:
        api = MXS_QUERY_CLASSIFIER_VERSION;
        break;

    default:
        MXS_ERROR("Unknown module type: 0x%02hhx", (unsigned char)mod_info->modapi);
        mxb_assert(!true);
        break;
    }

    if (api.major != mod_info->api_version.major
        || api.minor != mod_info->api_version.minor
        || api.patch != mod_info->api_version.patch)
    {
        MXS_ERROR("API version mismatch for '%s': Need version %d.%d.%d, have %d.%d.%d",
                  name.c_str(),
                  api.major,
                  api.minor,
                  api.patch,
                  mod_info->api_version.major,
                  mod_info->api_version.minor,
                  mod_info->api_version.patch);
        rval = true;
    }

    return rval;
}

namespace
{

bool check_module(const MXS_MODULE* mod_info, const string& name, ModuleType expected_type)
{
    auto namec = name.c_str();
    bool success = true;
    if (expected_type != ModuleType::UNKNOWN)
    {
        auto found_type = mod_info->modapi;
        if (found_type != expected_type)
        {
            auto expected_type_str = module_type_to_string(expected_type);
            auto found_type_str = module_type_to_string(found_type);
            MXS_ERROR(wrong_mod_type, namec, found_type_str, expected_type_str);
            success = false;
        }
    }

    if (api_version_mismatch(mod_info, name))
    {
        success = false;
    }

    if (mod_info->version == NULL)
    {
        MXS_ERROR("Module '%s' does not define a version string.", namec);
        success = false;
    }

    if (mod_info->module_object == NULL)
    {
        MXS_ERROR("Module '%s' does not define any API functions.", namec);
        success = false;
    }

    return success;
}
}

static bool is_maxscale_module(const char* fpath)
{
    bool rval = false;

    if (void* dlhandle = dlopen(fpath, RTLD_LAZY | RTLD_LOCAL))
    {
        if (void* sym = dlsym(dlhandle, MXS_MODULE_SYMBOL_NAME))
        {
            Dl_info info;

            if (dladdr(sym, &info))
            {
                if (strcmp(info.dli_fname, fpath) == 0)
                {
                    // The module entry point symbol is located in the file we're loading,
                    // this is a MaxScale module.
                    rval = true;
                }
            }
        }

        dlclose(dlhandle);
    }

    if (!rval)
    {
        MXS_INFO("Not a MaxScale module: %s", fpath);
    }

    return rval;
}

static int load_module_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    int rval = 0;

    if (typeflag == FTW_F)
    {
        const char* filename = fpath + ftwbuf->base;

        if (strncmp(filename, "lib", 3) == 0)
        {
            const char* name = filename + 3;

            if (const char* dot = strchr(filename, '.'))
            {
                std::string module(name, dot);

                if (is_maxscale_module(fpath) && !load_module(module.c_str(), ModuleType::UNKNOWN))
                {
                    MXS_ERROR("Failed to load '%s'. Make sure it is not a stale library "
                              "left over from an old installation of MaxScale.", fpath);
                    rval = 1;
                }
            }
        }
    }

    return rval;
}

bool load_all_modules()
{
    int rv = nftw(mxs::libdir(), load_module_cb, 10, FTW_PHYS);
    return rv == 0;
}

void* load_module(const char* name, mxs::ModuleType type)
{
    mxb_assert(name);
    string eff_name = module_get_effective_name(name);
    auto loaded_module = find_module(eff_name);
    if (loaded_module)
    {
        // Module was already loaded, return API functions.
        return loaded_module->info->module_object;
    }

    void* rval = nullptr;
    string load_errmsg;

    /** The module is not already loaded, search for the shared object */
    string fname = mxb::string_printf("%s/lib%s.so", mxs::libdir(), eff_name.c_str());
    auto fnamec = fname.c_str();
    if (access(fnamec, F_OK) != 0)
    {
        int eno = errno;
        load_errmsg = mxb::string_printf("Cannot access library file '%s'. Error %i: %s",
                                         fnamec, eno, mxb_strerror(eno));
    }
    else
    {
        void* dlhandle = dlopen(fnamec, RTLD_NOW | RTLD_LOCAL);
        if (!dlhandle)
        {
            load_errmsg = mxb::string_printf("Cannot load library file '%s'. %s.", fnamec, dlerror());
        }
        else
        {
            void* sym = dlsym(dlhandle, MXS_MODULE_SYMBOL_NAME);
            if (!sym)
            {
                load_errmsg = mxb::string_printf("Library file '%s' does not contain the entry point "
                                                 "function. %s.", fnamec, dlerror());
                dlclose(dlhandle);
            }
            else
            {
                // Module was loaded, check that it's valid.
                auto entry_point = (void* (*)())sym;
                auto mod_info = (MXS_MODULE*)entry_point();
                if (!check_module(mod_info, eff_name, type))
                {
                    dlclose(dlhandle);
                }
                else
                {
                    loaded_module = new LOADED_MODULE(dlhandle, mod_info);
                }
            }
        }
    }

    if (loaded_module)
    {
        auto mod_info = loaded_module->info;
        auto mod_name_low = mxb::tolower(mod_info->name);
        mxb_assert(loaded_modules.count(mod_name_low) == 0);
        loaded_modules.insert(std::make_pair(mod_name_low, loaded_module));
        MXS_NOTICE("Module '%s' loaded from '%s'.", mod_info->name, fname.c_str());

        // Run module process/thread init functions.
        if (mxs::RoutingWorker::is_running())
        {
            if (mod_info->process_init)
            {
                mod_info->process_init();
            }

            if (mod_info->thread_init)
            {
                mxs::RoutingWorker::broadcast(
                    [mod_info]() {
                        mod_info->thread_init();
                    }, mxs::RoutingWorker::EXECUTE_AUTO);

                if (mxs::MainWorker::created())
                {
                    mxs::MainWorker::get()->call(
                        [mod_info]() {
                            mod_info->thread_init();
                        }, mxb::Worker::EXECUTE_AUTO);
                }
            }
        }
        rval = loaded_module->info->module_object;
    }
    else if (!load_errmsg.empty())
    {
        MXB_ERROR("Cannot load module '%s'. %s", name, load_errmsg.c_str());
    }
    return rval;
}

void unload_module(const char* name)
{
    string eff_name = module_get_effective_name(name);
    auto iter = loaded_modules.find(eff_name);
    if (iter != loaded_modules.end())
    {
        auto module = iter->second;
        loaded_modules.erase(iter);
        // The module is no longer in the container and all related memory can be freed.
        dlclose(module->handle);
        delete module;
    }
}

namespace
{

/**
 * Find a module that has been previously loaded.
 *
 * @param name The name of the module, in lowercase
 * @return     The module handle or NULL if it was not found
 */
LOADED_MODULE* find_module(const string& name)
{
    LOADED_MODULE* rval = nullptr;
    auto iter = loaded_modules.find(name);
    if (iter != loaded_modules.end())
    {
        rval = iter->second;
    }
    return rval;
}
}

void unload_all_modules()
{
    while (!loaded_modules.empty())
    {
        auto first = loaded_modules.begin();
        unload_module(first->first.c_str());
    }
}

struct cb_param
{
    json_t*     commands;
    const char* domain;
    const char* host;
};

bool modulecmd_cb(const MODULECMD* cmd, void* data)
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
    mxb_assert(strcasecmp(d->domain, cmd->domain) == 0);

    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(d->host, CN_MODULES, s.c_str()));
    json_object_set_new(attr, CN_PARAMETERS, param);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);

    json_array_append_new(d->commands, obj);

    return true;
}

static json_t* default_value_to_json(mxs_module_param_type type, const char* value)
{
    switch (type)
    {
    case MXS_MODULE_PARAM_COUNT:
    case MXS_MODULE_PARAM_INT:
        return json_integer(strtol(value, nullptr, 10));

    case MXS_MODULE_PARAM_SIZE:
        {
            uint64_t val = 0;
            get_suffixed_size(value, &val);
            return json_integer(val);
        }

    case MXS_MODULE_PARAM_BOOL:
        return json_boolean(config_truth_value(value));

    case MXS_MODULE_PARAM_STRING:
    case MXS_MODULE_PARAM_QUOTEDSTRING:
    case MXS_MODULE_PARAM_PASSWORD:
    case MXS_MODULE_PARAM_ENUM:
    case MXS_MODULE_PARAM_PATH:
    case MXS_MODULE_PARAM_SERVICE:
    case MXS_MODULE_PARAM_SERVER:
    case MXS_MODULE_PARAM_TARGET:
    case MXS_MODULE_PARAM_SERVERLIST:
    case MXS_MODULE_PARAM_TARGETLIST:
    case MXS_MODULE_PARAM_REGEX:
    case MXS_MODULE_PARAM_DURATION:
        return json_string(value);

    default:
        mxb_assert(!true);
        return json_null();
    }
}
static json_t* module_param_to_json(const MXS_MODULE_PARAM& param)
{
    json_t* p = json_object();
    const char* type;

    if (param.type == MXS_MODULE_PARAM_ENUM && (param.options & MXS_MODULE_OPT_ENUM_UNIQUE) == 0)
    {
        type = "enum_mask";
    }
    else
    {
        type = mxs_module_param_type_to_string(param.type);
    }

    json_object_set_new(p, CN_NAME, json_string(param.name));
    json_object_set_new(p, CN_TYPE, json_string(type));

    if (param.default_value)
    {
        json_object_set_new(p, "default_value", default_value_to_json(param.type, param.default_value));
    }

    json_object_set_new(p, "mandatory", json_boolean(param.options & MXS_MODULE_OPT_REQUIRED));

    if (param.type == MXS_MODULE_PARAM_ENUM && param.accepted_values)
    {
        json_t* arr = json_array();

        for (int x = 0; param.accepted_values[x].name; x++)
        {
            json_array_append_new(arr, json_string(param.accepted_values[x].name));
        }

        json_object_set_new(p, "enum_values", arr);
    }
    else if (param.type == MXS_MODULE_PARAM_DURATION)
    {
        const char* value_unit = param.options & MXS_MODULE_OPT_DURATION_S ? "s" : "ms";
        json_object_set_new(p, "unit", json_string(value_unit));
    }

    return p;
}

namespace
{

json_t* legacy_params_to_json(const LOADED_MODULE* mod)
{
    json_t* params = json_array();

    for (int i = 0; mod->info->parameters[i].name; i++)
    {
        const auto& p = mod->info->parameters[i];

        if (p.type != MXS_MODULE_PARAM_DEPRECATED && (p.options & MXS_MODULE_OPT_DEPRECATED) == 0)
        {
            json_array_append_new(params, module_param_to_json(p));
        }
    }

    const MXS_MODULE_PARAM* extra = nullptr;
    std::set<std::string> ignored;

    switch (mod->info->modapi)
    {
    case ModuleType::FILTER:
    case ModuleType::AUTHENTICATOR:
    case ModuleType::QUERY_CLASSIFIER:
        break;

    case ModuleType::PROTOCOL:
        extra = common_listener_params();
        ignored = {CN_SERVICE, CN_TYPE, CN_MODULE};
        break;

    case ModuleType::ROUTER:
        extra = common_service_params();
        ignored = {CN_SERVERS, CN_TARGETS, CN_ROUTER, CN_TYPE, CN_CLUSTER, CN_FILTERS};
        break;

    case ModuleType::MONITOR:
        extra = common_monitor_params();
        ignored = {CN_SERVERS, CN_TYPE, CN_MODULE};
        break;

    default:
        mxb_assert(!true);      // Module type should never be unknown
    }

    if (extra)
    {
        for (int i = 0; extra[i].name; i++)
        {
            if (ignored.count(extra[i].name) == 0)
            {
                json_array_append_new(params, module_param_to_json(extra[i]));
            }
        }
    }

    return params;
}
}

static json_t* module_json_data(const LOADED_MODULE* mod, const char* host)
{
    json_t* obj = json_object();
    auto mod_info = mod->info;
    auto module_name = mod_info->name;
    json_object_set_new(obj, CN_ID, json_string(module_name));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULES));

    json_t* attr = json_object();
    auto mod_type = module_type_to_string(mod_info->modapi);
    json_object_set_new(attr, "module_type", json_string(mod_type));
    json_object_set_new(attr, "version", json_string(mod_info->version));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(mod_info->description));
    json_object_set_new(attr, "api", json_string(module_type_to_string(mod_info->modapi)));
    json_object_set_new(attr, "maturity", json_string(module_maturity_to_string(mod_info->status)));

    json_t* commands = json_array();
    cb_param p = {commands, module_name, host};
    modulecmd_foreach(module_name, NULL, modulecmd_cb, &p);

    json_t* params = nullptr;

    if (mod_info->specification)
    {
        params = mod_info->specification->to_json();
    }
    else
    {
        params = legacy_params_to_json(mod);
    }

    json_object_set_new(attr, "commands", commands);
    json_object_set_new(attr, CN_PARAMETERS, params);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, module_name));

    return obj;
}

json_t* module_to_json(const MXS_MODULE* module, const char* host)
{
    json_t* data = NULL;

    for (auto& elem : loaded_modules)
    {
        auto ptr = elem.second;
        if (ptr->info == module)
        {
            data = module_json_data(ptr, host);
            break;
        }
    }

    // This should always be non-NULL
    mxb_assert(data);

    return mxs_json_resource(host, MXS_JSON_API_MODULES, data);
}

json_t* spec_module_json_data(const char* host, const mxs::config::Specification& spec)
{
    json_t* commands = json_array();
    // TODO: The following data will now be somewhat different compared to
    // TODO: what the modules that do not use the new configuration mechanism
    // TODO: return.
    json_t* params = spec.to_json();

    json_t* attr = json_object();
    json_object_set_new(attr, "module_type", json_string(spec.module().c_str()));
    json_object_set_new(attr, "version", json_string(MAXSCALE_VERSION));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(spec.module().c_str()));
    json_object_set_new(attr, "maturity", json_string("GA"));
    json_object_set_new(attr, "commands", commands);
    json_object_set_new(attr, CN_PARAMETERS, params);

    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(spec.module().c_str()));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULES));
    json_object_set_new(obj, CN_ATTRIBUTES, attr);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, spec.module().c_str()));

    return obj;
}

json_t* spec_module_to_json(const char* host, const mxs::config::Specification& spec)
{
    json_t* data = spec_module_json_data(host, spec);

    return mxs_json_resource(host, MXS_JSON_API_MODULES, data);
}

json_t* module_list_to_json(const char* host)
{
    json_t* arr = json_array();

    json_array_append_new(arr, spec_module_json_data(host, mxs::Config::get().specification()));
    json_array_append_new(arr, spec_module_json_data(host, Server::specification()));

    for (auto& elem : loaded_modules)
    {
        auto ptr = elem.second;
        if (ptr->info->specification)
        {
            json_array_append_new(arr, spec_module_json_data(host, *ptr->info->specification));
        }
        else
        {
            json_array_append_new(arr, module_json_data(ptr, host));
        }
    }
    return mxs_json_resource(host, MXS_JSON_API_MODULES, arr);
}

const MXS_MODULE* get_module(const std::string& name, mxs::ModuleType type)
{
    MXS_MODULE* rval = nullptr;
    string eff_name = module_get_effective_name(name);
    LOADED_MODULE* module = find_module(eff_name);
    if (module)
    {
        // If the module is already loaded, then it has been validated during loading. Only type needs to
        // be checked.
        auto mod_info = module->info;
        if (type == mxs::ModuleType::UNKNOWN || mod_info->modapi == type)
        {
            rval = mod_info;
        }
        else
        {
            auto expected_type_str = module_type_to_string(type);
            auto found_type_str = module_type_to_string(mod_info->modapi);
            MXS_ERROR(wrong_mod_type, name.c_str(), found_type_str, expected_type_str);
        }
    }
    // No such module loaded, try to load.
    else if (load_module(eff_name.c_str(), type))
    {
        module = find_module(eff_name);
        rval = module->info;
    }
    return rval;
}

string module_get_effective_name(const string& name)
{
    string eff_name = mxb::tolower(name);
    for (auto& nm : name_mappings)
    {
        if (eff_name == nm.from)
        {
            if (!nm.warned)
            {
                MXS_WARNING("%s module '%s' has been deprecated, use '%s' instead.",
                            module_type_to_string(nm.type), nm.from, nm.to);
                nm.warned = true;
            }
            eff_name = nm.to;
            break;
        }
    }
    return eff_name;
}

namespace
{
enum class InitType
{
    PROCESS,
    THREAD
};
bool call_init_funcs(InitType init_type)
{
    LOADED_MODULE* failed_init_module = nullptr;
    for (auto& elem : loaded_modules)
    {
        auto mod_info = elem.second->info;
        int rc = 0;
        auto init_func = (init_type == InitType::PROCESS) ? mod_info->process_init : mod_info->thread_init;
        if (init_func)
        {
            rc = init_func();
        }
        if (rc != 0)
        {
            failed_init_module = elem.second;
            break;
        }
    }

    bool initialized = false;
    if (failed_init_module)
    {
        // Init failed for a module. Call finish on so-far initialized modules.
        for (auto& elem : loaded_modules)
        {
            auto mod_info = elem.second->info;
            auto finish_func = (init_type == InitType::PROCESS) ? mod_info->process_finish :
                mod_info->thread_finish;
            if (finish_func)
            {
                finish_func();
            }
            if (elem.second == failed_init_module)
            {
                break;
            }
        }
    }
    else
    {
        initialized = true;
    }

    return initialized;
}

void call_finish_funcs(InitType init_type)
{
    for (auto& elem : loaded_modules)
    {
        auto mod_info = elem.second->info;
        auto finish_func = (init_type == InitType::PROCESS) ? mod_info->process_finish :
            mod_info->thread_finish;
        if (finish_func)
        {
            finish_func();
        }
    }
}

const char* module_type_to_string(ModuleType type)
{
    switch (type)
    {
    case ModuleType::PROTOCOL:
        return "protocol";

    case ModuleType::ROUTER:
        return "router";

    case ModuleType::MONITOR:
        return "monitor";

    case ModuleType::FILTER:
        return "filter";

    case ModuleType::AUTHENTICATOR:
        return "authenticator";

    case ModuleType::QUERY_CLASSIFIER:
        return "query_classifier";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}

const char* module_maturity_to_string(ModuleStatus type)
{
    switch (type)
    {
    case ModuleStatus::IN_DEVELOPMENT:
        return "In development";

    case ModuleStatus::ALPHA:
        return "Alpha";

    case ModuleStatus::BETA:
        return "Beta";

    case ModuleStatus::GA:
        return "GA";

    case ModuleStatus::EXPERIMENTAL:
        return "Experimental";

    default:
        mxb_assert(!true);
        return "Unknown";
    }
}

const char* mxs_module_param_type_to_string(mxs_module_param_type type)
{
    switch (type)
    {
    case MXS_MODULE_PARAM_COUNT:
        return "count";

    case MXS_MODULE_PARAM_INT:
        return "int";

    case MXS_MODULE_PARAM_SIZE:
        return "size";

    case MXS_MODULE_PARAM_BOOL:
        return "bool";

    case MXS_MODULE_PARAM_STRING:
        return "string";

    case MXS_MODULE_PARAM_QUOTEDSTRING:
        return "quoted string";

    case MXS_MODULE_PARAM_PASSWORD:
        return "password string";

    case MXS_MODULE_PARAM_ENUM:
        return "enum";

    case MXS_MODULE_PARAM_PATH:
        return "path";

    case MXS_MODULE_PARAM_SERVICE:
        return "service";

    case MXS_MODULE_PARAM_SERVER:
        return "server";

    case MXS_MODULE_PARAM_TARGET:
        return "target";

    case MXS_MODULE_PARAM_SERVERLIST:
        return "serverlist";

    case MXS_MODULE_PARAM_TARGETLIST:
        return "list of targets";

    case MXS_MODULE_PARAM_REGEX:
        return "regular expression";

    case MXS_MODULE_PARAM_DURATION:
        return "duration";

    default:
        mxb_assert(!true);
        return "unknown";
    }
}
}

ModuleType module_type_from_string(const string& type_str)
{
    auto rval = ModuleType::UNKNOWN;
    if (type_str == "protocol")
    {
        rval = ModuleType::PROTOCOL;
    }
    else if (type_str == "router")
    {
        rval = ModuleType::ROUTER;
    }
    else if (type_str == "monitor")
    {
        rval = ModuleType::ROUTER;
    }
    else if (type_str == "filter")
    {
        rval = ModuleType::ROUTER;
    }
    else if (type_str == "authenticator")
    {
        rval = ModuleType::ROUTER;
    }
    else if (type_str == "query_classifier")
    {
        rval = ModuleType::ROUTER;
    }
    return rval;
}

bool modules_thread_init()
{
    return call_init_funcs(InitType::THREAD);
}

void modules_thread_finish()
{
    call_finish_funcs(InitType::THREAD);
}

bool modules_process_init()
{
    return call_init_funcs(InitType::PROCESS);
}

void modules_process_finish()
{
    call_finish_funcs(InitType::PROCESS);
}
