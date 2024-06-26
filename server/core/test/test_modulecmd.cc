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

/**
 * Test modulecmd.h functionality
 */

#include <maxbase/maxbase.hh>
#include <maxscale/cn_strings.hh>
#include <maxscale/dcb.hh>
#include <maxscale/json_api.hh>
#include <maxscale/mainworker.hh>
#include <maxscale/modulecmd.hh>
#include <maxscale/paths.hh>
#include <maxscale/session.hh>

#include "test_utils.hh"
#include "../internal/monitormanager.hh"

#define TEST(a, b) do {if (!(a)) {printf("%s:%d " b "\n", __FILE__, __LINE__); return 1;}} while (false)
#define EXPECT_CMD_FOUND "The registered command should be found"

using namespace mxs::modulecmd;

namespace
{
mxs::KeyValueVector param_helper(const std::vector<std::string>& values)
{
    mxs::KeyValueVector rval;
    for (auto& v : values)
    {
        rval.emplace_back(v, "");
    }
    return rval;
}
bool ok = false;
int errors = 0;

bool log_redirect(int level, std::string_view msg)
{
    if (level < LOG_WARNING)    // Less is more.
    {
        ++errors;
    }

    return false;
}

bool errors_logged()
{
    bool rv = errors != 0;
    errors = 0;
    return rv;
}

int assume_no_errors()
{
    TEST(!errors_logged(), "Error message should be empty");
    return 0;
}

int assume_errors()
{
    TEST(errors_logged(), "Error message should not be empty");
    return 0;
}

bool test_fn(const ModuleCmdArgs& arg, json_t** output)
{
    ok = (arg.size() == 2 && arg[0].string == "Hello" && arg[1].boolean);
    return true;
}

int test_arguments()
{
    const char* ns = "test_arguments";
    const char* id = "test_arguments";
    std::vector<ModuleCmdArgDesc> args1 =
    {
        {ArgType::STRING,  ""},
        {ArgType::BOOLEAN, ""}
    };

    int rval = 0;
    rval += assume_no_errors();

    /**
     * Test command creation
     */

    TEST(modulecmd_find_command(ns, id) == NULL, "The registered command should not yet be found");
    rval += assume_errors();

    TEST(modulecmd_register_command(ns, id, CmdType::WRITE, test_fn, args1, "test"),
         "Registering a command should succeed");

    rval += assume_no_errors();

    const ModuleCmd* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    /**
     * Test bad arguments
     */
    auto test_bad_arguments = [cmd](const mxs::KeyValueVector& argv){
        auto n_args = cmd->test_arg_parse(argv);
        TEST(n_args < 0, "Parsing arguments should fail");
        return 0;
    };

    TEST(cmd->test_arg_parse({}) < 0, "Passing no arguments should fail");
    rval += assume_errors();

    rval += test_bad_arguments(param_helper({"Hello"}));
    rval += assume_errors();
    rval += test_bad_arguments(param_helper({"Hello", "true", "something"}));
    rval += assume_errors();

    rval += test_bad_arguments(param_helper({"Hello", "World!"}));
    rval += assume_errors();
    rval += test_bad_arguments(param_helper({"Hello", ""}));
    rval += assume_errors();
    rval += test_bad_arguments(param_helper({"", ""}));
    rval += assume_errors();
    rval += test_bad_arguments(param_helper({"", "World!"}));
    rval += assume_errors();

    /**
     * Test valid arguments
     */

    json_t* dummy = nullptr;
    auto arg_vector = param_helper({"Hello", "true"});
    TEST(cmd->call(arg_vector, &dummy), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    ok = false;

    TEST(cmd->call(arg_vector, &dummy), "Second Module call should be successful");
    TEST(ok, "Function should receive right parameters");


    ok = false;

    TEST(cmd->call(param_helper({"Hello", "1"}), &dummy), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    /**
     * Test valid but wrong arguments
     */
    auto test_valid_but_wrong = [cmd](const mxs::KeyValueVector& argv){
        int ret = 0;
        json_t* dummy2 = nullptr;
        TEST(cmd->call(argv, &dummy2), "Module call should be successful");
        ret += assume_no_errors();
        TEST(!ok, "Function should receive wrong parameters");
        return ret;
    };

    rval += test_valid_but_wrong(param_helper({"Hi", "true"}));
    rval += test_valid_but_wrong(param_helper({"Hello", "false"}));
    return rval;
}

bool test_fn2(const ModuleCmdArgs& arg, json_t** output)
{
    return true;
}

int test_optional_arguments()
{
    const auto params1 = param_helper({"Hello", "true"});
    const auto params2 = param_helper({"", "true"});
    const auto params3 = param_helper({"Hello", ""});
    const auto params4 = param_helper({"", ""});

    const char* ns = "test_optional_arguments";
    const char* id = "test_optional_arguments";
    std::vector<ModuleCmdArgDesc> args1 =
    {
        {ArgType::STRING,  ARG_OPTIONAL, ""},
        {ArgType::BOOLEAN, ARG_OPTIONAL, ""}
    };

    TEST(modulecmd_register_command(ns, id, CmdType::WRITE, test_fn2, args1, "test"),
         "Registering a command should succeed");

    const ModuleCmd* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    auto test_cmd_params = [cmd](const mxs::KeyValueVector& params) {
        int rval = 0;
        int n_args = cmd->test_arg_parse(params);
        TEST(n_args >= 0, "Parsing arguments should succeed");
        TEST(n_args == (int)params.size(), "Wrong number of arguments");
        rval += assume_no_errors();
        json_t* dummy = nullptr;
        TEST(cmd->call(params, &dummy), "Module call should be successful");
        rval += assume_no_errors();
        return rval;
    };

    int rval = 0;
    rval += test_cmd_params(params1);
    rval += test_cmd_params(params2);
    rval += test_cmd_params(params3);
    rval += test_cmd_params(params4);
    rval += test_cmd_params(param_helper({"true"}));
    rval += test_cmd_params({});

    json_t* dummy = nullptr;
    TEST(cmd->call({}, &dummy), "Module call should be successful");
    rval += assume_no_errors();
    return rval;
}

bool test_fn3(const ModuleCmdArgs& arg, json_t** output)
{
    MXB_ERROR("Something went wrong!");
    return false;
}

int test_module_errors()
{
    int rval = 0;
    const char* ns = "test_module_errors";
    const char* id = "test_module_errors";

    TEST(modulecmd_register_command(ns, id, CmdType::WRITE, test_fn3, {}, "test"),
         "Registering a command should succeed");

    const ModuleCmd* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    json_t* dummy = nullptr;
    TEST(!cmd->call({}, &dummy), "Module call should fail");
    rval += assume_errors();

    return rval;
}

bool monfn(const ModuleCmdArgs& arg, json_t** output)
{
    return true;
}

int call_module(const ModuleCmd* cmd, const char* ns)
{
    int rval = 0;
    const auto params = param_helper({ns});
    json_t* dummy = nullptr;
    TEST(cmd->call(params, &dummy), "Module call should be successful");
    rval += assume_no_errors();
    return rval;
}

/**
 * Load a module from ../../modules/monitor/mariadbmon and invoke a command.
 *
 * @param actual_module   The actual name of the module; the name of the module
 *                        that exists as a physcal file, i.e. mariadbmon.
 * @param loaded_module   The name of the module as referred to in the configuration
 *                        file, i.e. mysqlmon or mariadbmon.
 * @id                    The id of the command; unique for each invocation.
 *
 * @return 0 if successful, 1 otherwise.
 */
int test_domain_matching(const char* actual_module,
                         const char* loaded_module,
                         const char* id)
{
    int rval = 0;
    const char* name = "My-Module";

    std::vector<ModuleCmdArgDesc> args =
    {
        {ArgType::MONITOR, ARG_NAME_MATCHES_DOMAIN, ""}
    };

    TEST(modulecmd_register_command(actual_module, id, CmdType::WRITE, monfn, args, "test"),
         "Registering a command should succeed");
    rval += assume_no_errors();

    /** Create a monitor */
    mxs::set_libdir("../../modules/monitor/mariadbmon/");
    mxs::ConfigParameters params;
    params.set("module", actual_module);
    params.set("monitor_interval", "1s");
    params.set("backend_connect_timeout", "1s");
    params.set("backend_read_timeout", "1s");
    params.set("backend_write_timeout", "1s");
    params.set("journal_max_age", "1s");
    params.set("script_timeout", "1s");
    params.set(CN_DISK_SPACE_CHECK_INTERVAL, "1s");
    params.set("failover_timeout", "1s");
    params.set("switchover_timeout", "1s");
    params.set("master_failure_timeout", "1s");
    params.set(CN_USER, "dummy");
    params.set(CN_PASSWORD, "dummy");
    MonitorManager::create_monitor(name, actual_module, &params);

    const ModuleCmd* cmd;

    // First invoke using the actual module name.
    cmd = modulecmd_find_command(actual_module, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    TEST(call_module(cmd, name) == 0, "Invoking command should succeed");

    // Then invoke using the name used when loading.
    cmd = modulecmd_find_command(loaded_module, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    TEST(call_module(cmd, name) == 0, "Invoking command should succeed");

    MonitorManager::destroy_all_monitors();

    return rval;
}

bool outputfn(const ModuleCmdArgs& arg, json_t** output)
{
    json_t* obj = json_object();
    json_object_set_new(obj, "hello", json_string("world"));
    *output = obj;
    return output != NULL;
}

int test_output()
{
    int rval = 0;
    const char* ns = "test_output";
    const char* id = "test_output";

    TEST(modulecmd_register_command(ns, id, CmdType::WRITE, outputfn, {}, "test"),
         "Registering a command should succeed");
    rval += assume_no_errors();

    const ModuleCmd* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, EXPECT_CMD_FOUND);

    json_t* output = NULL;

    TEST(cmd->call({}, &output), "Module call should be successful");
    TEST(output, "Output should be non-NULL");
    rval += assume_no_errors();
    TEST(json_is_string(mxb::json_ptr(output, "/hello")), "Value should be correct");

    json_decref(output);
    return rval;
}

const char string_arg_name[] = "string-arg";
const char bool_arg_name[] = "optional-bool-arg";
bool function_entered = false;

std::tuple<bool, mxb::Json> key_value_func(const KVModuleCmdArgs& args)
{
    function_entered = true;
    bool rv = true;

    mxb::Json out(mxb::Json::Type::OBJECT);
    auto it = args.find(string_arg_name);
    if (it == args.end())
    {
        rv = false;
        printf("'%s' not found.\n", string_arg_name);
    }
    else
    {
        out.set_string(string_arg_name, it->second.string);
    }

    it = args.find(bool_arg_name);
    if (it != args.end())
    {
        out.set_bool(bool_arg_name, it->second.boolean);
    }

    return {rv, std::move(out)};
}

int test_kv_arguments()
{
    int rval = 0;
    const char domain[] = "test_domain";
    const char command_name[] = "key_value_cmd";

    modulecmd_register_kv_command(domain, command_name, CmdType::WRITE, key_value_func,
                                  {{string_arg_name, ArgType::STRING, "string"},
                                   {bool_arg_name, ArgType::BOOLEAN, ARG_OPTIONAL, "boolean"}},
                                  "key-value-test");

    const ModuleCmd* cmd = modulecmd_find_command(domain, command_name);
    TEST(cmd, EXPECT_CMD_FOUND);

    std::vector<mxs::KeyValueVector> bad_arg_sets = {
        {{}},
        {{"key", "value"}},
        {{bool_arg_name, "true"}},
        {{string_arg_name, "a"}, {bool_arg_name, "true"}, {"key", "value"}},
        {{string_arg_name, "a"}, {bool_arg_name, "not boolean"}},
        {{string_arg_name, "a"}, {bool_arg_name, "true"}, {bool_arg_name, "true"}},
    };

    json_t* dummy = nullptr;
    for (const auto& bad_args : bad_arg_sets)
    {
        function_entered = false;
        cmd->call(bad_args, &dummy);
        if (function_entered)
        {
            rval++;
            printf("Called function with invalid parameters\n");
            function_entered = false;
        }
    }

    if (!rval)
    {
        auto test_good_args = [&rval, cmd](const mxs::KeyValueVector& args, const std::string& expected_str,
                                           bool* expected_bool_ptr) {
            json_t* output = nullptr;
            bool ret = cmd->call(args, &output);
            if (!ret)
            {
                rval++;
                printf("Call with good arguments failed.\n");
            }
            else if (!output)
            {
                rval++;
                printf("Call with good arguments produced no output.\n");
            }
            else
            {
                mxb::Json out(output, mxb::Json::RefType::STEAL);

                std::string found_str;
                bool found = out.try_get_string(string_arg_name, &found_str);
                if (!found)
                {
                    rval++;
                    printf("Expected output field not found.\n");
                }
                else if (found_str != expected_str)
                {
                    rval++;
                    printf("Found wrong string value '%s'.\n", found_str.c_str());
                }

                if (expected_bool_ptr)
                {
                    bool found_bool = false;
                    found = out.try_get_bool(bool_arg_name, &found_bool);
                    if (!found)
                    {
                        rval++;
                        printf("Expected output field not found.\n");
                    }
                    else if (found_bool != *expected_bool_ptr)
                    {
                        rval++;
                        printf("Found wrong boolean value '%i'.\n", found_bool);
                    }
                }
            }
        };

        std::string expected_str = "abc";
        test_good_args({{string_arg_name, expected_str}}, expected_str, nullptr);

        expected_str = "eFd";
        bool expected_bool = true;
        test_good_args({{string_arg_name, expected_str}, {bool_arg_name, "true"}},
                       expected_str, &expected_bool);

        expected_str = "foo";
        expected_bool = false;
        test_good_args({{bool_arg_name, "false"}, {string_arg_name, expected_str}},
                       expected_str, &expected_bool);
    }

    return rval;
}
}

int main()
{
    int rc = 0;

    run_unit_test([&]() {
        mxb::LogRedirect redirect(log_redirect);

        rc += test_arguments();
        rc += test_optional_arguments();
        rc += test_module_errors();
        rc += test_domain_matching("mariadbmon", "mariadbmon", "test_domain_matching1");
        rc += test_domain_matching("mariadbmon", "mysqlmon", "test_domain_matching2");
        rc += test_output();
        rc += test_kv_arguments();
    });

    return rc;
}
