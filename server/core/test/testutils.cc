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

#include <maxscale/utils.h>
#include <string.h>
#include <iostream>

using std::cout;
using std::endl;

namespace
{

#define TRIM_TCE(zFrom, zTo) { zFrom, zTo }

struct TRIM_TEST_CASE
{
    const char* zFrom;
    const char* zTo;
};

TRIM_TEST_CASE trim_testcases[] =
{
    TRIM_TCE("", ""),
    TRIM_TCE("a", "a"),
    TRIM_TCE(" a", "a"),
    TRIM_TCE("a ", "a"),
    TRIM_TCE(" a ", "a"),
    TRIM_TCE("  a", "a"),
    TRIM_TCE("a  ", "a"),
    TRIM_TCE("  a  ", "a"),
    TRIM_TCE("  a b  ", "a b"),
};

const int n_trim_testcases = sizeof(trim_testcases) / sizeof(trim_testcases[0]);

TRIM_TEST_CASE trim_leading_testcases[] =
{
    TRIM_TCE("", ""),
    TRIM_TCE("a", "a"),
    TRIM_TCE(" a", "a"),
    TRIM_TCE("a ", "a "),
    TRIM_TCE(" a ", "a "),
    TRIM_TCE("  a", "a"),
    TRIM_TCE("a  ", "a  "),
    TRIM_TCE("  a  ", "a  "),
    TRIM_TCE("  a b  ", "a b  "),
};

const int n_trim_leading_testcases = sizeof(trim_leading_testcases) / sizeof(trim_leading_testcases[0]);

TRIM_TEST_CASE trim_trailing_testcases[] =
{
    TRIM_TCE("", ""),
    TRIM_TCE("a", "a"),
    TRIM_TCE(" a", " a"),
    TRIM_TCE("a ", "a"),
    TRIM_TCE(" a ", " a"),
    TRIM_TCE("  a", "  a"),
    TRIM_TCE("a  ", "a"),
    TRIM_TCE("  a  ", "  a"),
    TRIM_TCE("  a b  ", "  a b"),
};

const int n_trim_trailing_testcases = sizeof(trim_trailing_testcases) / sizeof(trim_trailing_testcases[0]);


int test(TRIM_TEST_CASE* pTest_cases, int n_test_cases, char* (*p)(char*))
{
    int rv = 0;

    for (int i = 0; i < n_test_cases; ++i)
    {
        const char* zFrom = pTest_cases[i].zFrom;
        const char* zTo = pTest_cases[i].zTo;

        char copy[strlen(zFrom) + 1];
        strcpy(copy, zFrom);

        char* z = p(copy);

        if (strcmp(z, zTo) != 0)
        {
            ++rv;
        }
    }

    return rv;
}

int test_trim()
{
    cout << "trim()" << endl;
    return test(trim_testcases, n_trim_testcases, trim);
}

int test_trim_leading()
{
    cout << "trim_leading()" << endl;
    return test(trim_leading_testcases, n_trim_leading_testcases, trim_leading);
}

int test_trim_trailing()
{
    cout << "trim_trailing()" << endl;
    return test(trim_trailing_testcases, n_trim_trailing_testcases, trim_trailing);
}

}

int main(int argc, char* argv[])
{
    int rv = 0;

    rv += test_trim();
    rv += test_trim_leading();
    rv += test_trim_trailing();

    return rv;
}
