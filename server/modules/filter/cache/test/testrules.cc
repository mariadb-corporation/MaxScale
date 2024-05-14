/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rules.hh"
#include <algorithm>
#include <iostream>
#include <maxbase/alloc.hh>
#include <maxscale/config.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/mariadbparser.hh>
#include "../../../../core/test/test_utils.hh"

using namespace std;

#if !defined (SS_DEBUG)
#define SS_DEBUG
#endif
#include <maxbase/assert.hh>

class CacheRules::Tester
{
public:
    static int test_all();

    static int test_user();
    static int test_store();
    static int test_array_store();
};

//
// Test user rules. Basically tests that a user specification is translated
// into the correct pcre2 regex.
//
struct user_test_case
{
    const char* json;
    struct
    {
        CacheRule::Op op;
        const char*   value;
    } expect;
};

#define USER_TEST_CASE(op_from, from, op_to, to)                                 \
    {"{ \"use\": [ { \"attribute\": \"user\", \"op\": \"" op_from "\", \"value\": \"" from "\" } ] }", \
     {op_to, to}}

#define COLUMN_

const struct user_test_case user_test_cases[] =
{
    USER_TEST_CASE("=", "bob",           CacheRule::Op::LIKE, "bob@.*"),
    USER_TEST_CASE("=", "'bob'",         CacheRule::Op::LIKE, "bob@.*"),
    USER_TEST_CASE("=", "bob@%",         CacheRule::Op::LIKE, "bob@.*"),
    USER_TEST_CASE("=", "'bob'@'%.52'",  CacheRule::Op::LIKE, "bob@.*\\.52"),
    USER_TEST_CASE("=", "bob@127.0.0.1", CacheRule::Op::EQ,   "bob@127.0.0.1"),
    USER_TEST_CASE("=", "b*b@127.0.0.1", CacheRule::Op::EQ,   "b*b@127.0.0.1"),
    USER_TEST_CASE("=", "b*b@%.0.0.1",   CacheRule::Op::LIKE, "b\\*b@.*\\.0\\.0\\.1"),
    USER_TEST_CASE("=", "b*b@%.0.%.1",   CacheRule::Op::LIKE, "b\\*b@.*\\.0\\..*\\.1"),
};

const size_t n_user_test_cases = sizeof(user_test_cases) / sizeof(user_test_cases[0]);

int CacheRules::Tester::test_user()
{
    int errors = 0;

    for (size_t i = 0; i < n_user_test_cases; ++i)
    {
        const struct user_test_case& test_case = user_test_cases[i];


        CacheConfig config("noconfig", nullptr);
        CacheRules::SVector sRule_vec = CacheRules::parse(&config, test_case.json);
        mxb_assert(sRule_vec);

        auto rules = *sRule_vec.get();

        for (size_t j = 0; j < rules.size(); ++j)
        {
            CacheRules::S sRules = rules[j];

            mxb_assert(!sRules->m_use_rules.empty());

            CacheRule* pRule = sRules->m_use_rules.front().get();

            if (pRule->op() != test_case.expect.op)
            {
                printf("%s\nExpected: %s,\nGot     : %s\n",
                       test_case.json,
                       CacheRule::to_string(test_case.expect.op),
                       CacheRule::to_string(pRule->op()));
                ++errors;
            }

            if (strcmp(pRule->value().c_str(), test_case.expect.value) != 0)
            {
                printf("%s\nExpected: %s,\nGot     : %s\n",
                       test_case.json,
                       test_case.expect.value,
                       pRule->value().c_str());
                ++errors;
            }
        }
    }

    return errors;
}

//
//
//
struct store_test_case
{
    const char* rule;       // The rule in JSON format.
    bool        matches;    // Whether or not the rule should match the query.
    const char* default_db; // The current default db.
    const char* query;      // The query to be matched against the rule.
};

#define STORE_TEST_CASE(attribute, op, value, matches, default_db, query) \
    {"{ \"store\": [ { \"attribute\": \"" attribute "\", \"op\": \"" op "\", \"value\": \"" value "\" } ] }", \
     matches, default_db, query}

// In the following,
//   true:  The query SHOULD match the rule,
//   false: The query should NOT match the rule.
const struct store_test_case store_test_cases[] =
{
    STORE_TEST_CASE("column",
                    "=",
                    "a",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("column",
                    "!=",
                    "a",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("column",
                    "=",
                    "b",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("column",
                    "!=",
                    "b",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("column",
                    "=",
                    "tbl.a",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("column",
                    "=",
                    "tbl.a",
                    true,
                    NULL,
                    "SELECT tbl.a FROM tbl"),

    STORE_TEST_CASE("column",
                    "like",
                    ".*a",
                    true,
                    NULL,
                    "SELECT a from tbl"),
    STORE_TEST_CASE("column",
                    "like",
                    ".*a",
                    true,
                    NULL,
                    "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column",
                    "like",
                    ".*a",
                    true,
                    NULL,
                    "SELECT db.tbl.a from tbl"),
    STORE_TEST_CASE("column",
                    "like",
                    ".*aa",
                    false,
                    NULL,
                    "SELECT a from tbl"),
    STORE_TEST_CASE("column",
                    "like",
                    ".*aa",
                    false,
                    NULL,
                    "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column",
                    "like",
                    ".*aa",
                    false,
                    NULL,
                    "SELECT db.tbl.a from tbl"),
    STORE_TEST_CASE("column",
                    "unlike",
                    ".*aa",
                    true,
                    NULL,
                    "SELECT a from tbl"),
    STORE_TEST_CASE("column",
                    "unlike",
                    ".*aa",
                    true,
                    NULL,
                    "SELECT tbl.a from tbl"),
    STORE_TEST_CASE("column",
                    "unlike",
                    ".*aa",
                    true,
                    NULL,
                    "SELECT db.tbl.a from tbl"),

    STORE_TEST_CASE("table",
                    "=",
                    "tbl",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("table",
                    "!=",
                    "tbl",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("table",
                    "=",
                    "tbl2",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("table",
                    "!=",
                    "tbl2",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("table",
                    "=",
                    "db.tbl",
                    true,
                    NULL,
                    "SELECT a from db.tbl"),
    STORE_TEST_CASE("table",
                    "=",
                    "db.tbl",
                    true,
                    "db",
                    "SELECT a from tbl"),
    STORE_TEST_CASE("table",
                    "!=",
                    "db.tbl",
                    false,
                    NULL,
                    "SELECT a from db.tbl"),
    STORE_TEST_CASE("table",
                    "!=",
                    "db.tbl",
                    false,
                    "db",
                    "SELECT a from tbl"),

    STORE_TEST_CASE("database",
                    "=",
                    "db",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("database",
                    "!=",
                    "db",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("database",
                    "=",
                    "db1",
                    true,
                    NULL,
                    "SELECT a FROM db1.tbl"),
    STORE_TEST_CASE("database",
                    "!=",
                    "db1",
                    false,
                    NULL,
                    "SELECT a FROM db1.tbl"),
    STORE_TEST_CASE("database",
                    "=",
                    "db1",
                    true,
                    "db1",
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("database",
                    "!=",
                    "db1",
                    false,
                    "db1",
                    "SELECT a FROM tbl"),

    STORE_TEST_CASE("query",
                    "=",
                    "SELECT a FROM tbl",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("query",
                    "!=",
                    "SELECT a FROM tbl",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("query",
                    "=",
                    "SELECT b FROM tbl",
                    false,
                    NULL,
                    "SELECT a FROM tbl"),
    STORE_TEST_CASE("query",
                    "!=",
                    "SELECT b FROM tbl",
                    true,
                    NULL,
                    "SELECT a FROM tbl"),
    // We are no longer able to distinguish selected columns
    // from one used in the WHERE-clause.
    STORE_TEST_CASE("column",
                    "=",
                    "a",
                    true,
                    NULL,
                    "SELECT b FROM tbl WHERE a = 5"),
    STORE_TEST_CASE("column",
                    "=",
                    "a",
                    true,
                    NULL,
                    "SELECT a, b FROM tbl WHERE a = 5"),
};

const int n_store_test_cases = sizeof(store_test_cases) / sizeof(store_test_cases[0]);

int CacheRules::Tester::test_store()
{
    int errors = 0;

    MariaDBParser& parser = MariaDBParser::get();
    for (int i = 0; i < n_store_test_cases; ++i)
    {
        printf("TC      : %d\n", (int)(i + 1));
        const struct store_test_case& test_case = store_test_cases[i];

        CacheConfig config("noconfig", nullptr);
        CacheRules::SVector sRules = CacheRules::parse(&config, test_case.rule);
        mxb_assert(sRules);

        auto& rules = *sRules.get();

        for (size_t j = 0; j < rules.size(); ++j)
        {
            CacheRules* pRules = rules[j].get();

            mxb_assert(!pRules->m_store_rules.empty());
            CacheRule* pRule = pRules->m_store_rules.front().get();

            GWBUF packet = mariadb::create_query(test_case.query);

            bool matches = pRules->should_store(parser, test_case.default_db, packet);

            if (matches != test_case.matches)
            {
                printf("Query   : %s\n"
                       "Rule    : %s\n"
                       "Def-db  : %s\n"
                       "Expected: %s\n"
                       "Result  : %s\n\n",
                       test_case.query,
                       test_case.rule,
                       test_case.default_db,
                       test_case.matches ? "A match" : "Not a match",
                       matches ? "A match" : "Not a match");
            }
        }
    }

    return errors;
}


static const char ARRAY_RULES[] =
    "["
    "  {"
    "    \"store\": ["
    "      {"
    "        \"attribute\": \"column\","
    "        \"op\":        \"=\","
    "        \"value\":     \"a\""
    "      }"
    "    ]"
    "  },"
    "  {"
    "    \"store\": ["
    "      {"
    "        \"attribute\": \"column\","
    "        \"op\":        \"=\","
    "        \"value\":     \"b\""
    "      }"
    "    ]"
    "  },"
    "  {"
    "    \"store\": ["
    "      {"
    "        \"attribute\": \"column\","
    "        \"op\":        \"=\","
    "        \"value\":     \"c\""
    "      }"
    "    ]"
    "  }"
    "]";

struct ARRAY_TEST_CASE
{
    const char* zStmt;  // Statement
    int32_t     index;  // Index of rule to match, -1 if none.
} array_test_cases[] =
{
    {
        "select a from tbl",
        0
    },
    {
        "select b from tbl",
        1
    },
    {
        "select c from tbl",
        2
    },
    {
        "select a, b from tbl",
        0
    },
    {
        "select d from tbl",
        -1
    }
};

const int n_array_test_cases = sizeof(array_test_cases) / sizeof(array_test_cases[0]);

typedef CacheRules::SCacheRules SCacheRules;

int CacheRules::Tester::test_array_store()
{
    int errors = 0;

    CacheConfig config("noconfig", nullptr);
    if (CacheRules::SVector sRules = CacheRules::parse(&config, ARRAY_RULES))
    {
        auto& rules = *sRules.get();

        for (int i = 0; i < n_array_test_cases; ++i)
        {
            const ARRAY_TEST_CASE& tc = array_test_cases[i];

            cout << tc.zStmt << endl;

            GWBUF stmt = mariadb::create_query(tc.zStmt);
            auto it = std::find_if(rules.begin(), rules.end(), [&](SCacheRules sR){
                return sR->should_store(MariaDBParser::get(), NULL, stmt);
            });

            int index = (it == rules.end()) ? -1 : std::distance(rules.begin(), it);

            if (it != rules.end())
            {
                if (tc.index == index)
                {
                    cout << "OK: Rule " << tc.index << " matches as expected." << endl;
                }
                else
                {
                    ++errors;
                    cout << "ERROR: Rule " << tc.index << " should have matched, but " << index << " did."
                         << endl;
                }
            }
            else
            {
                if (tc.index == -1)
                {
                    cout << "OK: No rule matched, as expected." << endl;
                }
                else
                {
                    ++errors;
                    cout << "ERROR: Rule " << tc.index << " should have matched, but none did." << endl;
                }
            }

            cout << endl;
        }
    }

    return errors;
}


int CacheRules::Tester::test_all()
{
    int errors = 0;

    errors += test_user();
    errors += test_store();
    errors += test_array_store();

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    int rc = EXIT_FAILURE;

    init_test_env();

    mxs::Config& config = mxs::Config::get();
    config.n_threads = 1;

    mxs::CachingParser::thread_init();
    MariaDBParser::get().plugin().thread_init();

    return CacheRules::Tester::test_all();
}
