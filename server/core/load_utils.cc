/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
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

#include <maxbase/alloc.h>
#include <maxbase/format.hh>
#include <maxscale/authenticator.hh>
#include <maxscale/filter.hh>
#include <maxscale/json_api.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/monitor.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/router.hh>
#include <maxscale/routingworker.hh>
#include <maxscale/version.h>

#include "internal/config.hh"
#include "internal/listener.hh"
#include "internal/modules.hh"
#include "internal/monitor.hh"
#include "internal/server.hh"
#include "internal/service.hh"

using std::string;
using std::unique_ptr;
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
    MXS_MODULE* info{nullptr};  /**< The module information */
    void*       handle{nullptr};/**< The handle returned by dlopen */
    std::string filepath;       /**< Path to file */

    LOADED_MODULE(void* dlhandle, MXS_MODULE* info, const string& filepath)
        : info(info)
        , handle(dlhandle)
        , filepath(filepath)
    {
    }

    ~LOADED_MODULE()
    {
        // Built-in modules cannot be closed.
        if (handle)
        {
            dlclose(handle);
        }
    }
};

struct ThisUnit
{
    /**
     * Module name to module mapping. Stored alphabetically, names in lowercase. Only accessed from the main
     * thread. */
    std::map<string, unique_ptr<LOADED_MODULE>> loaded_modules;

    /**
     * List of module filepaths already loaded. When loading a library through a link, the target filename
     * should be added to this list.
     */
    std::set<string> loaded_filepaths;

    bool load_all_ok {false};
};
ThisUnit this_unit;

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

enum class LoadResult
{
    OK,
    ERR,
    NOT_A_MODULE,
};

struct LoadAttempt
{
    LoadResult                result;
    std::string               error;
    unique_ptr<LOADED_MODULE> module;
};

LoadAttempt load_module(const string& fpath, mxs::ModuleType type, const string& given_name = "");
bool        run_module_thread_init(MXS_MODULE* mod_info);

const char madbproto[] = "mariadbprotocol";
NAME_MAPPING name_mappings[] =
{
    {ModuleType::MONITOR,       "clustrixmon",   "xpandmon",    false},
    {ModuleType::MONITOR,       "mysqlmon",      "mariadbmon",  false},
    {ModuleType::PROTOCOL,      "mysqlclient",   madbproto,     false},
    {ModuleType::PROTOCOL,      "mariadb",       madbproto,     true },
    {ModuleType::PROTOCOL,      "mariadbclient", madbproto,     true },
    {ModuleType::AUTHENTICATOR, "mysqlauth",     "mariadbauth", false},
};
}

static bool api_version_match(const MXS_MODULE* mod_info, const string& filepath)
{
    MXS_MODULE_VERSION required;
    switch (mod_info->modapi)
    {
    case ModuleType::PROTOCOL:
        required = MXS_PROTOCOL_VERSION;
        break;

    case ModuleType::AUTHENTICATOR:
        required = MXS_AUTHENTICATOR_VERSION;
        break;

    case ModuleType::ROUTER:
        required = MXS_ROUTER_VERSION;
        break;

    case ModuleType::MONITOR:
        required = MXS_MONITOR_VERSION;
        break;

    case ModuleType::FILTER:
        required = MXS_FILTER_VERSION;
        break;

    case ModuleType::QUERY_CLASSIFIER:
        required = MXS_QUERY_CLASSIFIER_VERSION;
        break;

    default:
        MXS_ERROR("Unknown module type %i for module '%s' from '%s'.",
                  (int)mod_info->modapi, mod_info->name, filepath.c_str());
        return false;
        break;
    }

    bool rval = false;
    if (required == mod_info->api_version)
    {
        rval = true;
    }
    else
    {
        string api_type = module_type_to_string(mod_info->modapi);
        MXS_ERROR("Module '%s' from '%s' implements wrong version of %s API. "
                  "Need version %d.%d.%d, found %d.%d.%d",
                  mod_info->name, filepath.c_str(), api_type.c_str(),
                  required.major, required.minor, required.patch,
                  mod_info->api_version.major, mod_info->api_version.minor, mod_info->api_version.patch);
    }
    return rval;
}

namespace
{

bool check_module(const MXS_MODULE* mod_info, const string& filepath, ModuleType expected_type)
{
    auto filepathc = filepath.c_str();
    // Check the first field of the module-struct to see if the struct is valid for this
    // MaxScale version.
    auto obj_version = mod_info->mxs_version;
    if (obj_version != mxs::MODULE_INFO_VERSION)
    {
        MXB_ERROR("Module from '%s' is a for a different version of MaxScale and cannot be loaded.",
                  filepathc);
        return false;
    }
    auto namec = mod_info->name;
    bool success = true;
    if (expected_type != ModuleType::UNKNOWN)
    {
        auto found_type = mod_info->modapi;
        if (found_type != expected_type)
        {
            auto expected_type_str = module_type_to_string(expected_type);
            auto found_type_str = module_type_to_string(found_type);
            MXB_ERROR("Module '%s' from '%s' is a %s, not a %s.",
                      namec, filepathc, found_type_str, expected_type_str);
            success = false;
        }
    }

    if (!api_version_match(mod_info, filepath))
    {
        success = false;
    }

    if (mod_info->version == nullptr)
    {
        MXS_ERROR("Module '%s' from '%s' does not define a version string.", namec, filepathc);
        success = false;
    }

    if (mod_info->module_object == nullptr)
    {
        MXS_ERROR("Module '%s' from '%s' does not define any API functions.", namec, filepathc);
        success = false;
    }

    return success;
}

int load_module_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    if (typeflag == FTW_F && this_unit.loaded_filepaths.count(fpath) == 0)
    {
        // Check that the path looks like an .so-file. Also, avoid loading the main library.
        auto last_part_ptr = strrchr(fpath, '/');
        if (last_part_ptr)
        {
            string last_part = (last_part_ptr + 1);
            if (last_part.find("lib") == 0 && last_part.find(".so") != string::npos
                && last_part.find("libmaxscale-common.so") == string::npos)
            {
                auto res = load_module(fpath, ModuleType::UNKNOWN);

                if (res.result == LoadResult::ERR)
                {
                    MXS_ERROR("%s", res.error.c_str());
                    this_unit.load_all_ok = false;
                }
            }
        }
    }
    return 0;
}

LoadAttempt load_module_file(const string& filepath, ModuleType type, const string& given_name = "")
{
    LoadAttempt res;
    res.result = LoadResult::ERR;

    // Search for the so-file
    auto fnamec = filepath.c_str();
    if (access(fnamec, F_OK) != 0)
    {
        int eno = errno;
        res.error = mxb::string_printf("Cannot access library file '%s'. Error %i: %s",
                                       fnamec, eno, mxb_strerror(eno));
    }
    else
    {
        void* dlhandle = dlopen(fnamec, RTLD_NOW | RTLD_LOCAL);
        if (!dlhandle)
        {
            res.error = mxb::string_printf("Cannot load library file '%s'. %s.", fnamec, dlerror());
        }
        else
        {
            Dl_info info;
            void* sym = dlsym(dlhandle, MXS_MODULE_SYMBOL_NAME);

            if (!sym)
            {
                res.result = LoadResult::NOT_A_MODULE;
                res.error = mxb::string_printf("Library file '%s' does not contain the entry point "
                                               "function. %s.", fnamec, dlerror());
                dlclose(dlhandle);
            }
            else if (dladdr(sym, &info) == 0)
            {
                res.result = LoadResult::NOT_A_MODULE;
                res.error = mxb::string_printf("Failed to get module entry point for '%s'.", fnamec);
                dlclose(dlhandle);
            }
            else
            {
                // Sometimes the path returned in dli_fname seems to point at the symbolic link instead of the
                // file that it points to. Comparing the concrete files instead of the links should be more
                // stable.
                char file_path[PATH_MAX] = "";
                char symbol_path[PATH_MAX] = "";
                realpath(filepath.c_str(), file_path);
                realpath(info.dli_fname, symbol_path);

                if (strcmp(file_path, symbol_path) != 0)
                {
                    res.result = LoadResult::NOT_A_MODULE;
                    res.error = mxb::string_printf(
                        "Not a MaxScale module (defined in '%s', module is '%s'): %s",
                        symbol_path, file_path, fnamec);
                    dlclose(dlhandle);
                }
                else
                {
                    // Module was loaded, check that it's valid.
                    auto entry_point = (void* (*)())sym;
                    auto mod_info = (MXS_MODULE*)entry_point();
                    if (!check_module(mod_info, filepath, type))
                    {
                        dlclose(dlhandle);
                    }
                    else
                    {
                        // The path may be a link, get the true filepath. Not essential, but is used to avoid
                        // loading already loaded files.
                        char buf[PATH_MAX];
                        auto real_filepath = realpath(fnamec, buf);
                        res.module = std::make_unique<LOADED_MODULE>(dlhandle, mod_info,
                                                                     real_filepath ? real_filepath : "");
                        res.result = LoadResult::OK;
                    }
                }
            }
        }
    }

    return res;
}

/**
 *@brief Load a module
 *
 * @param fname Filepath to load from
 * @param name Name of the module to load, as given by user
 * @param type Type of module
 * @return The module specific entry point structure or NULL
 */
LoadAttempt load_module(const string& fname, mxs::ModuleType type, const string& name)
{
    auto res = load_module_file(fname, type, name);
    if (res.result == LoadResult::OK)
    {
        auto mod_info = res.module->info;
        auto mod_name_low = mxb::tolower(mod_info->name);
        // The same module may be already loaded from a symbolic link. This only
        // happens when called from load_all_modules().
        if (this_unit.loaded_modules.count(mod_name_low) == 0)
        {
            auto process_init_func = mod_info->process_init;
            bool process_init_ok = !process_init_func || process_init_func() == 0;

            bool thread_init_ok = false;
            if (process_init_ok)
            {
                thread_init_ok = run_module_thread_init(mod_info);
                if (!thread_init_ok && mod_info->process_finish)
                {
                    mod_info->process_finish();
                }
            }

            if (process_init_ok && thread_init_ok)
            {
                auto new_kv = std::make_pair(mod_name_low, std::move(res.module));
                this_unit.loaded_filepaths.insert(new_kv.second->filepath);
                this_unit.loaded_modules.insert(std::move(new_kv));
                MXS_NOTICE("Module '%s' loaded from '%s'.", mod_info->name, fname.c_str());
            }
            else
            {
                res.result = LoadResult::ERR;
                res.error = "Module initialization failed";
            }
        }
    }

    return res;
}
}

bool load_all_modules()
{
    this_unit.load_all_ok = true;
    int rv = nftw(mxs::libdir(), load_module_cb, 10, FTW_PHYS);
    return this_unit.load_all_ok;
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
    auto iter = this_unit.loaded_modules.find(name);
    if (iter != this_unit.loaded_modules.end())
    {
        rval = iter->second.get();
    }
    return rval;
}
}

void unload_all_modules()
{
    // This is only ran when exiting, at which point threads have stopped and ran their own finish functions.
    modules_process_finish();
    this_unit.loaded_modules.clear();
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
    case ModuleType::PROTOCOL:
    case ModuleType::ROUTER:
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

const char* module_type_to_legacy_string(ModuleType type)
{
    // NOTE: The names are CamelCase on purpose to be backwards compatible with 2.5. This function should only
    // be used to generate the module_type field of the modules endpoint response.
    switch (type)
    {
    case ModuleType::PROTOCOL:
        return "Protocol";

    case ModuleType::ROUTER:
        return "Router";

    case ModuleType::MONITOR:
        return "Monitor";

    case ModuleType::FILTER:
        return "Filter";

    case ModuleType::AUTHENTICATOR:
        return "Authenticator";

    case ModuleType::QUERY_CLASSIFIER:
        return "QueryClassifier";

    default:
        mxb_assert(!true);
        return "unknown";
    }
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
    auto mod_type = module_type_to_legacy_string(mod_info->modapi);
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

    json_t* core_params = nullptr;

    if (mod_info->modapi == mxs::ModuleType::ROUTER)
    {
        core_params = Service::specification()->to_json();
    }
    else if (mod_info->modapi == mxs::ModuleType::PROTOCOL)
    {
        core_params = Listener::specification()->to_json();
    }
    else if (mod_info->modapi == mxs::ModuleType::FILTER)
    {
        core_params = FilterDef::specification()->to_json();
    }
    else if (mod_info->modapi == mxs::ModuleType::MONITOR)
    {
        // TODO: Use new config params in monitors
    }

    if (core_params)
    {
        json_object_update(params, core_params);
        json_decref(core_params);
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

    for (auto& elem : this_unit.loaded_modules)
    {
        auto ptr = elem.second.get();
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
    mxb_assert((spec.kind() == mxs::config::Specification::Kind::GLOBAL && spec.module() == "maxscale")
               || (spec.kind() == mxs::config::Specification::Kind::SERVER && spec.module() == "servers"));

    json_t* commands = json_array();
    // TODO: The following data will now be somewhat different compared to
    // TODO: what the modules that do not use the new configuration mechanism
    // TODO: return.
    json_t* params = spec.to_json();

    json_t* attr = json_object();
    json_object_set_new(attr, "module_type", json_string(spec.module().c_str()));
    json_object_set_new(attr, "version", json_string(MAXSCALE_VERSION));
    // TODO: The description could be something other than than "maxscale" or "servers"
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

    for (auto& elem : this_unit.loaded_modules)
    {
        json_array_append_new(arr, module_json_data(elem.second.get(), host));
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
    else
    {
        // No such module loaded, try to load.
        string fname = mxb::string_printf("%s/lib%s.so", mxs::libdir(), eff_name.c_str());
        auto res = load_module(fname, type, name);

        if (res.result == LoadResult::OK)
        {
            if ((module = find_module(eff_name)))
            {
                rval = module->info;
            }
            else
            {
                MXS_ERROR("Module '%s' was not found after being loaded successfully: "
                          "library name and module name are different.", fname.c_str());
            }
        }
        else
        {
            MXS_ERROR("%s", res.error.c_str());
        }
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
    for (auto& elem : this_unit.loaded_modules)
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
            failed_init_module = elem.second.get();
            break;
        }
    }

    bool initialized = false;
    if (failed_init_module)
    {
        // Init failed for a module. Call finish on so-far initialized modules.
        for (auto& elem : this_unit.loaded_modules)
        {
            auto mod_info = elem.second->info;
            auto finish_func = (init_type == InitType::PROCESS) ? mod_info->process_finish :
                mod_info->thread_finish;
            if (finish_func)
            {
                finish_func();
            }
            if (elem.second.get() == failed_init_module)
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
    for (auto& elem : this_unit.loaded_modules)
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

bool run_module_thread_init(MXS_MODULE* mod_info)
{
    std::atomic_bool thread_init_ok {true};
    auto thread_init_func = mod_info->thread_init;
    if (thread_init_func)
    {
        auto exec_auto = mxs::RoutingWorker::EXECUTE_AUTO;

        auto main_worker = mxs::MainWorker::get();
        if (main_worker)
        {
            auto mw_state = main_worker->state();
            if (mw_state == mxb::Worker::state_t::POLLING
                || mw_state == mxb::Worker::state_t::PROCESSING)
            {
                auto run_thread_init = [&thread_init_ok, thread_init_func]() {
                        if (thread_init_func() != 0)
                        {
                            thread_init_ok = false;
                        }
                    };
                main_worker->call(run_thread_init, exec_auto);
            }
        }

        if (thread_init_ok)
        {
            std::mutex lock;
            std::vector<mxb::Worker*> succeeded_workers;

            if (mxs::RoutingWorker::is_running())
            {
                auto run_thread_init = [&thread_init_ok, &lock, &succeeded_workers, thread_init_func]() {
                        if (thread_init_func() == 0)
                        {
                            std::lock_guard<std::mutex> guard(lock);
                            succeeded_workers.push_back(mxb::Worker::get_current());
                        }
                        else
                        {
                            thread_init_ok = false;
                        }
                    };

                mxb::Semaphore sem;
                auto n = mxs::RoutingWorker::broadcast(run_thread_init, &sem, exec_auto);
                sem.wait_n(n);
            }

            if (!thread_init_ok)
            {
                auto thread_finish_func = mod_info->thread_finish;
                if (thread_finish_func)
                {
                    mxb::Semaphore sem;
                    for (auto worker : succeeded_workers)
                    {
                        worker->execute(thread_finish_func, &sem, exec_auto);
                    }
                    sem.wait_n(succeeded_workers.size());

                    if (main_worker)
                    {
                        main_worker->call(thread_finish_func, exec_auto);
                    }
                }
            }
        }
    }
    return thread_init_ok;
}
}

bool MXS_MODULE_VERSION::operator==(const MXS_MODULE_VERSION& rhs) const
{
    return major == rhs.major && minor == rhs.minor && patch == rhs.patch;
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

void add_built_in_module(MXS_MODULE* module)
{
    auto mod_name_low = mxb::tolower(module->name);
    mxb_assert(this_unit.loaded_modules.count(mod_name_low) == 0);
    auto init_func = module->process_init;
    bool init_ok = init_func ? (init_func() == 0) : true;
    mxb_assert(init_ok);

    auto new_module = std::make_unique<LOADED_MODULE>(nullptr, module, "");
    auto new_kv = std::make_pair(mod_name_low, std::move(new_module));
    this_unit.loaded_modules.insert(std::move(new_kv));
}

namespace maxscale
{

/**
 * Initialize an authenticator module. Is in public namespace as it's called from protocol code.
 *
 * @param authenticator Authenticator name
 * @param options Authenticator options
 * @return Authenticator instance or NULL on error
 */
std::unique_ptr<AuthenticatorModule>
authenticator_init(const std::string& authenticator, mxs::ConfigParameters* options)
{
    std::unique_ptr<AuthenticatorModule> rval;
    auto module_info = get_module(authenticator, ModuleType::AUTHENTICATOR);
    if (module_info)
    {
        auto func = (mxs::AUTHENTICATOR_API*)module_info->module_object;
        rval.reset(func->create(options));
    }
    return rval;
}
}

QUERY_CLASSIFIER* qc_load(const char* plugin_name)
{
    void* module = nullptr;
    auto module_info = get_module(plugin_name, mxs::ModuleType::QUERY_CLASSIFIER);
    if (module_info)
    {
        module = module_info->module_object;
    }

    if (module)
    {
        MXS_INFO("%s loaded.", plugin_name);
    }
    else
    {
        MXS_ERROR("Could not load %s.", plugin_name);
    }

    return (QUERY_CLASSIFIER*) module;
}

void qc_unload(QUERY_CLASSIFIER* classifier)
{
    // TODO: The module loading/unloading needs an overhaul before we
    // TODO: actually can unload something.
    classifier = NULL;
}
