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

/**
 * Test modulecmd.h functionality
 */

#include <maxbase/maxbase.hh>
#include <maxscale/alloc.h>
#include <maxscale/dcb.h>
#include <maxscale/paths.h>
#include <maxscale/modulecmd.h>
#include <maxscale/session.h>
#include <maxscale/json_api.h>

#include "../internal/monitor.h"

#define TEST(a, b) do {if (!(a)) {printf("%s:%d " b "\n", __FILE__, __LINE__); return 1;}} while (false)

static bool ok = false;

bool test_fn(const MODULECMD_ARG* arg, json_t** output)
{

    ok = (arg->argc == 2 && strcmp(arg->argv[0].value.string, "Hello") == 0
          && arg->argv[1].value.boolean);

    return true;
}

int test_arguments()
{
    const void* params1[] = {"Hello", "true"};
    const void* params2[] = {"Hello", "1"};

    const void* wrong_params1[] = {"Hi", "true"};
    const void* wrong_params2[] = {"Hello", "false"};

    const void* bad_params1[] = {"Hello", "World!"};
    const void* bad_params2[] = {"Hello", NULL};
    const void* bad_params3[] = {NULL, NULL};
    const void* bad_params4[] = {NULL, "World!"};

    const char* ns = "test_arguments";
    const char* id = "test_arguments";
    modulecmd_arg_type_t args1[] =
    {
        {MODULECMD_ARG_STRING,  ""},
        {MODULECMD_ARG_BOOLEAN, ""}
    };

    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    /**
     * Test command creation
     */

    TEST(modulecmd_find_command(ns, id) == NULL, "The registered command should not yet be found");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    TEST(modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, test_fn, 2, args1, ""),
         "Registering a command should succeed");

    TEST(!modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, test_fn, 2, args1, ""),
         "Registering the command a second time should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    const MODULECMD* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    /**
     * Test bad arguments
     */

    TEST(modulecmd_arg_parse(cmd, 0, NULL) == NULL, "Passing no arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(modulecmd_arg_parse(cmd, 1, params1) == NULL, "Passing one argument should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(modulecmd_arg_parse(cmd, 3, params1) == NULL, "Passing three arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    TEST(modulecmd_arg_parse(cmd, 2, bad_params1) == NULL, "Passing bad arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(modulecmd_arg_parse(cmd, 2, bad_params2) == NULL, "Passing bad arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(modulecmd_arg_parse(cmd, 2, bad_params3) == NULL, "Passing bad arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(modulecmd_arg_parse(cmd, 2, bad_params4) == NULL, "Passing bad arguments should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    /**
     * Test valid arguments
     */

    MODULECMD_ARG* alist = modulecmd_arg_parse(cmd, 2, params1);
    TEST(alist, "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    TEST(modulecmd_call_command(cmd, alist, NULL), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    ok = false;

    TEST(modulecmd_call_command(cmd, alist, NULL), "Second Module call should be successful");
    TEST(ok, "Function should receive right parameters");


    ok = false;
    modulecmd_arg_free(alist);

    TEST((alist = modulecmd_arg_parse(cmd, 2, params2)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist, NULL), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    modulecmd_arg_free(alist);

    /**
     * Test valid but wrong arguments
     */
    TEST((alist = modulecmd_arg_parse(cmd, 2, wrong_params1)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(!ok, "Function should receive wrong parameters");
    modulecmd_arg_free(alist);

    TEST((alist = modulecmd_arg_parse(cmd, 2, wrong_params2)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist, NULL), "Module call should be successful");
    TEST(!ok, "Function should receive wrong parameters");
    modulecmd_arg_free(alist);

    return 0;
}

bool test_fn2(const MODULECMD_ARG* arg, json_t** output)
{
    return true;
}

int test_optional_arguments()
{
    const void* params1[] = {"Hello", "true"};
    const void* params2[] = {NULL, "true"};
    const void* params3[] = {"Hello", NULL};
    const void* params4[] = {NULL, NULL};

    const char* ns = "test_optional_arguments";
    const char* id = "test_optional_arguments";
    modulecmd_arg_type_t args1[] =
    {
        {MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,  ""},
        {MODULECMD_ARG_BOOLEAN | MODULECMD_ARG_OPTIONAL, ""}
    };

    TEST(modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, test_fn2, 2, args1, ""),
         "Registering a command should succeed");

    const MODULECMD* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    MODULECMD_ARG* arg = modulecmd_arg_parse(cmd, 2, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params2);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params3);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params4);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 1, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 1, params2);
    TEST(arg, "Parsing arguments should succeed");
    TEST(arg->argc == 1, "Only one argument should be given");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 0, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(arg->argc == 0, "No arguments should be given");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    TEST(modulecmd_call_command(cmd, NULL, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    return 0;
}

bool test_fn3(const MODULECMD_ARG* arg, json_t** output)
{
    modulecmd_set_error("Something went wrong!");
    return false;
}

int test_module_errors()
{
    const char* ns = "test_module_errors";
    const char* id = "test_module_errors";

    TEST(modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, test_fn3, 0, NULL, ""),
         "Registering a command should succeed");

    const MODULECMD* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    TEST(!modulecmd_call_command(cmd, NULL, NULL), "Module call should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    return 0;
}

bool test_fn_map(const MODULECMD_ARG* arg, json_t** output)
{
    return true;
}

const char* map_dom = "test_map";

bool mapfn(const MODULECMD* cmd, void* data)
{
    int* i = (int*)data;
    (*i)++;
    return true;
}

int test_map()
{
    for (int i = 0; i < 10; i++)
    {
        char id[200];
        sprintf(id, "test_map%d", i + 1);
        TEST(modulecmd_register_command(map_dom, id, MODULECMD_TYPE_ACTIVE, test_fn_map, 0, NULL, ""),
             "Registering a command should succeed");
    }

    int n = 0;
    TEST(modulecmd_foreach(NULL, NULL, mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n >= 10, "Every registered command should be called");

    n = 0;
    TEST(modulecmd_foreach("test_map", NULL, mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n == 10, "Every registered command should be called");

    n = 0;
    TEST(modulecmd_foreach(NULL, "test_map", mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n == 10, "Every registered command should be called");

    n = 0;
    TEST(modulecmd_foreach("test_map", "test_map", mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n == 10, "Every registered command should be called");

    n = 0;
    TEST(modulecmd_foreach("wrong domain", "test_map", mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n == 0, "No registered command should be called");

    n = 0;
    TEST(modulecmd_foreach("test_map", "test_map[2-4]", mapfn, &n), "Mapping function should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(n == 3, "Three registered commands should be called");

    n = 0;
    TEST(!modulecmd_foreach("(", NULL, mapfn, &n), "Mapping function should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");
    TEST(n == 0, "No registered command should be called");

    return 0;
}

static DCB my_dcb;

bool ptrfn(const MODULECMD_ARG* argv, json_t** output)
{
    bool rval = false;

    if (argv->argc == 1 && argv->argv[0].value.dcb == &my_dcb)
    {
        rval = true;
    }

    return rval;
}

int test_pointers()
{
    const char* ns = "test_pointers";
    const char* id = "test_pointers";

    modulecmd_arg_type_t args[] =
    {
        {MODULECMD_ARG_DCB, ""}
    };

    TEST(modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, ptrfn, 1, args, ""),
         "Registering a command should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    const MODULECMD* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    const void* params[] = {&my_dcb};

    MODULECMD_ARG* arg = modulecmd_arg_parse(cmd, 1, params);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    modulecmd_arg_free(arg);
    return 0;
}

bool monfn(const MODULECMD_ARG* arg, json_t** output)
{
    return true;
}

int call_module(const MODULECMD* cmd, const char* ns)
{
    const void* params[] = {ns};

    MODULECMD_ARG* arg = modulecmd_arg_parse(cmd, 1, params);

    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    TEST(modulecmd_call_command(cmd, arg, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    modulecmd_arg_free(arg);

    return 0;
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
    const char* name = "My-Module";

    modulecmd_arg_type_t args[] =
    {
        {MODULECMD_ARG_MONITOR | MODULECMD_ARG_NAME_MATCHES_DOMAIN, ""}
    };

    TEST(modulecmd_register_command(actual_module, id, MODULECMD_TYPE_ACTIVE, monfn, 1, args, ""),
         "Registering a command should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    /** Create a monitor */
    char* libdir = MXS_STRDUP_A("../../modules/monitor/mariadbmon/");
    set_libdir(libdir);
    monitor_create(name, actual_module, NULL);

    const MODULECMD* cmd;

    // First invoke using the actual module name.
    cmd = modulecmd_find_command(actual_module, id);
    TEST(cmd, "The registered command should be found");

    TEST(call_module(cmd, name) == 0, "Invoking command should succeed");

    // Then invoke using the name used when loading.
    cmd = modulecmd_find_command(loaded_module, id);
    TEST(cmd, "The registered command should be found");

    TEST(call_module(cmd, name) == 0, "Invoking command should succeed");

    return 0;
}

bool outputfn(const MODULECMD_ARG* arg, json_t** output)
{
    json_t* obj = json_object();
    json_object_set_new(obj, "hello", json_string("world"));
    *output = obj;
    return output != NULL;
}

int test_output()
{
    const char* ns = "test_output";
    const char* id = "test_output";

    TEST(modulecmd_register_command(ns, id, MODULECMD_TYPE_ACTIVE, outputfn, 0, NULL, ""),
         "Registering a command should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    const MODULECMD* cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    json_t* output = NULL;

    TEST(modulecmd_call_command(cmd, NULL, &output), "Module call should be successful");
    TEST(output, "Output should be non-NULL");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(json_is_string(mxs_json_pointer(output, "/hello")), "Value should be correct");

    json_decref(output);

    TEST(modulecmd_call_command(cmd, NULL, NULL), "Module call with NULL output should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    return 0;
}

int main(int argc, char** argv)
{
    int rc = 0;

    mxs_log_init(NULL, NULL, MXS_LOG_TARGET_STDOUT);
    maxbase::init();

    rc += test_arguments();
    rc += test_optional_arguments();
    rc += test_module_errors();
    rc += test_map();
    rc += test_pointers();
    rc += test_domain_matching("mariadbmon", "mariadbmon", "test_domain_matching1");
    rc += test_domain_matching("mariadbmon", "mysqlmon", "test_domain_matching2");
    rc += test_output();

    maxbase::finish();
    mxs_log_finish();
    return rc;
}
