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

#include <stdlib.h>
#include "rules.h"
#include <maxscale/log_manager.h>
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#include <maxscale/debug.h>

struct test_case
{
    const char* json;
    struct
    {
        cache_rule_op_t op;
        const char *value;
    } expect;
};

#define TEST_CASE(op_from, from, op_to, to)                                 \
{ "{ \"use\": [ { \"attribute\": \"user\", \"op\": \"" #op_from "\", \"value\": \"" #from "\" } ] }",\
    { op_to, #to } }

const struct test_case test_cases[] =
{
    TEST_CASE(=, bob,           CACHE_OP_LIKE, bob@.*),
    TEST_CASE(=, 'bob',         CACHE_OP_LIKE, bob@.*),
    TEST_CASE(=, bob@%,         CACHE_OP_LIKE, bob@.*),
    TEST_CASE(=, 'bob'@'%.52',  CACHE_OP_LIKE, bob@.*\\.52),
    TEST_CASE(=, bob@127.0.0.1, CACHE_OP_EQ,   bob@127.0.0.1),
    TEST_CASE(=, b*b@127.0.0.1, CACHE_OP_EQ,   b*b@127.0.0.1),
    TEST_CASE(=, b*b@%.0.0.1,   CACHE_OP_LIKE, b\\*b@.*\\.0\\.0\\.1),
    TEST_CASE(=, b*b@%.0.%.1,   CACHE_OP_LIKE, b\\*b@.*\\.0\\..*\\.1),
};

const size_t n_test_cases = sizeof(test_cases) / sizeof(test_cases[0]);

int test()
{
    int errors = 0;

    for (int i = 0; i < n_test_cases; ++i)
    {
        const struct test_case *test_case = &test_cases[i];

        CACHE_RULES *rules = cache_rules_parse(test_case->json, 0);
        ss_dassert(rules);

        CACHE_RULE *rule = rules->use_rules;
        ss_dassert(rule);

        if (rule->op != test_case->expect.op)
        {
            printf("%s\nExpected: %s,\nGot     : %s\n",
                   test_case->json,
                   cache_rule_op_to_string(test_case->expect.op),
                   cache_rule_op_to_string(rule->op));
            ++errors;
        }

        if (strcmp(rule->value, test_case->expect.value) != 0)
        {
            printf("%s\nExpected: %s,\nGot     : %s\n",
                   test_case->json,
                   test_case->expect.value,
                   rule->value);
            ++errors;
        }
    }

    return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main()
{
    int rc = EXIT_FAILURE;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        rc = test();

        mxs_log_finish();
    }
    else
    {
        printf("error: Could not initialize log.");
    }

    return rc;
}
