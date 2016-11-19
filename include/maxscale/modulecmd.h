#pragma once
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
 * First 8 bits are reserved for argument type, bits 9 through 32 are reserved
 * for argument options and bits 33 through 64 are reserved for future use.
 */
typedef uint64_t modulecmd_arg_type_t;

/**
 * Argument types for the registered functions, the first 8 bits of
 * the modulecmd_arg_type_t type. An argument can be of only one type.
 */
#define MODULECMD_ARG_NONE    0
#define MODULECMD_ARG_STRING  1
#define MODULECMD_ARG_BOOLEAN 2
#define MODULECMD_ARG_SERVICE 3
#define MODULECMD_ARG_SERVER  4
#define MODULECMD_ARG_SESSION 5
#define MODULECMD_ARG_DCB     6
#define MODULECMD_ARG_MONITOR 7
#define MODULECMD_ARG_FILTER  8

/**
 * Options for arguments, bits 9 through 32
 */
#define MODULECMD_ARG_OPTIONAL (1 << 8) /**< The argument is optional */

/**
 * Helper macros
 */
#define MODULECMD_GET_TYPE(type) ((type & 0xff))
#define MODULECMD_ARG_IS_REQUIRED(type) ((type & MODULECMD_ARG_OPTIONAL) == 0)

/** Argument list node */
struct arg_node
{
    modulecmd_arg_type_t  type;
    union
    {
        char       *string;
        bool        boolean;
        SERVICE    *service;
        SERVER     *server;
        SESSION    *session;
        DCB        *dcb;
        MONITOR    *monitor;
        FILTER_DEF *filter;
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
 * @param cmd Command for which the parameters are parsed
 * @param argc Number of arguments
 * @param argv Argument list in string format of size @c argc
 * @return Parsed arguments or NULL on error
 */
MODULECMD_ARG* modulecmd_arg_parse(const MODULECMD *cmd, int argc, const char **argv);

/**
 * @brief Free parsed arguments returned by modulecmd_arg_parse
 * @param arg Arguments to free
 */
void modulecmd_arg_free(MODULECMD_ARG *arg);

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

MXS_END_DECLS
