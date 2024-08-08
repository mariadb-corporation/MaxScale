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
const char duplicate_cmd[] = "Command registered more than once: %.*s::%.*s";

using maxscale::Monitor;

/**
 * Module command using traditional positional argument passing.
 */
class PosArgModuleCmd final : public ModuleCmd
{
public:
    ModuleCmdFn                   func;             /**< The registered function */
    int                           arg_count_min {0};/**< Minimum number of arguments */
    int                           arg_count_max {0};/**< Maximum number of arguments */
    std::vector<ModuleCmdArgDesc> arg_types;        /**< Argument types */

    bool      call(const mxs::KeyValueVector& args, json_t** cmd_output) const override;
    mxb::Json to_json(const std::string& cmd_name, const char* host) const override;

    int test_arg_parse(const mxs::KeyValueVector& args) const override
    {
        std::optional<ModuleCmdArgs> parsed_args = arg_parse(args);
        return parsed_args.has_value() ? parsed_args->size() : -1;
    }

    PosArgModuleCmd(std::string_view domain, mxs::modulecmd::CmdType type,
                    ModuleCmdFn entry_point, std::vector<ModuleCmdArgDesc> args,
                    std::string_view description);

private:
    std::optional<ModuleCmdArgs> arg_parse(const mxs::KeyValueVector& argv) const;
};

PosArgModuleCmd::PosArgModuleCmd(std::string_view domain,
                                 mxs::modulecmd::CmdType type,
                                 ModuleCmdFn entry_point,
                                 std::vector<ModuleCmdArgDesc> args,
                                 std::string_view description)
    : ModuleCmd(domain, type, description)
    , func(entry_point)
    , arg_count_max(args.size())
{
    int argc_min = 0;
    for (const auto& arg : args)
    {
        if (arg.is_required())
        {
            argc_min++;
        }
    }

    arg_count_min = argc_min;
    arg_types = std::move(args);
}

/**
 * Module command using named i.e. key-value arguments.
 */
class KVArgModuleCmd final : public ModuleCmd
{
public:
    KVModuleCmdFn                   func;               /**< The registered function */
    std::vector<KVModuleCmdArgDesc> arg_types;          /**< Argument types */

    bool call(const mxs::KeyValueVector& args, json_t** cmd_output) const override
    {
        bool rval = false;
        std::optional<KVModuleCmdArgs> parsed_args = arg_parse(args);
        if (parsed_args)
        {
            auto [ret, out] = func(*parsed_args);
            rval = ret;
            if (out)
            {
                *cmd_output = out.release();
            }
        }
        return rval;
    }

    mxb::Json to_json(const std::string& cmd_name, const char* host) const override;

    int test_arg_parse(const mxs::KeyValueVector& args) const override
    {
        auto parsed_args = arg_parse(args);
        return parsed_args.has_value() ? parsed_args->size() : -1;
    }

    KVArgModuleCmd(std::string_view domain, mxs::modulecmd::CmdType type,
                   KVModuleCmdFn entry_point, std::vector<KVModuleCmdArgDesc> argv,
                   std::string_view description)
        : ModuleCmd(domain, type, description)
        , func(entry_point)
        , arg_types(std::move(argv))
    {
    }

private:
    std::optional<KVModuleCmdArgs> arg_parse(const mxs::KeyValueVector& args) const;
};

/**
 * A registered domain. Contains registered commands, mapped by command name.
 */

struct MODULECMD_DOMAIN
{
    std::map<std::string, PosArgModuleCmd> positional_commands;
    std::map<std::string, KVArgModuleCmd>  key_value_commands;
};

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

std::optional<KVModuleCmdArgs> KVArgModuleCmd::arg_parse(const mxs::KeyValueVector& args) const
{
    using std::string;
    std::map<string, string> key_values;

    bool error = false;
    for (auto& [key, value] : args)
    {
        if (key.empty())
        {
            MXB_ERROR("Empty argument name not allowed.");
            error = true;
        }
        else if (value.empty())
        {
            MXB_ERROR("Argument '%s' does not have a corresponding value. This command expects arguments as "
                      "a list of key=value pairs.", key.c_str());
            error = true;
        }
        else if (!key_values.emplace(key, value).second)
        {
            MXB_ERROR("Argument '%s' is defined multiple times.", key.c_str());
            error = true;
        }
    }

    std::optional<KVModuleCmdArgs> rval;
    if (!error)
    {
        // Check argument types and that all mandatory arguments are defined.
        KVModuleCmdArgs parsed_args;
        for (const auto& arg_desc : arg_types)
        {
            auto it = key_values.find(arg_desc.name);
            if (it != key_values.end())
            {
                string err;
                ModuleCmdArg parsed_arg;
                if (process_argument(this, arg_desc, it->second, &parsed_arg, err))
                {
                    parsed_args.add_arg(it->first, std::move(parsed_arg));
                    key_values.erase(it);
                }
                else
                {
                    error = true;
                    MXB_ERROR("Argument '%s' value '%s': %s",
                              it->first.c_str(), it->second.c_str(), err.c_str());
                }
            }
            else if (arg_desc.is_required())
            {
                MXB_ERROR("Mandatory argument '%s' is not defined.", arg_desc.name.c_str());
                error = true;
            }
        }

        if (!error)
        {
            // Any remaining arguments were unrecognized.
            if (key_values.size() == 1)
            {
                MXB_ERROR("Argument '%s' was unrecognized.", key_values.begin()->first.c_str());
                error = true;
            }
            else if (!key_values.empty())
            {
                std::vector<string> unrecognized;
                for (auto& kvs : key_values)
                {
                    unrecognized.push_back(kvs.first);
                }
                string list = mxb::create_list_string(unrecognized, ", ", " and ", "'");
                MXB_ERROR("Arguments %s were unrecognized.", list.c_str());
                error = true;
            }
        }

        if (!error)
        {
            rval = std::move(parsed_args);
        }
    }
    return rval;
}

MODULECMD_DOMAIN* find_domain(std::string_view domain)
{
    std::string domain_lower = mxb::tolower(domain);
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
    return dm;
}
}

bool modulecmd_register_command(std::string_view domain,
                                std::string_view identifier,
                                mxs::modulecmd::CmdType type,
                                ModuleCmdFn entry_point,
                                std::vector<ModuleCmdArgDesc> args,
                                std::string_view description)
{
    std::lock_guard guard(this_unit.lock);
    auto* dm = find_domain(domain);
    bool success = dm->positional_commands.emplace(mxb::tolower(identifier),
                                                   PosArgModuleCmd(domain, type, entry_point, std::move(args),
                                                                   description)).second;
    if (!success)
    {
        MXB_ERROR(duplicate_cmd,
                  (int)domain.size(), domain.data(), (int)identifier.size(), identifier.data());
        mxb_assert(!true);
    }
    return success;
}

bool modulecmd_register_kv_command(std::string_view domain,
                                   std::string_view identifier,
                                   mxs::modulecmd::CmdType type,
                                   KVModuleCmdFn entry_point,
                                   std::vector<KVModuleCmdArgDesc> args,
                                   std::string_view description)
{
    std::lock_guard guard(this_unit.lock);
    auto* dm = find_domain(domain);
    bool success = dm->key_value_commands.emplace(mxb::tolower(identifier),
                                                  KVArgModuleCmd(domain, type, entry_point, std::move(args),
                                                                 description)).second;
    if (!success)
    {
        MXB_ERROR(duplicate_cmd,
                  (int)domain.size(), domain.data(), (int)identifier.size(), identifier.data());
        mxb_assert(!true);
    }
    return success;
}

const ModuleCmd* modulecmd_find_command(const char* domain, const char* identifier,
                                        CmdVersion preferred_version)
{
    std::string effective_domain = module_get_effective_name(domain);

    const ModuleCmd* rval = nullptr;
    std::lock_guard guard(this_unit.lock);

    auto domain_it = this_unit.domains.find(effective_domain);
    if (domain_it != this_unit.domains.end())
    {
        std::string id_lower = mxb::tolower(identifier);
        auto& dm = domain_it->second;
        auto pos_cmd_it = dm.positional_commands.find(id_lower);
        auto kv_cmd_it = dm.key_value_commands.find(id_lower);
        bool have_pos_cmd = pos_cmd_it != dm.positional_commands.end();
        bool have_kv_cmd = kv_cmd_it != dm.key_value_commands.end();

        if (have_pos_cmd && have_kv_cmd)
        {
            if (preferred_version == CmdVersion::POS_ARG)
            {
                rval = &pos_cmd_it->second;
            }
            else
            {
                rval = &kv_cmd_it->second;
            }
        }
        else if (have_pos_cmd)
        {
            rval = &pos_cmd_it->second;
        }
        else if (have_kv_cmd)
        {
            rval = &kv_cmd_it->second;
        }
    }
    return rval;
}

/**
 * @brief Parse arguments for a command
 *
 * The argument types expect different forms of input.
 *
 * | Argument type         | Expected input    |
 * |-----------------------|-------------------|
 * | MODULECMD_ARG_SERVICE | Service name      |
 * | MODULECMD_ARG_SERVER  | Server name       |
 * | MODULECMD_ARG_MONITOR | Monitor name      |
 * | MODULECMD_ARG_FILTER  | Filter name       |
 * | MODULECMD_ARG_STRING  | String            |
 * | MODULECMD_ARG_BOOLEAN | Boolean value     |
 *
 * @param argv Argument list in string format
 * @return Parsed arguments or NULL on error
 */
std::optional<ModuleCmdArgs> PosArgModuleCmd::arg_parse(const mxs::KeyValueVector& argv) const
{
    auto cmd = this;
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
            // This command type does not support key-value pairs, so combine a key-value definition to
            // one value.
            const auto& kv = argv[i];
            const std::string* eff_value = &kv.first;
            std::string combined;
            if (!kv.second.empty())
            {
                combined.append(kv.first).append("=").append(kv.second);
                eff_value = &combined;
            }

            if (!process_argument(cmd, cmd->arg_types[i], *eff_value, &arg[i], err))
            {
                error = true;
                MXB_ERROR("Argument %d, %s: %s", i + 1, err.c_str(),
                          !eff_value->empty() ? eff_value->c_str() : "No argument given");
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

bool PosArgModuleCmd::call(const mxs::KeyValueVector& args, json_t** cmd_output) const
{
    mxb_assert(cmd_output);

    bool rval = false;
    std::optional<ModuleCmdArgs> parsed_args = arg_parse(args);
    if (parsed_args)
    {
        mxb_assert(arg_count_min == 0 || !args.empty());
        rval = func(*parsed_args, cmd_output);
    }
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

ModuleCmd::ModuleCmd(std::string_view domain, mxs::modulecmd::CmdType type, std::string_view desc)
    : domain(domain)
    , description(desc)
    , type(type)
{
    mxb_assert(!desc.empty());
}

json_t* ModuleCmd::base_json(const std::string& cmd_name, const char* host) const
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(cmd_name.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE_COMMAND));

    json_t* attr = json_object();
    const char* method = type == mxs::modulecmd::CmdType::WRITE ? "POST" : "GET";
    json_object_set_new(attr, CN_METHOD, json_string(method));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(description.c_str()));
    json_object_set_new(obj, CN_ATTRIBUTES, attr);

    std::string self = mxb::cat(domain, "/", cmd_name);
    json_object_set_new(obj, CN_LINKS, mxs_json_self_link(host, CN_MODULES, self.c_str()));
    return obj;
}

mxb::Json PosArgModuleCmd::to_json(const std::string& cmd_name, const char* host) const
{
    json_t* obj = base_json(cmd_name, host);
    json_t* attr = json_object_get(obj, CN_ATTRIBUTES);
    json_object_set_new(attr, CN_ARG_MIN, json_integer(arg_count_min));
    json_object_set_new(attr, CN_ARG_MAX, json_integer(arg_count_max));

    json_t* param = json_array();
    for (const auto& arg_info : arg_types)
    {
        json_t* p = arg_info.to_json();
        json_array_append_new(param, p);
    }
    json_object_set_new(attr, CN_PARAMETERS, param);

    return mxb::Json(obj, mxb::Json::RefType::STEAL);
}

json_t* ModuleCmdArgDesc::to_json() const
{
    json_t* p = json_object();
    json_object_set_new(p, CN_DESCRIPTION, json_string(description.c_str()));
    json_object_set_new(p, CN_TYPE, json_string(modulecmd_argtype_to_str(*this).c_str()));
    json_object_set_new(p, CN_REQUIRED, json_boolean(is_required()));
    return p;
}

mxb::Json KVArgModuleCmd::to_json(const std::string& cmd_name, const char* host) const
{
    json_t* obj = base_json(cmd_name, host);
    json_t* attr = json_object_get(obj, CN_ATTRIBUTES);

    json_t* param = json_array();
    for (const auto& arg_info : arg_types)
    {
        json_t* p = arg_info.to_json();
        json_object_set_new(p, CN_NAME, json_string(arg_info.name.c_str()));
        json_array_append_new(param, p);
    }
    json_object_set_new(attr, CN_PARAMETERS, param);

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
        for (const auto& map_elem : cmd_map.positional_commands)
        {
            rval.add_array_elem(map_elem.second.to_json(map_elem.first, host));
        }
        for (const auto& map_elem : cmd_map.key_value_commands)
        {
            rval.add_array_elem(map_elem.second.to_json(map_elem.first, host));
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

KVModuleCmdArgDesc::KVModuleCmdArgDesc(std::string name, mxs::modulecmd::ArgType type, std::string desc)
    : KVModuleCmdArgDesc(std::move(name), type, 0, std::move(desc))
{
}

KVModuleCmdArgDesc::KVModuleCmdArgDesc(std::string name, mxs::modulecmd::ArgType type, uint8_t opts,
                                       std::string desc)
    : ModuleCmdArgDesc(type, opts, std::move(desc))
    , name(std::move(name))
{
}

void KVModuleCmdArgs::add_arg(std::string name, ModuleCmdArg value)
{
    m_contents.emplace(std::move(name), std::move(value));
}

size_t KVModuleCmdArgs::size() const
{
    return m_contents.size();
}

const ModuleCmdArg* KVModuleCmdArgs::get_arg(const std::string& name) const
{
    auto it = m_contents.find(name);
    return it == m_contents.end() ? nullptr : &it->second;
}

std::string KVModuleCmdArgs::get_string(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->string : "";
}

bool KVModuleCmdArgs::get_bool(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->boolean : false;
}

SERVICE* KVModuleCmdArgs::get_service(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->service : nullptr;
}

SERVER* KVModuleCmdArgs::get_server(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->server : nullptr;
}

mxs::Monitor* KVModuleCmdArgs::get_monitor(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->monitor : nullptr;
}

MXS_FILTER_DEF* KVModuleCmdArgs::get_filter(const std::string& key) const
{
    auto* val = get_arg(key);
    return val ? val->filter : nullptr;
}
