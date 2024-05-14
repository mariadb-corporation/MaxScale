/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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

/** Parameter passed to functions that do not always expect arguments */
static const MODULECMD_ARG MODULECMD_NO_ARGUMENTS = {0, NULL};

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
                                enum modulecmd_type type,
                                MODULECMDFN entry_point,
                                int argc,
                                const modulecmd_arg_type_t* argv,
                                const char* description)
{
    mxb_assert((argc && argv) || (argc == 0 && argv == NULL));
    mxb_assert(description);
    MODULECMD rval;

    int argc_min = 0;

    if (argc == 0)
    {
        /** The command requires no arguments */
        rval.arg_types.push_back(modulecmd_arg_type_t {MODULECMD_ARG_NONE, ""});
    }
    else
    {
        rval.arg_types.resize(argc);
        for (int i = 0; i < argc; i++)
        {
            if (MODULECMD_ARG_IS_REQUIRED(&argv[i]))
            {
                argc_min++;
            }
            rval.arg_types[i] = argv[i];
        }
    }

    rval.type = type;
    rval.func = entry_point;
    rval.identifier = identifier;
    rval.domain = domain;
    rval.description = description;
    rval.arg_count_min = argc_min;
    rval.arg_count_max = argc;

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
                             const modulecmd_arg_type_t* type,
                             const void* value,
                             struct arg_node* arg,
                             const char** err)
{
    bool rval = false;

    if (!MODULECMD_ARG_IS_REQUIRED(type) && value == NULL)
    {
        arg->type.type = MODULECMD_ARG_NONE;
        rval = true;
    }
    else if (value)
    {
        switch (MODULECMD_GET_TYPE(type))
        {
        case MODULECMD_ARG_NONE:
            arg->type.type = MODULECMD_ARG_NONE;
            rval = true;
            break;

        case MODULECMD_ARG_STRING:
            if ((arg->value.string = MXB_STRDUP((char*)value)))
            {
                arg->type.type = MODULECMD_ARG_STRING;
                rval = true;
            }
            else
            {
                *err = "memory allocation failed";
            }
            break;

        case MODULECMD_ARG_BOOLEAN:
            {
                int truthval = config_truth_value((char*)value);
                if (truthval != -1)
                {
                    arg->value.boolean = truthval;
                    arg->type.type = MODULECMD_ARG_BOOLEAN;
                    rval = true;
                }
                else
                {
                    *err = "not a boolean value";
                }
            }
            break;

        case MODULECMD_ARG_SERVICE:
            if ((arg->value.service = Service::find((char*)value)))
            {
                if (MODULECMD_ALLOW_NAME_MISMATCH(type)
                    || strcmp(cmd->domain.c_str(), arg->value.service->router_name()) == 0)
                {
                    arg->type.type = MODULECMD_ARG_SERVICE;
                    rval = true;
                }
                else
                {
                    *err = "router and domain names don't match";
                }
            }
            else
            {
                *err = "service not found";
            }
            break;

        case MODULECMD_ARG_SERVER:
            if ((arg->value.server = ServerManager::find_by_unique_name((char*)value)))
            {
                if (MODULECMD_ALLOW_NAME_MISMATCH(type))
                {
                    arg->type.type = MODULECMD_ARG_SERVER;
                    rval = true;
                }
                else
                {
                    *err = "server and domain names don't match";
                }
            }
            else
            {
                *err = "server not found";
            }
            break;

        case MODULECMD_ARG_SESSION:
            if ((arg->value.session = session_get_by_id(strtoul((const char*)value, NULL, 0))))
            {
                arg->type.type = MODULECMD_ARG_SESSION;
            }
            rval = true;
            break;

        case MODULECMD_ARG_DCB:
            arg->type.type = MODULECMD_ARG_DCB;
            arg->value.dcb = (DCB*)value;
            rval = true;
            break;

        case MODULECMD_ARG_MONITOR:
            if ((arg->value.monitor = MonitorManager::find_monitor((char*) value)))
            {
                std::string eff_name = module_get_effective_name(arg->value.monitor->m_module);
                if (MODULECMD_ALLOW_NAME_MISMATCH(type) || strcasecmp(cmd->domain.c_str(), eff_name.c_str()) == 0)
                {
                    arg->type.type = MODULECMD_ARG_MONITOR;
                    rval = true;
                }
                else
                {
                    *err = "monitor and domain names don't match";
                }
            }
            else
            {
                *err = "monitor not found";
            }
            break;

        case MODULECMD_ARG_FILTER:
            if (auto f = filter_find((char*)value))
            {
                arg->value.filter = f.get();
                const char* orig_name = f->module();
                std::string eff_name = module_get_effective_name(orig_name);
                if (MODULECMD_ALLOW_NAME_MISMATCH(type) || strcasecmp(cmd->domain.c_str(), eff_name.c_str()) == 0)
                {
                    arg->type.type = MODULECMD_ARG_FILTER;
                    rval = true;
                }
                else
                {
                    *err = "filter and domain names don't match";
                }
            }
            else
            {
                *err = "filter not found";
            }
            break;

        default:
            mxb_assert(false);
            MXB_ERROR("Undefined argument type: %0lx", type->type);
            *err = "internal error";
            break;
        }
    }
    else
    {
        *err = "required argument";
    }

    return rval;
}

static MODULECMD_ARG* modulecmd_arg_create(int argc)
{
    MODULECMD_ARG* arg = (MODULECMD_ARG*)MXB_MALLOC(sizeof(*arg));
    struct arg_node* argv = (struct arg_node*)MXB_CALLOC(argc, sizeof(*argv));

    if (arg && argv)
    {
        arg->argc = argc;
        arg->argv = argv;
    }
    else
    {
        MXB_FREE(argv);
        MXB_FREE(arg);
        arg = NULL;
    }

    return arg;
}

static void free_argument(struct arg_node* arg)
{
    switch (arg->type.type)
    {
    case MODULECMD_ARG_STRING:
        MXB_FREE(arg->value.string);
        break;

    case MODULECMD_ARG_SESSION:
        session_put_ref(arg->value.session);
        break;

    default:
        break;
    }
}
}

/**
 * Public functions declared in modulecmd.h
 */

bool modulecmd_register_command(const char* domain,
                                const char* identifier,
                                enum modulecmd_type type,
                                MODULECMDFN entry_point,
                                int argc,
                                const modulecmd_arg_type_t* argv,
                                const char* description)
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
                                                argc, argv, description));
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

MODULECMD_ARG* modulecmd_arg_parse(const MODULECMD* cmd, int argc, const void** argv)
{
    MODULECMD_ARG* arg = NULL;

    if (argc >= cmd->arg_count_min && argc <= cmd->arg_count_max)
    {
        arg = modulecmd_arg_create(cmd->arg_count_max);
        bool error = false;

        if (arg)
        {
            for (int i = 0; i < cmd->arg_count_max && i < argc; i++)
            {
                const char* err = "";

                if (!process_argument(cmd, &cmd->arg_types[i], argv[i], &arg->argv[i], &err))
                {
                    error = true;
                    MXB_ERROR("Argument %d, %s: %s",
                              i + 1,
                              err,
                              argv[i] ? (char*)argv[i] : "No argument given");
                    break;
                }
            }

            if (error)
            {
                modulecmd_arg_free(arg);
                arg = NULL;
            }
            else
            {
                arg->argc = argc;
            }
        }
    }
    else
    {
        report_argc_mismatch(cmd, argc);
    }

    return arg;
}

void modulecmd_arg_free(MODULECMD_ARG* arg)
{
    if (arg)
    {
        for (int i = 0; i < arg->argc; i++)
        {
            free_argument(&arg->argv[i]);
        }

        MXB_FREE(arg->argv);
        MXB_FREE(arg);
    }
}

bool modulecmd_call_command(const MODULECMD* cmd, const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;

    if (cmd->arg_count_min > 0 && args == NULL)
    {
        report_argc_mismatch(cmd, 0);
    }
    else
    {
        if (args == NULL)
        {
            args = &MODULECMD_NO_ARGUMENTS;
        }

        json_t* discard = NULL;
        rval = cmd->func(args, output ? output : &discard);
        json_decref(discard);
    }

    return rval;
}

#define format_type(a, b) (MODULECMD_ARG_IS_REQUIRED(a) ? b : "[" b "]")

const char* modulecmd_argtype_to_str(const modulecmd_arg_type_t* type)
{
    const char* rval = "UNKNOWN";

    switch (MODULECMD_GET_TYPE(type))
    {
    case MODULECMD_ARG_NONE:
        rval = format_type(type, "NONE");
        break;

    case MODULECMD_ARG_STRING:
        rval = format_type(type, "STRING");
        break;

    case MODULECMD_ARG_BOOLEAN:
        rval = format_type(type, "BOOLEAN");
        break;

    case MODULECMD_ARG_SERVICE:
        rval = format_type(type, "SERVICE");
        break;

    case MODULECMD_ARG_SERVER:
        rval = format_type(type, "SERVER");
        break;

    case MODULECMD_ARG_SESSION:
        rval = format_type(type, "SESSION");
        break;

    case MODULECMD_ARG_DCB:
        rval = format_type(type, "DCB");
        break;

    case MODULECMD_ARG_MONITOR:
        rval = format_type(type, "MONITOR");
        break;

    case MODULECMD_ARG_FILTER:
        rval = format_type(type, "FILTER");
        break;

    default:
        mxb_assert(false);
        MXB_ERROR("Unknown type");
        break;
    }

    return rval;
}

bool modulecmd_arg_is_present(const MODULECMD_ARG* arg, int idx)
{
    return arg->argc > idx
           && MODULECMD_GET_TYPE(&arg->argv[idx].type) != MODULECMD_ARG_NONE;
}

mxb::Json to_json(const MODULECMD& cmd, const char* host)
{
    json_t* obj = json_object();
    json_object_set_new(obj, CN_ID, json_string(cmd.identifier.c_str()));
    json_object_set_new(obj, CN_TYPE, json_string(CN_MODULE_COMMAND));

    json_t* attr = json_object();
    const char* method = cmd.type == MODULECMD_TYPE_ACTIVE ? "POST" : "GET";
    json_object_set_new(attr, CN_METHOD, json_string(method));
    json_object_set_new(attr, CN_ARG_MIN, json_integer(cmd.arg_count_min));
    json_object_set_new(attr, CN_ARG_MAX, json_integer(cmd.arg_count_max));
    json_object_set_new(attr, CN_DESCRIPTION, json_string(cmd.description.c_str()));

    json_t* param = json_array();

    for (int i = 0; i < cmd.arg_count_max; i++)
    {
        json_t* p = json_object();
        json_object_set_new(p, CN_DESCRIPTION, json_string(cmd.arg_types[i].description));
        json_object_set_new(p, CN_TYPE, json_string(modulecmd_argtype_to_str(&cmd.arg_types[i])));
        json_object_set_new(p, CN_REQUIRED, json_boolean(MODULECMD_ARG_IS_REQUIRED(&cmd.arg_types[i])));
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
