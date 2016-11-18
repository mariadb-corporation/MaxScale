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
 * Test modulecmd.h functionality
 */

#include <maxscale/modulecmd.h>

#define TEST(a, b) do{if (!(a)){printf("%s:%d "b"\n", __FILE__, __LINE__);return 1;}}while(false)

static bool ok = false;

bool test_fn(const MODULECMD_ARG *arg)
{

    ok = (arg->argc == 2 && strcmp(arg->argv[0].value.string, "Hello") == 0 &&
          arg->argv[1].value.boolean);

    return true;
}

int test_arguments()
{
    const char *params1[] = {"Hello", "true"};
    const char *params2[] = {"Hello", "1"};

    const char *wrong_params1[] = {"Hi", "true"};
    const char *wrong_params2[] = {"Hello", "false"};

    const char *bad_params1[] = {"Hello", "World!"};
    const char *bad_params2[] = {"Hello", NULL};
    const char *bad_params3[] = {NULL, NULL};
    const char *bad_params4[] = {NULL, "World!"};

    const char *ns = "test_arguments";
    const char *id = "test_arguments";
    modulecmd_arg_type_t args1[] = {MODULECMD_ARG_STRING, MODULECMD_ARG_BOOLEAN};

    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    /**
     * Test command creation
     */

    TEST(modulecmd_find_command(ns, id) == NULL, "The registered command should not yet be found");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    TEST(modulecmd_register_command(ns, id, test_fn, 2, args1),
         "Registering a command should succeed");

    TEST(!modulecmd_register_command(ns, id, test_fn, 2, args1),
         "Registering the command a second time should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    const MODULECMD *cmd = modulecmd_find_command(ns, id);
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

    TEST(modulecmd_call_command(cmd, alist), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    ok = false;

    TEST(modulecmd_call_command(cmd, alist), "Second Module call should be successful");
    TEST(ok, "Function should receive right parameters");


    ok = false;
    modulecmd_arg_free(alist);

    TEST((alist = modulecmd_arg_parse(cmd, 2, params2)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist), "Module call should be successful");
    TEST(ok, "Function should receive right parameters");

    modulecmd_arg_free(alist);

    /**
     * Test valid but wrong arguments
     */
    TEST((alist = modulecmd_arg_parse(cmd, 2, wrong_params1)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(!ok, "Function should receive wrong parameters");
    modulecmd_arg_free(alist);

    TEST((alist = modulecmd_arg_parse(cmd, 2, wrong_params2)), "Arguments should be parsed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, alist), "Module call should be successful");
    TEST(!ok, "Function should receive wrong parameters");
    modulecmd_arg_free(alist);

    return 0;
}

bool test_fn2(const MODULECMD_ARG *arg)
{
    return true;
}

int test_optional_arguments()
{
    const char *params1[] = {"Hello", "true"};
    const char *params2[] = {NULL, "true"};
    const char *params3[] = {"Hello", NULL};
    const char *params4[] = {NULL, NULL};

    const char *ns = "test_optional_arguments";
    const char *id = "test_optional_arguments";
    modulecmd_arg_type_t args1[] =
    {
        MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
        MODULECMD_ARG_BOOLEAN | MODULECMD_ARG_OPTIONAL
    };

    TEST(modulecmd_register_command(ns, id, test_fn2, 2, args1),
         "Registering a command should succeed");

    const MODULECMD *cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    MODULECMD_ARG *arg = modulecmd_arg_parse(cmd, 2, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params2);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params3);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 2, params4);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 1, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 1, params2);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    arg = modulecmd_arg_parse(cmd, 0, params1);
    TEST(arg, "Parsing arguments should succeed");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    TEST(modulecmd_call_command(cmd, arg), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");
    modulecmd_arg_free(arg);

    TEST(modulecmd_call_command(cmd, NULL), "Module call should be successful");
    TEST(strlen(modulecmd_get_error()) == 0, "Error message should be empty");

    return 0;
}

bool test_fn3(const MODULECMD_ARG *arg)
{
    modulecmd_set_error("Something went wrong!");
    return false;
}

int test_module_errors()
{
    const char *ns = "test_module_errors";
    const char *id = "test_module_errors";

    TEST(modulecmd_register_command(ns, id, test_fn3, 0, NULL),
         "Registering a command should succeed");

    const MODULECMD *cmd = modulecmd_find_command(ns, id);
    TEST(cmd, "The registered command should be found");

    TEST(!modulecmd_call_command(cmd, NULL), "Module call should fail");
    TEST(strlen(modulecmd_get_error()), "Error message should not be empty");

    return 0;
}

int main(int argc, char **argv)
{
    int rc = 0;

    rc += test_arguments();
    rc += test_optional_arguments();
    rc += test_module_errors();

    return rc;
}
