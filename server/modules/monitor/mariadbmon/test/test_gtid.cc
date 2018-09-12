/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "../gtid.hh"
#include "maxbase/log.hh"
#include <iostream>
#include <string>
#include <maxbase/maxbase.hh>

using std::string;
using std::cout;

/**
 * Test parsing.
 *
 * @return Number of errors
 */
int test1()
{
    struct TestCase
    {
        string input;
        string result;
    };

    std::vector<TestCase> cases = {
        {"0-1-1", "0-1-1"},
        {"4321-1234-4321", "4321-1234-4321"},
        {"blah", ""},
        {"1x2x3", ""},
        {"1-2-3-4", ""},
        {"45-54-123456789,0-1-2", "0-1-2,45-54-123456789"},
        {"1-1-1,2-2-2,287-234-134,9-9-9,7-7-7", "1-1-1,2-2-2,7-7-7,9-9-9,287-234-134"},
        {"1-1-1,3-3-3,a-b-c", ""},
        {"-2--2--2",""},
        {"2-2-i",""},
        {"2-i-2",""},
        {"i-2-2",""},
        {"1-1-1,",""},
        {"3-1-0,3-2-4", "3-1-0,3-2-4"}, // Invalid triplet, but this case is not detected by the parser.
    };

    int errors = 0;
    for (auto& test_case : cases)
    {
        string output = GtidList::from_string(test_case.input).to_string();
        if (output != test_case.result)
        {
            cout << "Wrong result: '" << test_case.input << "' produced '" << output << "' while '" <<
                    test_case.result << "' was expected.\n";
            errors++;
        }
    }
    return errors;
}

/**
 * Test parsing + calculations
 *
 * @return Number of errors
 */
int test2()
{
    using sub_mode = GtidList::substraction_mode_t;
    auto ignore = sub_mode::MISSING_DOMAIN_IGNORE;
    auto lhs_add = sub_mode::MISSING_DOMAIN_LHS_ADD;

    struct TestCase
    {
        string input1;
        string input2;
        sub_mode mode;
        uint64_t result;
    };

    std::vector<TestCase> cases = {
        {"1-2-3", "1-2-3", ignore, 0},
        {"1-2-3,2-3-4", "1-2-3", lhs_add, 4},
        {"1-2-3,2-3-4", "1-2-3", ignore, 0},
        {"3-2-1,4-3-2", "4-3-1,3-1-0", lhs_add, 2},
        {"1-2-3,2-2-4,3-2-5", "1-2-3", lhs_add, 9},
        {"1-1-1000000,2-2-2000000", "1-1-1,2-2-2", ignore, 2999997},
        {"4-4-4,7-4-7,5-4-5,6-4-6,", "1-4-1", lhs_add, 0},
        {"4-4-4,7-4-7,5-4-5,6-4-6", "1-4-1", lhs_add, 22},
        {"5-1-4,", "5-1-2", ignore, 0},
    };

    int errors = 0;
    for (auto& test_case : cases)
    {
        auto gtid1 = GtidList::from_string(test_case.input1);
        auto gtid2 = GtidList::from_string(test_case.input2);
        auto output = gtid1.events_ahead(gtid2, test_case.mode);
        if (output != test_case.result)
        {
            cout << "Wrong result: '" << test_case.input1 << "' and '" << test_case.input2 <<
                    "' produced '" << output << "' while '" << test_case.result << "' was expected.\n";
            errors++;
        }
    }
    return errors;
}

int main()
{
    maxbase::init();
    maxbase::Log log;

    int result = 0;
    result += test1();
    result += test2();
    return result;
}
