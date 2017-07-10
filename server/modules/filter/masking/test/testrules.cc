/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define TESTING_MASKINGRULES
#include "maskingrules.hh"
#include <iostream>
#include <maxscale/debug.h>

using namespace std;
using namespace std::tr1;

const char valid_minimal[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "        \"column\": \"a\" "
    "      },"
    "      \"with\": {"
    "        \"value\": \"blah\" "
    "      }"
    "    },"
    "    {"
    "      \"obfuscate\": { "
    "        \"column\": \"b\" "
    "      }"
    "    }"
    "  ]"
    "}";

const char valid_maximal[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "        \"column\": \"a\", "
    "        \"table\": \"b\", "
    "        \"database\": \"c\" "
    "      },"
    "      \"with\": {"
    "        \"value\": \"blah\", "
    "        \"fill\": \"blah\" "
    "      },"
    "      \"applies_to\": ["
    "        \"'alice'@'host'\","
    "        \"'bob'@'%'\","
    "        \"'cecil'@'%.123.45.2'\""
    "      ],"
    "      \"exempted\": ["
    "        \"'admin'\""
    "      ]"
    "    },"
    "    {"
    "      \"obfuscate\": { "
    "        \"column\": \"c\", "
    "        \"table\": \"d\", "
    "        \"database\": \"e\" "
    "      }"
    "    }"
    "  ]"
    "}";

// Neither "obfuscate", nor "replace".
const char invalid1[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"applies_to\": ["
    "        \"'alice'@'host'\","
    "        \"'bob'@'%'\""
    "      ],"
    "      \"exempted\": ["
    "        \"'admin'\""
    "      ]"
    "    }"
    "  ]"
    "}";

// No "column" in "replace"
const char invalid2[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "      },"
    "      \"with\": { "
    "        \"value\": \"blah\" "
    "      }"
    "    }"
    "  ]"
    "}";

// No "value" or "fill" in "with"
/**
 * NOTE:
 * This test fails for ", " after column
 * and after "}," (Json parsing).
 *
 * If Json is ok the test doesn't fail at all.
 * The default 'fill' is used even if value is not set:
 *
 * void MaskingRules::ReplaceRule::rewrite(LEncString& s)
 *
 */
const char invalid3[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "        \"column\": \"a\", "
    "      },"
    "      \"with\": {"
    "      },"
    "    }"
    "  ]"
    "}";

// No "column" in "obfuscate"
const char invalid4[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"obfuscate\": { "
    "      }"
    "    }"
    "  ]"
    "}";

// No "with" in "replace"
const char invalid5[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "        \"column\": \"a\" "
    "      },"
    "      \"applies_to\": ["
    "        \"'alice'@'host'\","
    "        \"'bob'@'%'\""
    "      ],"
    "      \"exempted\": ["
    "        \"'admin'\""
    "      ]"
    "    }"
    "  ]"
    "}";

struct rule_test
{
    const char* zJson;
    bool        valid;
} rule_tests[] =
{
    { valid_minimal, true },
    { valid_maximal, true },
    { invalid1,      false },
    { invalid2,      false },
    { invalid3,      false },
    { invalid4,      false },
    { invalid5,      false },
};

const size_t nRule_tests = (sizeof(rule_tests) / sizeof(rule_tests[0]));

// Valid, lot's of users.
const char valid_users[] =
    "{"
    "  \"rules\": ["
    "    {"
    "      \"replace\": { "
    "        \"column\": \"a\" "
    "      },"
    "      \"with\": {"
    "        \"value\": \"blah\" "
    "      },"
    "      \"applies_to\": ["
    "        \"'alice'@'host'\","
    "        \"'bob'@'%'\","
    "        \"'cecil'@'%.123.45.2'\","
    "        \"'david'\","
    "        \"@'host'\""
    "      ],"
    "      \"exempted\": ["
    "        \"'admin'\""
    "      ]"
    "    }"
    "  ]"
    "}";

struct expected_account
{
    const char* zUser;
    const char* zHost;
} expected_accounts[] =
{
    {
        "alice",
        "host",
    },
    {
        "bob",
        ".*"
    },
    {
        "cecil",
        ".*\\.123\\.45\\.2"
    },
    {
        "david",
        ""
    },
    {
        "",
        "host"
    }
};

const size_t nExpected_accounts = (sizeof(expected_accounts) / sizeof(expected_accounts[0]));

class MaskingRulesTester
{
public:
    static int test_parsing()
    {
        int rc = EXIT_SUCCESS;

        for (size_t i = 0; i < nRule_tests; i++)
        {
            const rule_test& test = rule_tests[i];

            auto_ptr<MaskingRules> sRules = MaskingRules::parse(test.zJson);

            if ((sRules.get() && !test.valid) || (!sRules.get() && test.valid))
            {
                rc = EXIT_FAILURE;
            }
        }

        return rc;
    }

    static int test_account_handling()
    {
        int rc = EXIT_SUCCESS;

        using std::tr1::shared_ptr;
        auto_ptr<MaskingRules> sRules = MaskingRules::parse(valid_users);
        ss_dassert(sRules.get());

        const vector<shared_ptr<MaskingRules::Rule> >& rules = sRules->m_rules;
        ss_dassert(rules.size() == 1);

        shared_ptr<MaskingRules::Rule> sRule = rules[0];

        const vector<shared_ptr<MaskingRules::Rule::Account> >& accounts = sRule->applies_to();
        ss_dassert(accounts.size() == nExpected_accounts);

        int j = 0;
        for (vector<shared_ptr<MaskingRules::Rule::Account> >::const_iterator i = accounts.begin();
             i != accounts.end();
             ++i)
        {
            const expected_account& account = expected_accounts[j];

            string user = (*i)->user();

            if (user != account.zUser)
            {
                cout << j << ": Expected \"" << account.zUser << "\", got \"" << user << "\"." << endl;
                rc = EXIT_FAILURE;
            }

            string host = (*i)->host();

            if (host != account.zHost)
            {
                cout << j << ": Expected \"" << account.zHost << "\", got \"" << host << "\"." << endl;
                rc = EXIT_FAILURE;
            }

            ++j;
        }

        return rc;
    }
};

int main()
{
    int rc = EXIT_SUCCESS;

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_STDOUT))
    {
        rc = (MaskingRulesTester::test_parsing() == EXIT_FAILURE) ? EXIT_FAILURE : EXIT_SUCCESS;
        if (!rc)
        {
            rc = (MaskingRulesTester::test_account_handling() == EXIT_FAILURE) ? EXIT_FAILURE : EXIT_SUCCESS;
        }
    }

    return rc;
}
