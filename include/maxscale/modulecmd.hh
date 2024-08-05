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

/** What type of an action does the command perform? */
enum class CmdType
{
    READ,   /**< Command only displays data */
    WRITE   /**< Command can modify data */
};

constexpr uint8_t ARG_OPTIONAL = (1 << 0);      /**< The argument is optional */
/**< Argument value is a module instance and the instance type must match the command domain. */
constexpr uint8_t ARG_NAME_MATCHES_DOMAIN = (1 << 1);
}
}

/**
 * Argument descriptor. Defines the type and options for the command argument.
 */
struct ModuleCmdArgDesc
{
    ModuleCmdArgDesc() = default;
    ModuleCmdArgDesc(mxs::modulecmd::ArgType type, std::string desc);
    ModuleCmdArgDesc(mxs::modulecmd::ArgType type, uint8_t opts, std::string desc);

    bool    is_required() const;
    json_t* to_json() const;

    mxs::modulecmd::ArgType type {mxs::modulecmd::ArgType::NONE};
    uint8_t                 options {0};/**< Argument options */
    std::string             description;/**< Human-readable argument description, printed to rest-api */
};

/**
 * Describes an argument for a key-value style module command.
 */
struct KVModuleCmdArgDesc : public ModuleCmdArgDesc
{
    KVModuleCmdArgDesc(std::string name, mxs::modulecmd::ArgType type, std::string desc);
    KVModuleCmdArgDesc(std::string name, mxs::modulecmd::ArgType type, uint8_t opts, std::string desc);

    std::string name;
};

/** Argument value */
struct ModuleCmdArg
{
    mxs::modulecmd::ArgType type {mxs::modulecmd::ArgType::NONE};

    std::string     string;
    bool            boolean {false};
    SERVICE*        service {nullptr};
    SERVER*         server {nullptr};
    mxs::Monitor*   monitor {nullptr};
    MXS_FILTER_DEF* filter {nullptr};
};

/** Argument list */
using ModuleCmdArgs = std::vector<ModuleCmdArg>;

/** Argument list for commands that use key-value argument passing */
class KVModuleCmdArgs
{
public:
    void   add_arg(std::string name, ModuleCmdArg value);
    size_t size() const;

    // Argument value getters. Return the value or null/false/empty if argument is not defined.

    const ModuleCmdArg* get_arg(const std::string& name) const;
    std::string         get_string(const std::string& key) const;
    bool                get_bool(const std::string& key) const;
    SERVICE*            get_service(const std::string& key) const;
    SERVER*             get_server(const std::string& key) const;
    mxs::Monitor*       get_monitor(const std::string& key) const;
    MXS_FILTER_DEF*     get_filter(const std::string& key) const;

private:
    std::map<std::string, ModuleCmdArg> m_contents;
};

/**
 * The function signature for the module commands.
 *
 * The number of arguments passed to the function is at least the number of mandatory parameters.
 * Optional arguments are passed only if the argument value was provided by caller.
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
using ModuleCmdFn = bool (*)(const ModuleCmdArgs& argv, json_t** output);

/**
 * Function signature for module commands with key-value parameters. Returns success/false and json output.
 */
using KVModuleCmdFn = std::tuple<bool, mxb::Json> (*)(const KVModuleCmdArgs& args);

/**
 * A registered command. This base class contains fields shared by all module command types.
 */
class ModuleCmd
{
public:
    std::string             domain;             /**< Command domain */
    std::string             description;        /**< Command description */
    mxs::modulecmd::CmdType type;               /**< Command type, either read or write */

    ModuleCmd(std::string_view domain, mxs::modulecmd::CmdType type, std::string_view desc);
    virtual ~ModuleCmd() = default;

    /**
     * @brief Call a registered command
     *
     * There are no guarantees on the length of the call or whether it will block. All of this depends on the
     * module and what the command does.
     *
     * @param args   List of key-value arguments. Values may be empty if using positional arguments.
     * @param output JSON output of the called command
     *
     * @return True on success, false on error
     */
    virtual bool call(const mxs::KeyValueVector& args, json_t** cmd_output) const = 0;

    /**
     * Print command description to json.
     *
     * @param cmd_name Command name
     * @param host Hostname
     * @return Json data
     */
    virtual mxb::Json to_json(const std::string& cmd_name, const char* host) const = 0;

    /**
     * Test argument parsing. Used in the test_modulecmd unit test.
     *
     * @param args Arguments
     * @return -1 on error. Otherwise, number of parsed arguments.
     */
    virtual int test_arg_parse(const mxs::KeyValueVector& args) const = 0;

protected:
    /**
     * Print base class data to json.
     *
     * @param cmd_name Command name
     * @param host Hostname
     * @return Json data
     */
    json_t* base_json(const std::string& cmd_name, const char* host) const;
};

/**
 * Register a module command using positional arguments into the domain.
 *
 * @param domain      Command domain
 * @param identifier  The unique identifier for this command
 * @param type        Command type
 * @param entry_point The actual entry point function
 * @param args        Array of argument types
 * @param description Human-readable description of this command
 *
 * @return True if the module was successfully registered, false on error.
 */
bool modulecmd_register_command(std::string_view domain,
                                std::string_view identifier,
                                mxs::modulecmd::CmdType type,
                                ModuleCmdFn entry_point,
                                std::vector<ModuleCmdArgDesc> args,
                                std::string_view description);

/**
 * Register a module command using key-value arguments into the domain.
 */
bool modulecmd_register_kv_command(std::string_view domain,
                                   std::string_view identifier,
                                   mxs::modulecmd::CmdType type,
                                   KVModuleCmdFn entry_point,
                                   std::vector<KVModuleCmdArgDesc> args,
                                   std::string_view description);

/**
 * @brief Find a registered command
 *
 * @param domain Command domain
 * @param identifier Command identifier
 * @return Registered command or NULL if no command was found
 */
const ModuleCmd* modulecmd_find_command(const char* domain, const char* identifier);

/**
 * Print the module's commands as JSON
 *
 * @param module The module to print
 * @param host   The hostname to use
 *
 * @return The module's commands as JSON
 */
json_t* modulecmd_to_json(std::string_view module, const char* host);
