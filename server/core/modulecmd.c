/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/modulecmd.h>
#include <maxscale/spinlock.h>

/**
 * A registered domain
 */
typedef struct modulecmd_domain
{
    char                    *domain; /**< The domain */
    MODULECMD               *commands; /**< List of registered commands */
    struct modulecmd_domain *next; /**< Next domain */
} MODULECMD_DOMAIN;

/**
 * Internal functions
 */

/** The global list of registered domains */
static MODULECMD_DOMAIN *modulecmd_domains = NULL;
static SPINLOCK modulecmd_lock = SPINLOCK_INIT;

static MODULECMD_DOMAIN* domain_create(const char *domain)
{
    MODULECMD_DOMAIN *rval = MXS_MALLOC(sizeof(*rval));
    char *dm = MXS_STRDUP(domain);

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

static void domain_free(MODULECMD_DOMAIN *dm)
{
    if (dm)
    {
        MXS_FREE(dm->domain);
        MXS_FREE(dm);
    }
}

static MODULECMD_DOMAIN* get_or_create_domain(const char *domain)
{

    MODULECMD_DOMAIN *dm;

    for (dm = modulecmd_domains; dm; dm = dm->next)
    {
        if (strcmp(dm->domain, domain) == 0)
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

static MODULECMD* command_create(const char *identifier, const char *domain,
                                 MODULECMDFN entry_point, int argc,
                                 modulecmd_arg_type_t* argv)
{
    MODULECMD *rval = MXS_MALLOC(sizeof(*rval));
    char *id = MXS_STRDUP(identifier);
    char *dm = MXS_STRDUP(domain);
    modulecmd_arg_type_t *types = MXS_MALLOC(sizeof(*types) * argc);

    if (rval && id  && dm && types)
    {
        for (int i = 0; i < argc; i++)
        {
            types[i] = argv[i];
        }

        rval->func = entry_point;
        rval->identifier = id;
        rval->domain = dm;
        rval->arg_types = types;
        rval->arg_count = argc;
        rval->next = NULL;
    }
    else
    {
        MXS_FREE(rval);
        MXS_FREE(id);
        MXS_FREE(dm);
        MXS_FREE(types);
        rval = NULL;
    }

    return rval;
}

static void command_free(MODULECMD *cmd)
{
    if (cmd)
    {
        MXS_FREE(cmd->identifier);
        MXS_FREE(cmd->domain);
        MXS_FREE(cmd->arg_types);
        MXS_FREE(cmd);
    }
}

static bool domain_has_command(MODULECMD_DOMAIN *dm, const char *id)
{
    for (MODULECMD *cmd = dm->commands; cmd; cmd = cmd->next)
    {
        if (strcmp(cmd->identifier, id) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool process_argument(modulecmd_arg_type_t type, const char* value,
                             struct arg_node *arg)
{
    bool rval = false;

    if (!MODULECMD_ARG_IS_REQUIRED(type) && value == NULL)
    {
        arg->type = MODULECMD_ARG_NONE;
        rval = true;
    }
    else if (value)
    {
        switch (MODULECMD_GET_TYPE(type))
        {
            case MODULECMD_ARG_NONE:
                arg->type = MODULECMD_ARG_NONE;
                rval = true;
                break;

            case MODULECMD_ARG_STRING:
                if ((arg->value.string = MXS_STRDUP(value)))
                {
                    arg->type = MODULECMD_ARG_STRING;
                    rval = true;
                }
                break;

            case MODULECMD_ARG_BOOLEAN:
            {
                int truthval = config_truth_value((char*)value);
                if (truthval != -1)
                {
                    arg->value.boolean = truthval;
                    arg->type = MODULECMD_ARG_BOOLEAN;
                    rval = true;
                }
            }
            break;

            case MODULECMD_ARG_SERVICE:
                if ((arg->value.service = service_find((char*)value)))
                {
                    arg->type = MODULECMD_ARG_SERVICE;
                    rval = true;
                }
                break;

            case MODULECMD_ARG_SERVER:
                if ((arg->value.server = server_find_by_unique_name(value)))
                {
                    arg->type = MODULECMD_ARG_SERVER;
                    rval = true;
                }
                break;

            case MODULECMD_ARG_SESSION:
                // TODO: Implement this
                break;

            case MODULECMD_ARG_DCB:
                // TODO: Implement this
                break;

            case MODULECMD_ARG_MONITOR:
                if ((arg->value.monitor = monitor_find((char*)value)))
                {
                    arg->type = MODULECMD_ARG_MONITOR;
                    rval = true;
                }
                break;

            case MODULECMD_ARG_FILTER:
                if ((arg->value.filter = filter_find((char*)value)))
                {
                    arg->type = MODULECMD_ARG_FILTER;
                    rval = true;
                }
                break;

            default:
                ss_dassert(false);
                MXS_ERROR("Undefined argument type: %0lx", type);
                break;
        }
    }

    return rval;
}

static MODULECMD_ARG* modulecmd_arg_create(int argc)
{
    MODULECMD_ARG* arg = MXS_MALLOC(sizeof(*arg));
    struct arg_node *argv = MXS_CALLOC(argc, sizeof(*argv));

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

static void free_argument(struct arg_node *arg)
{
    switch (arg->type)
    {
        case MODULECMD_ARG_STRING:
            MXS_FREE(arg->value.string);
            break;

        default:
            break;
    }
}

/**
 * Public functions declared in modulecmd.h
 */

bool modulecmd_register_command(const char *domain, const char *identifier,
                                MODULECMDFN entry_point, int argc, modulecmd_arg_type_t *argv)
{
    bool rval = false;
    spinlock_acquire(&modulecmd_lock);

    MODULECMD_DOMAIN *dm = get_or_create_domain(domain);

    if (dm)
    {
        if (domain_has_command(dm, identifier))
        {
            MXS_ERROR("Command '%s' in domain '%s' was registered more than once.",
                      identifier, domain);
        }
        else
        {
            MODULECMD *cmd = command_create(identifier, domain, entry_point, argc, argv);

            if (cmd)
            {
                cmd->next = dm->commands;
                dm->commands = cmd;
                rval = true;
            }
        }
    }

    spinlock_release(&modulecmd_lock);

    return rval;
}

const MODULECMD* modulecmd_find_command(const char *domain, const char *identifier)
{
    MODULECMD *rval = NULL;
    spinlock_acquire(&modulecmd_lock);

    for (MODULECMD_DOMAIN *dm = modulecmd_domains; dm; dm = dm->next)
    {
        if (strcmp(domain, dm->domain) == 0)
        {
            for (MODULECMD *cmd = dm->commands; cmd; cmd = cmd->next)
            {
                if (strcmp(cmd->identifier, identifier) == 0)
                {
                    rval = cmd;
                    break;
                }
            }
            break;
        }
    }

    spinlock_release(&modulecmd_lock);
    return rval;
}

MODULECMD_ARG* modulecmd_arg_parse(const MODULECMD *cmd, int argc, const char **argv)
{
    int argc_min = 0;

    for (int i = 0; i < cmd->arg_count; i++)
    {
        if (MODULECMD_ARG_IS_REQUIRED(cmd->arg_types[i]))
        {
            argc_min++;
        }
    }

    MODULECMD_ARG* arg = NULL;

    if (argc >= argc_min && argc <= cmd->arg_count)
    {
        arg = modulecmd_arg_create(cmd->arg_count);
        bool error = false;

        if (arg)
        {
            for (int i = 0; i < cmd->arg_count && i < argc; i++)
            {
                if (!process_argument(cmd->arg_types[i], argv[i], &arg->argv[i]))
                {
                    MXS_ERROR("Failed to parse argument %d: %s", i + 1, argv[i] ? argv[i] : "NULL");
                    error = true;
                }
            }

            if (error)
            {
                modulecmd_arg_free(arg);
                arg = NULL;
            }
        }
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

bool modulecmd_call_command(const MODULECMD *cmd, const MODULECMD_ARG *args)
{
    return cmd->func(args);
}
