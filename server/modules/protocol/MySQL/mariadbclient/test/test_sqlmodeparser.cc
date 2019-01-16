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

#include "../sqlmodeparser.hh"
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <maxscale/buffer.hh>
#include <maxscale/paths.h>

using namespace std;

namespace
{

typedef SqlModeParser P;

struct TEST_CASE
{
    const char*               zValue;
    SqlModeParser::sql_mode_t sql_mode;
} test_cases[] =
{
    {
        "DEFAULT",
        P::DEFAULT
    },
    {
        "ORACLE",
        P::ORACLE
    },
    {
        "BLAH",
        P::SOMETHING
    },
    {
        "'BLAH'",
        P::SOMETHING
    },
    {
        "'ORACLE'",
        P::ORACLE
    },
    {
        "'BLAH, A, B, ORACLE'",
        P::ORACLE
    },
    {
        "'BLAH, A, B, XYZ_123'",
        P::SOMETHING
    },
    {
        "'A,B, ORACLE'",
        P::ORACLE
    },
};

const int N_TEST_CASES = sizeof(test_cases) / sizeof(test_cases[0]);

int test(const char* zValue, SqlModeParser::sql_mode_t expected_sql_mode)
{
    int rv = EXIT_SUCCESS;

    SqlModeParser parser;

    SqlModeParser::sql_mode_t sql_mode = parser.get_sql_mode(zValue, zValue + strlen(zValue));

    if (sql_mode == expected_sql_mode)
    {
        cout << "OK";
    }
    else
    {
        cout << "ERROR: Expected "
             << "'" << SqlModeParser::to_string(expected_sql_mode) << "'"
             << ", got "
             << "'" << SqlModeParser::to_string(sql_mode) << "'"
             << ".";
        rv = EXIT_FAILURE;
    }

    cout << endl;

    return rv;
}

int test(const TEST_CASE& test_case)
{
    int rv = EXIT_SUCCESS;

    cout << test_case.zValue << ": ";

    rv = test(test_case.zValue, test_case.sql_mode);

    return rv;
}

int test_contiguous()
{
    int rv = EXIT_SUCCESS;

    cout << "Test contiguous statements\n"
         << "--------------------------" << endl;

    for (int i = 0; i < N_TEST_CASES; ++i)
    {
        if (test(test_cases[i]) == EXIT_FAILURE)
        {
            rv = EXIT_FAILURE;
        }
    }

    cout << endl;

    return rv;
}

int test()
{
    int rv = EXIT_SUCCESS;

    if (test_contiguous() != EXIT_SUCCESS)
    {
        rv = EXIT_FAILURE;
    }

    if (rv == EXIT_SUCCESS)
    {
        cout << "OK" << endl;
    }
    else
    {
        cout << "ERROR" << endl;
    }

    return rv;
}
}


int main(int argc, char* argv[])
{
    int rv = EXIT_SUCCESS;

    srand(time(NULL));

    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        rv = test();

        mxs_log_finish();
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return rv;
}
