/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/modulecmd.hh>

#include <string>

#include <maxscale/alloc.h>
#include <maxscale/config.hh>
#include <maxscale/pcre2.h>

#include "internal/filter.hh"
#include "internal/modules.hh"
#include "internal/monitor.hh"

/** Size of the error buffer */
#define MODULECMD_ERRBUF_SIZE 512

/** Thread local error buffer */
thread_local char* errbuf = NULL;

/** Parameter passed to functions that do not always expect arguments */
static const MODULECMD_ARG MODULECMD_NO_ARGUMENTS = {0, NULL};

/**
 * A registered domain
 */
typedef struct modulecmd_domain
{
    char*                    domain;    /**< The domain */
    MODULECMD*               commands;  /**< List of registered commands */
    struct modulecmd_domain* next;      /**< Next domain */
} MODULECMD_DOMAIN;

/**
 * Internal functions
 */

/** The global list of registered domains */
static MODULECMD_DOMAIN* modulecmd_domains = NULL;
static std::mutex modulecmd_lock;

static inline void prepare_error()
{
    if (errbuf == NULL)
    {
        errbuf = (char*)MXS_MALLOC(MODULECMD_ERRBUF_SIZE);
        MXS_ABORT_IF_NULL(errbuf);
        errbuf[0] = '\0';
    }
}

/**
 * @brief Reset error message
 *
 * This should be the first function called in every API function that can
 * generate errors.
 */
static void reset_error()
{
    prepare_error();
    errbuf[0] = '\0';
}

static void report_argc_mismatch(const MODULECMD* cmd, int argc)
{
    if (cmd->arg_count_min == cmd->arg_count_max)
    {
        modulecmd_set_error("Expected %d arguments, got %d.", cmd->arg_count_min, argc);
    }
    else
    {
        modulecmd_set_error("Expected between %d and %d arguments, got %d.",
                            cmd->arg_count_min,
                            cmd->arg_count_max,
                            argc);
    }
}

static MODULECMD_DOMAIN* domain_create(const char* domain)
{
    MODULECMD_DOMAIN* rval = (MODULECMD_DOMAIN*)MXS_MALLOC(sizeof(*rval));
    char* dm = MXS_STRDUP(domain);

    if (rval && dm)
    {
        rval->domain = dm;
        rval->commands = NULL;
        rval->next = NULL;
    }
    else
    {
        MXS_FREE(rval);
        MXS_FREE(dm);
        rval = NULL;
    }

    return rval;
}

static void domain_free(MODULECMD_DOMAIN* dm)
{
    if (dm)
    {
        MXS_FREE(dm->domain);
        MXS_FREE(dm);
    }
}

static MODULECMD_DOMAIN* get_or_create_domain(const char* domain)
{

    MODULECMD_DOMAIN* dm;

    for (dm = modulecmd_domains; dm; dm = dm->next)
    {
        if (strcasecmp(dm->domain, domain) == 0)
        {
            return dm;
        }
    }

    if ((dm = domain_create(domain)))
    {
        dm->next = modulecmd_domains;
        modulecmd_domains = dm;
    }

    return dm;
}

static MODULECMD* command_create(const char* identifier,
                                 const char* domain,
                                 enum modulecmd_type type,
                                 MODULECMDFN entry_point,
                                 int argc,
                                 modulecmd_arg_type_t* argv,
                                 const char* description)
{
    mxb_assert((argc && argv) || (argc == 0 && argv == NULL));
    mxb_assert(description);
    MODULECMD* rval = (MODULECMD*)MXS_MALLOC(sizeof(*rval));
    char* id = MXS_STRDUP(identifier);
    char* dm = MXS_STRDUP(domain);
    char* desc = MXS_STRDUP(description);
    modulecmd_arg_type_t* types = (modulecmd_arg_type_t*)MXS_MALLOC(sizeof(*types) * (argc ? argc : 1));

    if (rval && id && dm && types && desc)
    {
        int argc_min = 0;

        for (int i = 0; i < argc; i++)
        {
            if (MODULECMD_ARG_IS_REQUIRED(&argv[i]))
            {
                argc_min++;
            }
            types[i] = argv[i];
        }

        if (argc == 0)
        {
            /** The command requires no arguments */
            types[0].type = MODULECMD_ARG_NONE;
            types[0].description = "";
        }

        rval->type = type;
        rval->func = entry_point;
        rval->identifier = id;
        rval->domain = dm;
        rval->description = desc;
        rval->arg_types = types;
        rval->arg_count_min = argc_min;
        rval->arg_count_max = argc;
        rval->next = NULL;
    }
    else
    {
        MXS_FREE(rval);
        MXS_FREE(id);
        MXS_FREE(dm);
        MXS_FREE(types);
        MXS_FREE(desc);
        rval = NULL;
    }

    return rval;
}

static void command_free(MODULECMD* cmd)
{
    if (cmd)
    {
        MXS_FREE(cmd->identifier);
        MXS_FREE(cmd->domain);
        MXS_FREE(cmd->arg_types);
        MXS_FREE(cmd);
    }
}

static bool domain_has_command(MODULECMD_DOMAIN* dm, const char* id)
{
    for (MODULECMD* cmd = dm->commands; cmd; cmd = cmd->next)
    {
        if (strcasecmp(cmd->identifier, id) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool process_argument(const MODULECMD* cmd,
                             modulecmd_arg_type_t* type,
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
            if ((arg->value.string = MXS_STRDUP((char*)value)))
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
            if ((arg->value.service = service_find((char*)value)))
            {
                if (MODULECMD_ALLOW_NAME_MISMATCH(type)
                    || strcmp(cmd->domain, arg->value.service->routerModule) == 0)
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
            if ((arg->value.server = server_find_by_unique_name((char*)value)))
            {
                if (MODULECMD_ALLOW_NAME_MISMATCH(type)
                    || strcmp(cmd->domain, arg->value.server->protocol) == 0)
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
            if ((arg->value.monitor = monitor_find((char*)value)))
            {
                const char* eff_name = mxs_module_get_effective_name(arg->value.monitor->module_name);
                if (MODULECMD_ALLOW_NAME_MISMATCH(type) || strcasecmp(cmd->domain, eff_name) == 0)
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
                const char* orig_name = filter_def_get_module_name(arg->value.filter);
                const char* eff_name = mxs_module_get_effective_name(orig_name);
                if (MODULECMD_ALLOW_NAME_MISMATCH(type) || strcasecmp(cmd->domain, eff_name) == 0)
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
            MXS_ERROR("Undefined argument type: %0lx", type->type);
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
    MODULECMD_ARG* arg = (MODULECMD_ARG*)MXS_MALLOC(sizeof(*arg));
    struct arg_node* argv = (struct arg_node*)MXS_CALLOC(argc, sizeof(*argv));

    if (arg && argv)
    {
        arg->argc = argc;
        arg->argv = argv;
    }
    else
    {
        MXS_FREE(argv);
        MXS_FREE(arg);
        arg = NULL;
    }

    return arg;
}

static void free_argument(struct arg_node* arg)
{
    switch (arg->type.type)
    {
    case MODULECMD_ARG_STRING:
        MXS_FREE(arg->value.string);
        break;

    case MODULECMD_ARG_SESSION:
        session_put_ref(arg->value.session);
        break;

    default:
        break;
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
                                modulecmd_arg_type_t* argv,
                                const char* description)
{
    reset_error();
    bool rval = false;
    std::lock_guard<std::mutex> guard(modulecmd_lock);

    MODULECMD_DOMAIN* dm = get_or_create_domain(domain);

    if (dm)
    {
        if (domain_has_command(dm, identifier))
        {
            modulecmd_set_error("Command registered more than once: %s::%s", domain, identifier);
            MXS_ERROR("Command registered more than once: %s::%s", domain, identifier);
        }
        else
        {
            MODULECMD* cmd = command_create(identifier,
                                            domain,
                                            type,
                                            entry_point,
                                            argc,
                                            argv,
                                            description);

            if (cmd)
            {
                cmd->next = dm->commands;
                dm->commands = cmd;
                rval = true;
            }
        }
    }

    return rval;
}

const MODULECMD* modulecmd_find_command(const char* domain, const char* identifier)
{
    reset_error();

    const char* effective_domain = mxs_module_get_effective_name(domain);

    MODULECMD* rval = NULL;
    std::lock_guard<std::mutex> guard(modulecmd_lock);

    for (MODULECMD_DOMAIN* dm = modulecmd_domains; dm; dm = dm->next)
    {
        if (strcasecmp(effective_domain, dm->domain) == 0)
        {
            for (MODULECMD* cmd = dm->commands; cmd; cmd = cmd->next)
            {
                if (strcasecmp(cmd->identifier, identifier) == 0)
                {
                    rval = cmd;
                    break;
                }
            }
            break;
        }
    }

    if (rval == NULL)
    {
        modulecmd_set_error("Command not found: %s::%s", domain, identifier);
    }

    return rval;
}

MODULECMD_ARG* modulecmd_arg_parse(const MODULECMD* cmd, int argc, const void** argv)
{
    reset_error();

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
                    modulecmd_set_error("Argument %d, %s: %s",
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

        MXS_FREE(arg->argv);
        MXS_FREE(arg);
    }
}

static void modulecmd_clear_error()
{
    prepare_error();
    errbuf[0] = '\0';
}

bool modulecmd_call_command(const MODULECMD* cmd, const MODULECMD_ARG* args, json_t** output)
{
    bool rval = false;
    reset_error();

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

        modulecmd_clear_error();

        json_t* discard = NULL;
        rval = cmd->func(args, output ? output : &discard);
        json_decref(discard);
    }

    return rval;
}

void modulecmd_set_error(const char* format, ...)
{
    prepare_error();

    va_list list;
    va_start(list, format);
    vsnprintf(errbuf, MODULECMD_ERRBUF_SIZE, format, list);
    va_end(list);
}

const char* modulecmd_get_error()
{
    prepare_error();
    return errbuf;
}

json_t* modulecmd_get_json_error()
{
    json_t* obj = NULL;
    std::string errmsg = modulecmd_get_error();
    modulecmd_clear_error();

    if (errmsg.length())
    {
        json_t* err = json_object();
        json_object_set_new(err, "detail", json_string(errmsg.c_str()));

        json_t* arr = json_array();
        json_array_append_new(arr, err);

        obj = json_object();
        json_object_set_new(obj, "errors", arr);
    }

    return obj;
}

bool modulecmd_foreach(const char* domain_re,
                       const char* ident_re,
                       bool (* fn)(const MODULECMD* cmd, void* data),
                       void* data)
{
    bool rval = true;
    bool stop = false;
    std::lock_guard<std::mutex> guard(modulecmd_lock);

    for (MODULECMD_DOMAIN* domain = modulecmd_domains; domain && rval && !stop; domain = domain->next)
    {
        int err;
        mxs_pcre2_result_t d_res = domain_re ?
            mxs_pcre2_simple_match(domain_re, domain->domain, PCRE2_CASELESS, &err) :
            MXS_PCRE2_MATCH;

        if (d_res == MXS_PCRE2_MATCH)
        {
            for (MODULECMD* cmd = domain->commands; cmd && rval; cmd = cmd->next)
            {
                mxs_pcre2_result_t i_res = ident_re ?
                    mxs_pcre2_simple_match(ident_re, cmd->identifier, PCRE2_CASELESS, &err) :
                    MXS_PCRE2_MATCH;

                if (i_res == MXS_PCRE2_MATCH)
                {
                    if (!fn(cmd, data))
                    {
                        stop = true;
                        break;
                    }
                }
                else if (i_res == MXS_PCRE2_ERROR)
                {
                    PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];
                    pcre2_get_error_message(err, errbuf, sizeof(errbuf));
                    MXS_ERROR("Failed to match command identifier with '%s': %s", ident_re, errbuf);
                    modulecmd_set_error("Failed to match command identifier with '%s': %s", ident_re, errbuf);
                    rval = false;
                }
            }
        }
        else if (d_res == MXS_PCRE2_ERROR)
        {
            PCRE2_UCHAR errbuf[MXS_STRERROR_BUFLEN];
            pcre2_get_error_message(err, errbuf, sizeof(errbuf));
            MXS_ERROR("Failed to match command domain with '%s': %s", domain_re, errbuf);
            modulecmd_set_error("Failed to match command domain with '%s': %s", domain_re, errbuf);
            rval = false;
        }
    }

    return rval;
}

#define format_type(a, b) (MODULECMD_ARG_IS_REQUIRED(a) ? b : "[" b "]")

const char* modulecmd_argtype_to_str(modulecmd_arg_type_t* type)
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
        MXS_ERROR("Unknown type");
        break;
    }

    return rval;
}

bool modulecmd_arg_is_present(const MODULECMD_ARG* arg, int idx)
{
    return arg->argc > idx
           && MODULECMD_GET_TYPE(&arg->argv[idx].type) != MODULECMD_ARG_NONE;
}
