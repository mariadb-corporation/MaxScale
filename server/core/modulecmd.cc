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
 * A registered domain
 */
typedef struct modulecmd_domain
{
    std::string            domain;      /**< The domain */
    std::vector<MODULECMD> commands;    /**< List of registered commands */
} MODULECMD_DOMAIN;

/**
 * Internal functions
 */

/** The global list of registered domains */
struct ThisUnit
{
    std::vector<MODULECMD_DOMAIN> domains;
    std::mutex                    lock;
};

static ThisUnit this_unit;

static void report_argc_mismatch(const MODULECMD* cmd, int argc)
{
    if (cmd->arg_count_min == cmd->arg_count_max)
    {
        MXB_ERROR("Expected %d arguments, got %d.", cmd->arg_count_min, argc);
    }
    else
    {
        MXB_ERROR("Expected between %d and %d arguments, got %d.",
                  cmd->arg_count_min,
                  cmd->arg_count_max,
                  argc);
    }
}

static MODULECMD_DOMAIN domain_create(const char* domain)
{
    MODULECMD_DOMAIN rval;
    rval.domain = domain;
    return rval;
}

static MODULECMD_DOMAIN& get_or_create_domain(const char* domain)
{
    for (auto& dm : this_unit.domains)
    {
        if (strcasecmp(dm.domain.c_str(), domain) == 0)
        {
            return dm;
        }
    }

    this_unit.domains.push_back(domain_create(domain));
    return this_unit.domains.back();
}

static MODULECMD command_create(const char* identifier,
                                const char* domain,
                                ModuleCmdType type,
                                MODULECMDFN entry_point,
                                std::vector<ModuleCmdArg> args,
                                std::string_view description)
{
    mxb_assert(!description.empty());
    MODULECMD rval;

    int argc_min = 0;
    for (const auto& arg : args)
    {
        if (modulecmd_arg_is_required(arg))
        {
            argc_min++;
        }
    }

    rval.type = type;
    rval.func = entry_point;
    rval.identifier = identifier;
    rval.domain = domain;
    rval.description = description;
    rval.arg_count_min = argc_min;
    rval.arg_count_max = args.size();
    rval.arg_types = std::move(args);

    return rval;
}

static bool domain_has_command(const MODULECMD_DOMAIN& dm, const char* id)
{
    for (const MODULECMD& cmd : dm.commands)
    {
        if (strcasecmp(cmd.identifier.c_str(), id) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool process_argument(const MODULECMD* cmd,
                             const ModuleCmdArg& type,
                             const std::string& value,
                             struct ModuleCmdArgValue* arg,
                             std::string& err)
{
    using namespace mxs::modulecmd;
    auto allow_name_mismatch = [](const ModuleCmdArg& t) {
        return (t.options & ARG_NAME_MATCHES_DOMAIN) == 0;
    };

    bool rval = false;

    if (!modulecmd_arg_is_required(type) && value.empty())
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
                                ModuleCmdType type,
                                MODULECMDFN entry_point,
                                std::vector<ModuleCmdArg> args,
                                std::string_view description)
{
    bool rval = false;
    std::lock_guard guard(this_unit.lock);

    MODULECMD_DOMAIN& dm = get_or_create_domain(domain);

    if (domain_has_command(dm, identifier))
    {
        MXB_ERROR("Command registered more than once: %s::%s", domain, identifier);
    }
    else
    {
        dm.commands.emplace_back(command_create(identifier, domain, type, entry_point,
                                                std::move(args), description));
        rval = true;
    }

    return rval;
}

const MODULECMD* modulecmd_find_command(const char* domain, const char* identifier)
{
    std::string effective_domain = module_get_effective_name(domain);

    const MODULECMD* rval = NULL;
    std::lock_guard guard(this_unit.lock);

    for (const MODULECMD_DOMAIN& dm : this_unit.domains)
    {
        if (strcasecmp(effective_domain.c_str(), dm.domain.c_str()) == 0)
        {
            for (const MODULECMD& cmd : dm.commands)
            {
                if (strcasecmp(cmd.identifier.c_str(), identifier) == 0)
                {
                    rval = &cmd;
                    break;
                }
            }
            break;
        }
    }

    if (rval == NULL)
    {
        MXB_ERROR("Command not found: %s::%s", domain, identifier);
    }

    return rval;
}

std::optional<MODULECMD_ARG> modulecmd_arg_parse(const MODULECMD* cmd, const mxs::KeyValueVector& argv)
{
    std::optional<MODULECMD_ARG> rval;
    int argc = argv.size();
    if (argc >= cmd->arg_count_min && argc <= cmd->arg_count_max)
    {
        MODULECMD_ARG arg;
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
        report_argc_mismatch(cmd, argc);
    }

    return rval;
}

bool modulecmd_call_command(const MODULECMD* cmd, const MODULECMD_ARG& args, json_t** output)
{
    bool rval = false;

    if (cmd->arg_count_min > 0 && args.empty())
    {
        report_argc_mismatch(cmd, 0);
    }
    else
    {
        json_t* discard = NULL;
        rval = cmd->func(args, output ? output : &discard);
        json_decref(discard);
    }

    return rval;
}

static std::string modulecmd_argtype_to_str(const ModuleCmdArg& type)
{
    auto format_type = [&type](std::string_view str) -> std::string {
        std::string rval;
        if (modulecmd_arg_is_required(type))
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

mxb::Json to_json(const MODULECMD& cmd, const char* host)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(cmd.identifier.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE_COMMAND));

    json_t* attr = json_object();
    const char* method = cmd.type == ModuleCmdType::WRITE ? "POST" : "GET";
    json_object_set_new(attr, CN_METHOD, json_string(method));
    json_object_set_new(attr, CN_ARG_MIN, json_integer(cmd.arg_count_min));
    json_object_set_new(attr, CN_ARG_MAX, json_integer(cmd.arg_count_max));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(cmd.description.c_str()));

    json_t* param = json_array();

    for (int i = 0; i < cmd.arg_count_max; i++)
    {
        json_t* p = json_object();
        json_object_set_new(p, CN_DESCRIPTION, json_string(cmd.arg_types[i].description.c_str()));
        json_object_set_new(p, CN_TYPE, json_string(modulecmd_argtype_to_str(cmd.arg_types[i]).c_str()));
        json_object_set_new(p, CN_REQUIRED, json_boolean(modulecmd_arg_is_required(cmd.arg_types[i])));
        json_array_append_new(param, p);
    }

    std::string self = mxb::cat(cmd.domain, "/", cmd.identifier);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, self.c_str()));
    json_object_set_new(attr, CN_PARAMETERS, param);
    json_object_set_new(obj, CN_ATTRIBUTES, attr);

    return mxb::Json(obj, mxb::Json::RefType::STEAL);
}

json_t* modulecmd_to_json(std::string_view domain, const char* host)
{
    mxb::Json rval(mxb::Json::Type::ARRAY);
    std::lock_guard guard(this_unit.lock);

    for (const auto& d : this_unit.domains)
    {
        if (mxb::sv_case_eq(d.domain, domain))
        {
            for (const auto& cmd : d.commands)
            {
                rval.add_array_elem(to_json(cmd, host));
            }

            break;
        }
    }

    return rval.release();
}

bool modulecmd_arg_is_required(const ModuleCmdArg& t)
{
    return (t.options & mxs::modulecmd::ARG_OPTIONAL) == 0;
}

ModuleCmdArg::ModuleCmdArg(mxs::modulecmd::ArgType type, std::string desc)
    : ModuleCmdArg(type, 0, std::move(desc))
{
}

ModuleCmdArg::ModuleCmdArg(mxs::modulecmd::ArgType type, uint8_t opts, std::string desc)
    : type(type)
    , options(opts)
    , description(std::move(desc))
{
}
