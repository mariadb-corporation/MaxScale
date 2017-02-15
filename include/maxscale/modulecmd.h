#pragma once
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
 * @file module_command.h Module driven commands
 *
 * This header describes the structures and functions used to register new
 * functions for modules. It allows modules to introduce custom commands that
 * are registered into a module specific domain. These commands can then be
 * accessed from multiple different client interfaces without implementing the
 * same functionality again.
 */

#include <maxscale/cdefs.h>
#include <maxscale/dcb.h>
#include <maxscale/filter.h>
#include <maxscale/monitor.h>
#include <maxscale/server.h>
#include <maxscale/service.h>
#include <maxscale/session.h>

MXS_BEGIN_DECLS

/**
 * The argument type
 *
 * First 8 bits of @c value are reserved for argument type, bits 9 through
 * 32 are reserved for argument options and bits 33 through 64 are reserved
 * for future use.
 *
 * @c description should be a human-readable description of the argument.
 */
typedef struct
{
    uint64_t    type;        /**< The argument type and options */
    const char *description; /**< The argument description */
} modulecmd_arg_type_t;

/**
 * Argument types for the registered functions, the first 8 bits of
 * the modulecmd_arg_type_t type's @c value member. An argument can be of
 * only one type.
 */
#define MODULECMD_ARG_NONE        0 /**< Empty argument */
#define MODULECMD_ARG_STRING      1 /**< String */
#define MODULECMD_ARG_BOOLEAN     2 /**< Boolean value */
#define MODULECMD_ARG_SERVICE     3 /**< Service */
#define MODULECMD_ARG_SERVER      4 /**< Server */
#define MODULECMD_ARG_SESSION     6 /**< Session */
#define MODULECMD_ARG_DCB         8 /**< DCB */
#define MODULECMD_ARG_MONITOR     9 /**< Monitor */
#define MODULECMD_ARG_FILTER      10 /**< Filter */
#define MODULECMD_ARG_OUTPUT      11 /**< DCB suitable for writing results to.
                                          This should always be the first argument
                                          if the function requires an output DCB. */

/**
 * Options for arguments, bits 9 through 32
 */
#define MODULECMD_ARG_OPTIONAL            (1 << 8) /**< The argument is optional */
#define MODULECMD_ARG_NAME_MATCHES_DOMAIN (1 << 9) /**< Argument module name must match domain name */

/**
 * Helper macros
 */
#define MODULECMD_GET_TYPE(t) ((t)->type & 0xff)
#define MODULECMD_ARG_IS_REQUIRED(t) (((t)->type & MODULECMD_ARG_OPTIONAL) == 0)
#define MODULECMD_ALLOW_NAME_MISMATCH(t) (((t)->type & MODULECMD_ARG_NAME_MATCHES_DOMAIN) == 0)
#define MODULECMD_ARG_PRESENT(t) (MODULECMD_GET_TYPE(t) != MODULECMD_ARG_NONE)

/** Argument list node */
struct arg_node
{
    modulecmd_arg_type_t  type;
    union
    {
        char           *string;
        bool           boolean;
        SERVICE        *service;
        SERVER         *server;
        MXS_SESSION    *session;
        DCB            *dcb;
        MXS_MONITOR    *monitor;
        MXS_FILTER_DEF *filter;
    } value;
};

/** Argument list */
typedef struct
{
    int              argc;
    struct arg_node *argv;
} MODULECMD_ARG;

/**
 * The function signature for the module commands.
 *
 * The number of arguments will always be the maximum number of arguments the
 * module requested. If an argument had the MODULECMD_ARG_OPTIONAL flag, and
 * the argument was not provided, the type of the argument will be
 * MODULECMD_ARG_NONE.
 *
 * @param argv Argument list
 * @return True on success, false on error
 */
typedef bool (*MODULECMDFN)(const MODULECMD_ARG *argv);

/**
 * A registered command
 */
typedef struct modulecmd
{
    char                 *identifier; /**< Unique identifier */
    char                 *domain; /**< Command domain */
    MODULECMDFN           func; /**< The registered function */
    int                   arg_count_min; /**< Minimum number of arguments */
    int                   arg_count_max; /**< Maximum number of arguments */
    modulecmd_arg_type_t *arg_types; /**< Argument types */
    struct modulecmd     *next; /**< Next command */
} MODULECMD;

/**
 * @brief Register a new command
 *
 * This function registers a new command into the domain.
 *
 * @param domain Command domain
 * @param identifier The unique identifier for this command
 * @param entry_point The actual entry point function
 * @param argc Maximum number of arguments
 * @param argv Array of argument types of size @c argc
 * @return True if the module was successfully registered, false on error
 */
bool modulecmd_register_command(const char *domain, const char *identifier,
                                MODULECMDFN entry_point, int argc, modulecmd_arg_type_t *argv);

/**
 * @brief Find a registered command
 *
 * @param domain Command domain
 * @param identifier Command identifier
 * @return Registered command or NULL if no command was found
 */
const MODULECMD* modulecmd_find_command(const char *domain, const char *identifier);

/**
 * @brief Parse arguments for a command
 *
 * The argument types expect different forms of input.
 *
 * | Argument type         | Expected input    |
 * |-----------------------|-------------------|
 * | MODULECMD_ARG_SERVICE | Service name      |
 * | MODULECMD_ARG_SERVER  | Server name       |
 * | MODULECMD_ARG_SESSION | Session unique ID |
 * | MODULECMD_ARG_MONITOR | Monitor name      |
 * | MODULECMD_ARG_FILTER  | Filter name       |
 * | MODULECMD_ARG_STRING  | String            |
 * | MODULECMD_ARG_BOOLEAN | Boolean value     |
 * | MODULECMD_ARG_DCB     | Raw DCB pointer   |
 * | MODULECMD_ARG_OUTPUT  | DCB for output    |
 *
 * @param cmd Command for which the parameters are parsed
 * @param argc Number of arguments
 * @param argv Argument list in string format of size @c argc
 * @return Parsed arguments or NULL on error
 */
MODULECMD_ARG* modulecmd_arg_parse(const MODULECMD *cmd, int argc, const void **argv);

/**
 * @brief Free parsed arguments returned by modulecmd_arg_parse
 * @param arg Arguments to free
 */
void modulecmd_arg_free(MODULECMD_ARG *arg);

/**
 * @brief Check if an optional argument was defined
 *
 * This function looks the argument list @c arg at an offset of @c idx and
 * checks if the argument list contains a value for an optional argument.
 *
 * @param arg Argument list
 * @param idx Index of the argument, starts at 0
 * @return True if the optional argument is present
 */
bool modulecmd_arg_is_present(const MODULECMD_ARG *arg, int idx);

/**
 * @brief Call a registered command
 *
 * This calls a registered command in a specific domain. There are no guarantees
 * on the length of the call or whether it will block. All of this depends on the
 * module and what the command does.
 *
 * @param cmd Command to call
 * @param args Parsed command arguments
 * @return True on success, false on error
 */
bool modulecmd_call_command(const MODULECMD *cmd, const MODULECMD_ARG *args);

/**
 * @brief Set the current error message
 *
 * Modules that register commands should use this function to report errors.
 * This will overwrite any existing error message.
 *
 * @param format Format string
 * @param ... Format string arguments
 */
void modulecmd_set_error(const char *format, ...);

/**
 * @brief Get the latest error generated by the modulecmd system
 *
 * @return Human-readable error message
 */
const char* modulecmd_get_error();


/**
 * @brief Call a function for each command
 *
 * Calls a function for each matching command in the matched domains. The filters
 * for the domain and identifier are PCRE2 expressions that are matched against
 * the domain and identifier. These are optional and both @c domain and @c ident
 * can be NULL.
 *
 * @param domain_re Command domain filter, NULL for all domains
 *
 * @param ident_re Command identifier filter, NULL for all commands
 *
 * @param fn Function that is called for every command. The first parameter is
 *           the current command. The second parameter is the value of @c data.
 *           The function should return true to continue iteration or false to
 *           stop iteration early. The function must not call any of the functions
 *           declared in modulecmd.h.
 *
 * @param data User defined data passed as the second parameter to @c fn
 *
 * @return True on success, false on PCRE2 error. Use modulecmd_get_error()
 * to retrieve the error.
 */
bool modulecmd_foreach(const char *domain_re, const char *ident_re,
                       bool(*fn)(const MODULECMD *cmd, void *data), void *data);

/**
 * @brief Return argument type as string
 *
 * This returns the string type of @c type. The returned string must be freed
 * by the called.
 *
 * @param type Type to convert
 * @return New string or NULL on memory allocation error
 */
char* modulecmd_argtype_to_str(modulecmd_arg_type_t *type);

MXS_END_DECLS
