/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define TESTING_MASKINGRULES
#include "maskingrules.hh"
#include <iostream>
#include <maxbase/assert.hh>

using namespace std;

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
    {valid_minimal, true },
    {valid_maximal, true },
    {invalid1,      false},
    {invalid2,      false},
    {invalid3,      false},
    {invalid4,      false},
    {invalid5,      false},
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
        int rv = 0;

        for (size_t i = 0; i < nRule_tests; i++)
        {
            const rule_test& test = rule_tests[i];

            auto sRules = MaskingRules::parse(test.zJson);

            if ((sRules.get() && !test.valid) || (!sRules.get() && test.valid))
            {
                ++rv;
            }
        }

        return rv;
    }

    static int test_account_handling()
    {
        int rv = 0;

        using std::shared_ptr;
        auto sRules = MaskingRules::parse(valid_users);
        mxb_assert(sRules.get());

        const auto& rules = sRules->m_rules;
        mxb_assert(rules.size() == 1);

        const auto& sRule = rules[0];

        const auto& accounts = sRule->applies_to();
        mxb_assert(accounts.size() == nExpected_accounts);

        int j = 0;
        for (const auto& acc : accounts)
        {
            const expected_account& account = expected_accounts[j];

            string user = acc->user();

            if (user != account.zUser)
            {
                cout << j << ": Expected \"" << account.zUser << "\", got \"" << user << "\"." << endl;
                ++rv;
            }

            string host = acc->host();

            if (host != account.zHost)
            {
                cout << j << ": Expected \"" << account.zHost << "\", got \"" << host << "\"." << endl;
                ++rv;
            }

            ++j;
        }

        return rv;
    }

    static int test_account_matching()
    {
        int rv = 0;

        struct TestCase
        {
            const char* zAccount;
            const char* zSuccess;
            const char* zFailure;
        } test_cases[] =
        {
            {"'alice'@'127.0.0.%'", "127.0.0.42", "127.0.1.0"}
        };

        const int nTest_cases = sizeof(test_cases) / sizeof(test_cases[0]);

        for (int i = 0; i < nTest_cases; ++i)
        {
            auto& tc = test_cases[i];

            auto sAccount = MaskingRules::Rule::Account::create(tc.zAccount);
            mxb_assert(sAccount);

            if (!sAccount->matches("alice", tc.zSuccess))
            {
                cout << "Rule \"" << tc.zAccount << "\" did not match \"" << tc.zSuccess
                     << "\" although expected to." << endl;
                ++rv;
            }

            if (sAccount->matches("alice", tc.zFailure))
            {
                cout << "Rule \"" << tc.zAccount << "\" matched \"" << tc.zFailure
                     << "\" although not expected to." << endl;
                ++rv;
            }
        }

        return rv;
    }
};

int main()
{
    int rv = 0;

    if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_STDOUT))
    {
        rv += MaskingRulesTester::test_parsing();
        rv += MaskingRulesTester::test_account_handling();
        rv += MaskingRulesTester::test_account_matching();

        mxs_log_finish();
    }

    return rv == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
