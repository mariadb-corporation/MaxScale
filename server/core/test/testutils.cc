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

namespace
{

#define TRIM_TEST_CASE_ENTRY(zFrom, zTo) { zFrom, zTo }

struct TRIM_TEST_CASE
{
    const char* zFrom;
    const char* zTo;
} trim_testcases[] =
{
    TRIM_TEST_CASE_ENTRY("", ""),
    TRIM_TEST_CASE_ENTRY("a", "a"),
    TRIM_TEST_CASE_ENTRY(" a", "a"),
    TRIM_TEST_CASE_ENTRY("a ", "a"),
    TRIM_TEST_CASE_ENTRY("a ", "a"),
    TRIM_TEST_CASE_ENTRY(" a ", "a"),
    TRIM_TEST_CASE_ENTRY("  a", "a"),
    TRIM_TEST_CASE_ENTRY("a  ", "a"),
    TRIM_TEST_CASE_ENTRY("  a  ", "a"),
    TRIM_TEST_CASE_ENTRY("  a b  ", "a b"),
};

const int n_trim_testcases = sizeof(trim_testcases) / sizeof(trim_testcases[0]);


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

int test1()
{
    return test(trim_testcases, n_trim_testcases, trim);
}

}

int main(int argc, char* argv[])
{
    int rv = 0;

    rv += test1();

    return rv;
}
