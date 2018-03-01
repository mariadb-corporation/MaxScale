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

// To ensure that ss_info_assert asserts also when builing in non-debug mode.
#ifndef SS_DEBUG
#define SS_DEBUG
#endif

#include "../config.c"

#define TEST(a) do{if(!(a)){printf("Error: `" #a "` was not true\n");return 1;}}while(false)

int test_validity()
{
    MXS_ENUM_VALUE enum_values[] =
    {
        {"a", (1 << 0)},
        {"b", (1 << 1)},
        {"c", (1 << 2)},
        {NULL}
    };

    MXS_MODULE_PARAM params[] =
    {
        {"p1", MXS_MODULE_PARAM_INT, "-123"},
        {"p2", MXS_MODULE_PARAM_COUNT, "123"},
        {"p3", MXS_MODULE_PARAM_BOOL, "true"},
        {"p4", MXS_MODULE_PARAM_STRING, "default"},
        {"p5", MXS_MODULE_PARAM_ENUM, "a", MXS_MODULE_OPT_NONE, enum_values},
        {"p6", MXS_MODULE_PARAM_PATH, "/tmp", MXS_MODULE_OPT_PATH_F_OK},
        {"p7", MXS_MODULE_PARAM_SERVICE, "my-service"},
        {"p8", MXS_MODULE_PARAM_ENUM, "a", MXS_MODULE_OPT_ENUM_UNIQUE, enum_values},
        {MXS_END_MODULE_PARAMS}
    };

    CONFIG_CONTEXT ctx = {.object = ""};

    /** Int parameter */
    TEST(config_param_is_valid(params, "p1", "1", &ctx));
    TEST(config_param_is_valid(params, "p1", "-1", &ctx));
    TEST(config_param_is_valid(params, "p1", "0", &ctx));
    TEST(!config_param_is_valid(params, "p1", "should not be OK", &ctx)); // String value for int, should fail

    /** Count parameter */
    TEST(config_param_is_valid(params, "p2", "1", &ctx));
    TEST(config_param_is_valid(params, "p2", "0", &ctx));
    TEST(!config_param_is_valid(params, "p2", "should not be OK", &ctx)); // String value for count, should fail
    TEST(!config_param_is_valid(params, "p2", "-1", &ctx)); // Negative values for count should fail

    /** Boolean parameter */
    TEST(config_param_is_valid(params, "p3", "1", &ctx));
    TEST(config_param_is_valid(params, "p3", "0", &ctx));
    TEST(config_param_is_valid(params, "p3", "true", &ctx));
    TEST(config_param_is_valid(params, "p3", "false", &ctx));
    TEST(config_param_is_valid(params, "p3", "yes", &ctx));
    TEST(config_param_is_valid(params, "p3", "no", &ctx));
    TEST(!config_param_is_valid(params, "p3", "maybe", &ctx));
    TEST(!config_param_is_valid(params, "p3", "perhaps", &ctx));
    TEST(!config_param_is_valid(params, "p3", "42", &ctx));
    TEST(!config_param_is_valid(params, "p3", "0.50", &ctx));

    /** String parameter */
    TEST(config_param_is_valid(params, "p4", "should be OK", &ctx));
    TEST(!config_param_is_valid(params, "p4", "", &ctx)); // Empty string is not OK

    /** Enum parameter */
    TEST(config_param_is_valid(params, "p5", "a", &ctx));
    TEST(config_param_is_valid(params, "p5", "b", &ctx));
    TEST(config_param_is_valid(params, "p5", "c", &ctx));
    TEST(config_param_is_valid(params, "p5", "a,b", &ctx));
    TEST(config_param_is_valid(params, "p5", "b,a", &ctx));
    TEST(config_param_is_valid(params, "p5", "a, b, c", &ctx));
    TEST(config_param_is_valid(params, "p5", "c,a,b", &ctx));
    TEST(!config_param_is_valid(params, "p5", "d", &ctx));
    TEST(!config_param_is_valid(params, "p5", "a,d", &ctx));
    TEST(!config_param_is_valid(params, "p5", "a,b,c,d", &ctx));

    /** Path parameter */
    TEST(config_param_is_valid(params, "p6", "/tmp", &ctx));
    TEST(!config_param_is_valid(params, "p6", "This is not a valid path", &ctx));

    /** Service parameter */
    CONFIG_CONTEXT svc = {.object = "test-service"};
    ctx.next = &svc;
    config_add_param(&svc, "type", "service");
    TEST(config_param_is_valid(params, "p7", "test-service", &ctx));
    TEST(!config_param_is_valid(params, "p7", "test-service", NULL));
    TEST(!config_param_is_valid(params, "p7", "no-such-service", &ctx));

    /** Unique enum parameter */
    TEST(config_param_is_valid(params, "p8", "a", &ctx));
    TEST(config_param_is_valid(params, "p8", "b", &ctx));
    TEST(config_param_is_valid(params, "p8", "c", &ctx));
    TEST(!config_param_is_valid(params, "p8", "a,b", &ctx));
    TEST(!config_param_is_valid(params, "p8", "b,a", &ctx));
    TEST(!config_param_is_valid(params, "p8", "a, b, c", &ctx));
    TEST(!config_param_is_valid(params, "p8", "c,a,b", &ctx));
    TEST(!config_param_is_valid(params, "p8", "d", &ctx));
    TEST(!config_param_is_valid(params, "p8", "a,d", &ctx));
    TEST(!config_param_is_valid(params, "p8", "a,b,c,d", &ctx));
    config_parameter_free(svc.parameters);
    return 0;
}

int test_add_parameter()
{
    MXS_ENUM_VALUE enum_values[] =
    {
        {"a", (1 << 0)},
        {"b", (1 << 1)},
        {"c", (1 << 2)},
        {NULL}
    };

    MXS_MODULE_PARAM params[] =
    {
        {"p1", MXS_MODULE_PARAM_INT, "-123"},
        {"p2", MXS_MODULE_PARAM_COUNT, "123"},
        {"p3", MXS_MODULE_PARAM_BOOL, "true"},
        {"p4", MXS_MODULE_PARAM_STRING, "default"},
        {"p5", MXS_MODULE_PARAM_ENUM, "a", MXS_MODULE_OPT_NONE, enum_values},
        {"p6", MXS_MODULE_PARAM_PATH, "/tmp", MXS_MODULE_OPT_PATH_F_OK},
        {"p7", MXS_MODULE_PARAM_SERVICE, "my-service"},
        {MXS_END_MODULE_PARAMS}
    };


    CONFIG_CONTEXT svc1 = {.object = "my-service"};
    CONFIG_CONTEXT svc2 = {.object = "some-service", .next = &svc1};
    CONFIG_CONTEXT ctx = {.object = "", .next = &svc2};
    config_add_param(&svc1, "type", "service");
    config_add_param(&svc2, "type", "service");

    config_add_defaults(&ctx, params);

    /** Test default values */
    TEST(config_get_integer(ctx.parameters, "p1") == -123);
    TEST(config_get_integer(ctx.parameters, "p2") == 123);
    TEST(config_get_bool(ctx.parameters, "p3") == true);
    TEST(strcmp(config_get_string(ctx.parameters, "p4"), "default") == 0);
    TEST(config_get_enum(ctx.parameters, "p5", enum_values) == 1);
    TEST(strcmp(config_get_string(ctx.parameters, "p6"), "/tmp") == 0);
    TEST(strcmp(config_get_string(ctx.parameters, "p7"), "my-service") == 0);

    config_parameter_free(ctx.parameters);
    ctx.parameters = NULL;

    /** Test custom parameters overriding default values */
    config_add_param(&ctx, "p1", "-321");
    config_add_param(&ctx, "p2", "321");
    config_add_param(&ctx, "p3", "false");
    config_add_param(&ctx, "p4", "strange");
    config_add_param(&ctx, "p5", "a,c");
    config_add_param(&ctx, "p6", "/dev/null");
    config_add_param(&ctx, "p7", "some-service");

    config_add_defaults(&ctx, params);

    TEST(config_get_integer(ctx.parameters, "p1") == -321);
    TEST(config_get_integer(ctx.parameters, "p2") == 321);
    TEST(config_get_param(ctx.parameters, "p3") && config_get_bool(ctx.parameters, "p3") == false);
    TEST(strcmp(config_get_string(ctx.parameters, "p4"), "strange") == 0);
    int val = config_get_enum(ctx.parameters, "p5", enum_values);
    TEST(val == 5);
    TEST(strcmp(config_get_string(ctx.parameters, "p6"), "/dev/null") == 0);
    TEST(strcmp(config_get_string(ctx.parameters, "p7"), "some-service") == 0);
    config_parameter_free(ctx.parameters);
    config_parameter_free(svc1.parameters);
    config_parameter_free(svc2.parameters);
    return 0;
}

int test_required_parameters()
{
    MXS_MODULE_PARAM params[] =
    {
        {"p1", MXS_MODULE_PARAM_INT, "-123", MXS_MODULE_OPT_REQUIRED},
        {"p2", MXS_MODULE_PARAM_COUNT, "123", MXS_MODULE_OPT_REQUIRED},
        {"p3", MXS_MODULE_PARAM_BOOL, "true", MXS_MODULE_OPT_REQUIRED},
        {MXS_END_MODULE_PARAMS}
    };

    CONFIG_CONTEXT ctx = {.object = ""};

    TEST(missing_required_parameters(params, ctx.parameters));
    config_add_defaults(&ctx, params);
    TEST(!missing_required_parameters(params, ctx.parameters));

    config_parameter_free(ctx.parameters);
    ctx.parameters = NULL;

    config_add_param(&ctx, "p1", "1");
    config_add_param(&ctx, "p2", "1");
    config_add_param(&ctx, "p3", "1");
    TEST(!missing_required_parameters(params, ctx.parameters));
    config_parameter_free(ctx.parameters);
    return 0;
}

int main(int argc, char **argv)
{
    int result = 0;

    result += test_validity();
    result += test_add_parameter();
    result += test_required_parameters();

    return result;
}
