/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/modulecmd.hh>

#include <string>

#include <maxbase/alloc.hh>
#include <maxbase/json.hh>
#include <maxbase/string.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/json_api.hh>
#include <maxscale/pcre2.hh>

#include "internal/filter.hh"
#include "internal/modules.hh"
#include "internal/monitormanager.hh"
#include "internal/servermanager.hh"
#include "internal/session.hh"

namespace
{
const char CN_ARG_MAX[] = "arg_max";
const char CN_ARG_MIN[] = "arg_min";
const char CN_METHOD[] = "method";
const char CN_MODULE_COMMAND[] = "module_command";

using maxscale::Monitor;

/**
 * A registered domain. Contains registered commands, mapped by command name.
 */
using MODULECMD_DOMAIN = std::map<std::string, ModuleCmd>;

/**
 * Internal functions
 */

/** The global list of registered domains */
struct ThisUnit
{
    std::map<std::string, MODULECMD_DOMAIN> domains;    /**< Map from command domain name -> domain */
    std::mutex                              lock;
};

static ThisUnit this_unit;

static ModuleCmd command_create(const char* domain,
                                mxs::modulecmd::CmdType type,
                                ModuleCmdFn entry_point,
                                std::vector<ModuleCmdArgDesc> args,
                                std::string_view description)
{
    mxb_assert(!description.empty());
    ModuleCmd rval;

    int argc_min = 0;
    for (const auto& arg : args)
    {
        if (arg.is_required())
        {
            argc_min++;
        }
    }

    rval.type = type;
    rval.func = entry_point;
    rval.domain = domain;
    rval.description = description;
    rval.arg_count_min = argc_min;
    rval.arg_count_max = args.size();
    rval.arg_types = std::move(args);

    return rval;
}

static bool process_argument(const ModuleCmd* cmd,
                             const ModuleCmdArgDesc& type,
                             const std::string& value,
                             ModuleCmdArg* arg,
                             std::string& err)
{
    using namespace mxs::modulecmd;
    auto allow_name_mismatch = [](const ModuleCmdArgDesc& t) {
        return (t.options & ARG_NAME_MATCHES_DOMAIN) == 0;
    };

    bool rval = false;

    if (!type.is_required() && value.empty())
    {
        arg->type = ArgType::NONE;
        rval = true;
    }
    else if (!value.empty())
    {
        switch (type.type)
        {
        case ArgType::NONE:
            rval = true;
            break;

        case ArgType::STRING:
            arg->string = value;
            rval = true;
            break;

        case ArgType::BOOLEAN:
            {
                int truthval = config_truth_value(value);
                if (truthval != -1)
                {
                    arg->boolean = truthval;
                    rval = true;
                }
                else
                {
                    err = "not a boolean value";
                }
            }
            break;

        case ArgType::SERVICE:
            if ((arg->service = Service::find(value)))
            {
                if (allow_name_mismatch(type)
                    || strcmp(cmd->domain.c_str(), arg->service->router_name()) == 0)
                {
                    rval = true;
                }
                else
                {
                    err = "router and domain names don't match";
                }
            }
            else
            {
                err = "service not found";
            }
            break;

        case ArgType::SERVER:
            if ((arg->server = ServerManager::find_by_unique_name(value)))
            {
                if (allow_name_mismatch(type))
                {
                    rval = true;
                }
                else
                {
                    err = "server and domain names don't match";
                }
            }
            else
            {
                err = "server not found";
            }
            break;

        case ArgType::MONITOR:
            if ((arg->monitor = MonitorManager::find_monitor(value.c_str())))
            {
                std::string eff_name = module_get_effective_name(arg->monitor->m_module);
                if (allow_name_mismatch(type)
                    || strcasecmp(cmd->domain.c_str(), eff_name.c_str()) == 0)
                {
                    rval = true;
                }
                else
                {
                    err = "monitor and domain names don't match";
                }
            }
            else
            {
                err = "monitor not found";
            }
            break;

        case ArgType::FILTER:
            if (auto f = filter_find(value))
            {
                arg->filter = f.get();
                const char* orig_name = f->module();
                std::string eff_name = module_get_effective_name(orig_name);
                if (allow_name_mismatch(type)
                    || strcasecmp(cmd->domain.c_str(), eff_name.c_str()) == 0)
                {
                    rval = true;
                }
                else
                {
                    err = "filter and domain names don't match";
                }
            }
            else
            {
                err = "filter not found";
            }
            break;
        }

        if (rval)
        {
            arg->type = type.type;
        }
    }
    else
    {
        err = "required argument";
    }

    return rval;
}
}

/**
 * Public functions declared in modulecmd.h
 */

bool modulecmd_register_command(const char* domain,
                                const char* identifier,
                                mxs::modulecmd::CmdType type,
                                ModuleCmdFn entry_point,
                                std::vector<ModuleCmdArgDesc> args,
                                std::string_view description)
{
    std::string domain_lower = mxb::tolower(domain);
    bool rval;
    {
        std::lock_guard guard(this_unit.lock);
        MODULECMD_DOMAIN* dm;
        auto domain_it = this_unit.domains.find(domain_lower);
        if (domain_it == this_unit.domains.end())
        {
            dm = &this_unit.domains.emplace(std::move(domain_lower), MODULECMD_DOMAIN()).first->second;
        }
        else
        {
            dm = &domain_it->second;
        }

        rval = dm->emplace(mxb::tolower(identifier),
                           command_create(domain, type, entry_point, std::move(args), description)).second;
    }

    if (!rval)
    {
        MXB_ERROR("Command registered more than once: %s::%s", domain, identifier);
    }
    return rval;
}

const ModuleCmd* modulecmd_find_command(const char* domain, const char* identifier)
{
    std::string effective_domain = module_get_effective_name(domain);

    const ModuleCmd* rval = nullptr;
    std::lock_guard guard(this_unit.lock);

    auto domain_it = this_unit.domains.find(effective_domain);
    if (domain_it != this_unit.domains.end())
    {
        std::string id_lower = mxb::tolower(identifier);
        auto& dm = domain_it->second;
        auto cmd_it = dm.find(id_lower);
        if (cmd_it != dm.end())
        {
            rval = &cmd_it->second;
        }
    }

    if (rval == NULL)
    {
        MXB_ERROR("Command not found: %s::%s", domain, identifier);
    }

    return rval;
}

std::optional<ModuleCmdArgs> modulecmd_arg_parse(const ModuleCmd* cmd, const mxs::KeyValueVector& argv)
{
    std::optional<ModuleCmdArgs> rval;
    int argc = argv.size();
    if (argc >= cmd->arg_count_min && argc <= cmd->arg_count_max)
    {
        ModuleCmdArgs arg;
        arg.resize(cmd->arg_count_max);
        bool error = false;

        for (int i = 0; i < cmd->arg_count_max && i < argc; i++)
        {
            std::string err;
            // Use the key as the argument value, as this command type does not support key-value pairs.
            const std::string& arg_value = argv[i].first;
            if (!process_argument(cmd, cmd->arg_types[i], arg_value, &arg[i], err))
            {
                error = true;
                MXB_ERROR("Argument %d, %s: %s", i + 1, err.c_str(),
                          !arg_value.empty() ? arg_value.c_str() : "No argument given");
                break;
            }
        }

        if (!error)
        {
            arg.resize(argc);
            rval = std::move(arg);
        }
    }
    else
    {
        if (cmd->arg_count_min == cmd->arg_count_max)
        {
            MXB_ERROR("Expected %d arguments, got %d.", cmd->arg_count_min, argc);
        }
        else
        {
            MXB_ERROR("Expected between %d and %d arguments, got %d.",
                      cmd->arg_count_min, cmd->arg_count_max, argc);
        }
    }

    return rval;
}

bool modulecmd_call_command(const ModuleCmd* cmd, const ModuleCmdArgs& args, json_t** output)
{
    mxb_assert(cmd->arg_count_min == 0 || !args.empty());
    json_t* discard = NULL;
    bool rval = cmd->func(args, output ? output : &discard);
    json_decref(discard);
    return rval;
}

static std::string modulecmd_argtype_to_str(const ModuleCmdArgDesc& type)
{
    auto format_type = [&type](std::string_view str) -> std::string {
        std::string rval;
        if (type.is_required())
        {
            rval = str;
        }
        else
        {
            rval.append("[").append(str).append("]");
        }
        return rval;
    };

    using mxs::modulecmd::ArgType;

    std::string rval = "UNKNOWN";
    switch (type.type)
    {
    case ArgType::NONE:
        rval = format_type("NONE");
        break;

    case ArgType::STRING:
        rval = format_type("STRING");
        break;

    case ArgType::BOOLEAN:
        rval = format_type("BOOLEAN");
        break;

    case ArgType::SERVICE:
        rval = format_type("SERVICE");
        break;

    case ArgType::SERVER:
        rval = format_type("SERVER");
        break;

    case ArgType::MONITOR:
        rval = format_type("MONITOR");
        break;

    case ArgType::FILTER:
        rval = format_type("FILTER");
        break;
    }
    return rval;
}

mxb::Json to_json(const std::string& cmd_name, const ModuleCmd& cmd, const char* host)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(cmd_name.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE_COMMAND));

    json_t* attr = json_object();
    const char* method = cmd.type == mxs::modulecmd::CmdType::WRITE ? "POST" : "GET";
    json_object_set_new(attr, CN_METHOD, json_string(method));
    json_object_set_new(attr, CN_ARG_MIN, json_integer(cmd.arg_count_min));
    json_object_set_new(attr, CN_ARG_MAX, json_integer(cmd.arg_count_max));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(cmd.description.c_str()));

    json_t* param = json_array();

    for (const auto& arg_info : cmd.arg_types)
    {
        json_t* p = json_object();
        json_object_set_new(p, CN_DESCRIPTION, json_string(arg_info.description.c_str()));
        json_object_set_new(p, CN_TYPE, json_string(modulecmd_argtype_to_str(arg_info).c_str()));
        json_object_set_new(p, CN_REQUIRED, json_boolean(arg_info.is_required()));
        json_array_append_new(param, p);
    }

    std::string self = mxb::cat(cmd.domain, "/", cmd_name);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, self.c_str()));
    json_object_set_new(attr, CN_PARAMETERS, param);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);

    return mxb::Json(obj, mxb::Json::RefType::STEAL);
}

json_t* modulecmd_to_json(std::string_view domain, const char* host)
{
    mxb::Json rval(mxb::Json::Type::ARRAY);
    std::string domain_lower = mxb::tolower(domain);

    std::lock_guard guard(this_unit.lock);

    auto it = this_unit.domains.find(domain_lower);
    if (it != this_unit.domains.end())
    {
        const auto& cmd_map = it->second;
        for (const auto& map_elem : cmd_map)
        {
            rval.add_array_elem(to_json(map_elem.first, map_elem.second, host));
        }
    }
    return rval.release();
}

bool ModuleCmdArgDesc::is_required() const
{
    return (options & mxs::modulecmd::ARG_OPTIONAL) == 0;
}

ModuleCmdArgDesc::ModuleCmdArgDesc(mxs::modulecmd::ArgType type, std::string desc)
    : ModuleCmdArgDesc(type, 0, std::move(desc))
{
}

ModuleCmdArgDesc::ModuleCmdArgDesc(mxs::modulecmd::ArgType type, uint8_t opts, std::string desc)
    : type(type)
    , options(opts)
    , description(std::move(desc))
{
}
