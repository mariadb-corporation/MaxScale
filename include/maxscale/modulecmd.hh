/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

/**
 * @file module_command.hh Module driven commands
 *
 * This header describes the structures and functions used to register new
 * functions for modules. It allows modules to introduce custom commands that
 * are registered into a module specific domain. These commands can then be
 * accessed from multiple different client interfaces without implementing the
 * same functionality again.
 */

#include <maxscale/ccdefs.hh>
#include <maxbase/jansson.hh>
#include <maxscale/dcb.hh>
#include <maxscale/filter.hh>
#include <maxscale/monitor.hh>
#include <maxscale/server.hh>
#include <maxscale/service.hh>
#include <maxscale/session.hh>

namespace maxscale
{
using KeyValueVector = std::vector<std::pair<std::string, std::string>>;
namespace modulecmd
{
enum class ArgType {NONE, STRING, BOOLEAN, SERVICE, SERVER, MONITOR, FILTER};

constexpr uint8_t ARG_OPTIONAL = (1 << 0);      /**< The argument is optional */
/**< Argument value is a module instance and the instance type must match the command domain. */
constexpr uint8_t ARG_NAME_MATCHES_DOMAIN = (1 << 1);
}
}

/**
 * Argument descriptor. Defines the type and options for the command argument.
 */
struct ModuleCmdArg
{
    ModuleCmdArg() = default;
    ModuleCmdArg(mxs::modulecmd::ArgType type, std::string desc);
    ModuleCmdArg(mxs::modulecmd::ArgType type, uint8_t opts, std::string desc);

    mxs::modulecmd::ArgType type {mxs::modulecmd::ArgType::NONE};
    uint8_t                 options {0};/**< Argument options */
    std::string             description;/**< Human-readable argument description, printed to rest-api */
};

/** What type of an action does the command perform? */
enum class ModuleCmdType
{
    READ,   /**< Command only displays data */
    WRITE   /**< Command can modify data */
};

/** Argument list node */
struct ModuleCmdArgValue
{
    mxs::modulecmd::ArgType type {mxs::modulecmd::ArgType::NONE};

    std::string     string;
    bool            boolean {false};
    SERVICE*        service {nullptr};
    SERVER*         server {nullptr};
    mxs::Monitor*   monitor {nullptr};
    MXS_FILTER_DEF* filter {nullptr};
};

bool modulecmd_arg_is_required(const ModuleCmdArg& t);

/** Argument list */
using MODULECMD_ARG = std::vector<ModuleCmdArgValue>;

/**
 * The function signature for the module commands.
 *
 * The number of arguments will always be the maximum number of arguments the
 * module requested. If an argument had the MODULECMD_ARG_OPTIONAL flag, and
 * the argument was not provided, the type of the argument will be
 * MODULECMD_ARG_NONE.
 *
 * If the module command produces output, it should be stored in the @c output
 * parameter as a json_t pointer. The output should conform as closely as possible
 * to the JSON API specification. The minimal requirement for a JSON API conforming
 * object is that it has a `meta` field. Ideally, the `meta` field should not
 * be used as it offloads the work to the client.
 *
 * @see http://jsonapi.org/format/
 *
 * @param argv   Argument list
 * @param output JSON formatted output from the command
 *
 * @return True on success, false on error
 */
typedef bool (* MODULECMDFN)(const MODULECMD_ARG& argv, json_t** output);

/**
 * A registered command
 */
struct MODULECMD
{
    std::string                       identifier;   /**< Unique identifier */
    std::string                       domain;       /**< Command domain */
    std::string                       description;  /**< Command description */
    ModuleCmdType                     type;         /**< Command type, either read or write */
    MODULECMDFN                       func;         /**< The registered function */
    int                               arg_count_min;/**< Minimum number of arguments */
    int                               arg_count_max;/**< Maximum number of arguments */
    std::vector<ModuleCmdArg>         arg_types;    /**< Argument types */
};

/**
 * @brief Register a new command
 *
 * This function registers a new command into the domain.
 *
 * @param domain      Command domain
 * @param identifier  The unique identifier for this command
 * @param type        Command type
 * @param entry_point The actual entry point function
 * @param args        Array of argument types of size @c argc
 * @param description Human-readable description of this command
 *
 * @return True if the module was successfully registered, false on error
 */
bool modulecmd_register_command(const char* domain,
                                const char* identifier,
                                ModuleCmdType type,
                                MODULECMDFN entry_point,
                                std::vector<ModuleCmdArg> args,
                                std::string_view description);

/**
 * @brief Find a registered command
 *
 * @param domain Command domain
 * @param identifier Command identifier
 * @return Registered command or NULL if no command was found
 */
const MODULECMD* modulecmd_find_command(const char* domain, const char* identifier);

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
 *
 * @param cmd Command for which the parameters are parsed
 * @param argc Number of arguments
 * @param argv Argument list in string format of size @c argc
 * @return Parsed arguments or NULL on error
 */
std::optional<MODULECMD_ARG> modulecmd_arg_parse(const MODULECMD* cmd, const mxs::KeyValueVector& argv);

/**
 * @brief Call a registered command
 *
 * This calls a registered command in a specific domain. There are no guarantees
 * on the length of the call or whether it will block. All of this depends on the
 * module and what the command does.
 *
 * @param cmd    Command to call
 * @param args   Parsed command arguments, pass NULL for no arguments
 * @param output JSON output of the called command, pass NULL to ignore output
 *
 * @return True on success, false on error
 */
bool modulecmd_call_command(const MODULECMD* cmd, const MODULECMD_ARG& args, json_t** output);

/**
 * Print the module's commands as JSON
 *
 * @param module The module to print
 * @param host   The hostname to use
 *
 * @return The module's commands as JSON
 */
json_t* modulecmd_to_json(std::string_view module, const char* host);
