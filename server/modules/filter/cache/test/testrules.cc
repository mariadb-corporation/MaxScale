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

#include <stdlib.h>
#include "rules.h"
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>
#if !defined(SS_DEBUG)
#define SS_DEBUG
#endif
#include <maxscale/debug.h>

GWBUF* create_gwbuf(const char* s)
{
    size_t query_len = strlen(s);
    size_t payload_len = query_len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);
    ss_dassert(gwbuf);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + MYSQL_HEADER_LEN + 1, s, query_len);

    return gwbuf;
}

//
// Test user rules. Basically tests that a user specification is translated
// into the correct pcre2 regex.
//
struct user_test_case
{
    const char* json;
    struct
    {
        cache_rule_op_t op;
        const char *value;
    } expect;
};

#define USER_TEST_CASE(op_from, from, op_to, to)                                 \
{ "{ \"use\": [ { \"attribute\": \"user\", \"op\": \"" #op_from "\", \"value\": \"" #from "\" } ] }",\
    { op_to, #to } }

#define COLUMN_

const struct user_test_case user_test_cases[] =
{
    USER_TEST_CASE(=, bob,           CACHE_OP_LIKE, bob@.*),
    USER_TEST_CASE(=, 'bob',         CACHE_OP_LIKE, bob@.*),
    USER_TEST_CASE(=, bob@%,         CACHE_OP_LIKE, bob@.*),
    USER_TEST_CASE(=, 'bob'@'%.52',  CACHE_OP_LIKE, bob@.*\\.52),
    USER_TEST_CASE(=, bob@127.0.0.1, CACHE_OP_EQ,   bob@127.0.0.1),
    USER_TEST_CASE(=, b*b@127.0.0.1, CACHE_OP_EQ,   b*b@127.0.0.1),
    USER_TEST_CASE(=, b*b@%.0.0.1,   CACHE_OP_LIKE, b\\*b@.*\\.0\\.0\\.1),
    USER_TEST_CASE(=, b*b@%.0.%.1,   CACHE_OP_LIKE, b\\*b@.*\\.0\\..*\\.1),
};

const size_t n_user_test_cases = sizeof(user_test_cases) / sizeof(user_test_cases[0]);

int test_user()
{
    int errors = 0;

    for (size_t i = 0; i < n_user_test_cases; ++i)
    {
        const struct user_test_case *test_case = &user_test_cases[i];

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

        cache_rules_free(rules);
    }

    return errors;
}

//
//
//
struct store_test_case
{
    const char *rule;       // The rule in JSON format.
    bool matches;           // Whether or not the rule should match the query.
    const char *default_db; // The current default db.
    const char *query;      // The query to be matched against the rule.
};

#define STORE_TEST_CASE(attribute, op, value, matches, default_db, query) \
{ "{ \"store\": [ { \"attribute\": \"" attribute "\", \"op\": \"" op "\", \"value\": \"" value "\" } ] }",\
        matches, default_db, query }

// In the following,
//   true:  The query SHOULD match the rule,
//   false: The query should NOT match the rule.
const struct store_test_case store_test_cases[] =
{
    STORE_TEST_CASE("column", "=",  "a",     true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("column", "!=", "a",     false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("column", "=",  "b",     false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("column", "!=", "b",     true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("column", "=",  "tbl.a", true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("column", "=",  "tbl.a", true,  NULL, "SELECT tbl.a FROM tbl"),

    STORE_TEST_CASE("column", "like", ".*a",  true,  NULL, "SELECT a from tbl"),
    STORE_TEST_CASE("column", "like", ".*a",  true,  NULL, "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column", "like", ".*a",  true,  NULL, "SELECT db.tbl.a from tbl"),
    STORE_TEST_CASE("column", "like", ".*aa", false, NULL, "SELECT a from tbl"),
    STORE_TEST_CASE("column", "like", ".*aa", false, NULL, "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column", "like", ".*aa", false, NULL, "SELECT db.tbl.a from tbl"),
    STORE_TEST_CASE("column", "unlike", ".*aa", true, NULL, "SELECT a from tbl"),
    STORE_TEST_CASE("column", "unlike", ".*aa", true, NULL, "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column", "unlike", ".*aa", true, NULL, "SELECT db.tbl.a from tbl"),

    STORE_TEST_CASE("table", "=",   "tbl",    true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("table", "!=",  "tbl",    false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("table", "=",   "tbl2",   false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("table", "!=",  "tbl2",   true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("table", "=",   "db.tbl", true,  NULL, "SELECT a from db.tbl"),
    STORE_TEST_CASE("table", "=",   "db.tbl", true,  "db", "SELECT a from tbl"),
    STORE_TEST_CASE("table", "!=",  "db.tbl", false, NULL, "SELECT a from db.tbl"),
    STORE_TEST_CASE("table", "!=",  "db.tbl", false, "db", "SELECT a from tbl"),

    STORE_TEST_CASE("database", "=",  "db",    false, NULL,  "SELECT a FROM tbl"),
    STORE_TEST_CASE("database", "!=", "db",    true,  NULL,  "SELECT a FROM tbl"),
    STORE_TEST_CASE("database", "=",  "db1",   true,  NULL,  "SELECT a FROM db1.tbl"),
    STORE_TEST_CASE("database", "!=", "db1",   false, NULL,  "SELECT a FROM db1.tbl"),
    STORE_TEST_CASE("database", "=",  "db1",   true,  "db1", "SELECT a FROM tbl"),
    STORE_TEST_CASE("database", "!=", "db1",   false, "db1", "SELECT a FROM tbl"),

    STORE_TEST_CASE("query", "=",  "SELECT a FROM tbl", true,  NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("query", "!=", "SELECT a FROM tbl", false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("query", "=",  "SELECT b FROM tbl", false, NULL, "SELECT a FROM tbl"),
    STORE_TEST_CASE("query", "!=", "SELECT b FROM tbl", true,  NULL, "SELECT a FROM tbl"),

    STORE_TEST_CASE("column", "=", "a", false, NULL, "SELECT b FROM tbl WHERE a = 5"),
    STORE_TEST_CASE("column", "=", "a", true,  NULL, "SELECT a, b FROM tbl WHERE a = 5"),
};

const size_t n_store_test_cases = sizeof(store_test_cases) / sizeof(store_test_cases[0]);

int test_store()
{
    int errors = 0;

    for (size_t i = 0; i < n_store_test_cases; ++i)
    {
        printf("TC      : %d\n", (int)(i + 1));
        const struct store_test_case *test_case = &store_test_cases[i];

        CACHE_RULES *rules = cache_rules_parse(test_case->rule, 0);
        ss_dassert(rules);

        CACHE_RULE *rule = rules->store_rules;
        ss_dassert(rule);

        GWBUF *packet = create_gwbuf(test_case->query);

        bool matches = cache_rules_should_store(rules, 0, test_case->default_db, packet);

        if  (matches != test_case->matches)
        {
            printf("Query   : %s\n"
                   "Rule    : %s\n"
                   "Def-db  : %s\n"
                   "Expected: %s\n"
                   "Result  : %s\n\n",
                   test_case->query,
                   test_case->rule,
                   test_case->default_db,
                   test_case->matches ? "A match" : "Not a match",
                   matches ? "A match" : "Not a match");
        }

        gwbuf_free(packet);

        cache_rules_free(rules);
    }

    return errors;
}


int test()
{
    int errors = 0;

    errors += test_user();
    errors += test_store();

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main()
{
    int rc = EXIT_FAILURE;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        MXS_CONFIG* pConfig = config_get_global_options();
        pConfig->n_threads = 1;

        set_libdir(MXS_STRDUP_A("../../../../../query_classifier/qc_sqlite/"));
        if (qc_setup("qc_sqlite", "") && qc_process_init(QC_INIT_BOTH))
        {
            set_libdir(MXS_STRDUP_A("../"));
            rc = test();

            qc_process_end(QC_INIT_BOTH);
        }
        else
        {
            MXS_ERROR("Could not initialize query classifier.");
        }

        mxs_log_finish();
    }
    else
    {
        printf("error: Could not initialize log.");
    }

    return rc;
}
