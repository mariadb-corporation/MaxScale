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

#include "../setparser.hh"
#include "../sqlmodeparser.hh"
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <maxscale/buffer.hh>
#include <maxscale/paths.hh>

using namespace std;

namespace
{

typedef SetParser     P1;
typedef SqlModeParser P2;

struct TEST_CASE
{
    const char*               zStmt;
    SetParser::status_t       status;
    SqlModeParser::sql_mode_t sql_mode;
} test_cases[] =
{
    {
        "SET SQL_MODE=DEFAULT",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "SET SQL_MODE=DEFAULT;",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "SET SQL_MODE=DEFAULT;   ",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "-- This is a comment\nSET SQL_MODE=DEFAULT",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "#This is a comment\nSET SQL_MODE=DEFAULT",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "/*blah*/ SET /*blah*/ SQL_MODE /*blah*/ = /*blah*/ DEFAULT /*blah*/ ",
        P1::IS_SET_SQL_MODE,
        P2::DEFAULT
    },
    {
        "SET SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET SQL_MODE=BLAH",
        P1::IS_SET_SQL_MODE,
        P2::SOMETHING
    },
    {
        "SET SQL_MODE='BLAH'",
        P1::IS_SET_SQL_MODE,
        P2::SOMETHING
    },
    {
        "SET SQL_MODE=BLAHBLAH",
        P1::IS_SET_SQL_MODE,
        P2::SOMETHING
    },
    {
        "SET SQL_MODE='ORACLE'",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET SQL_MODE='BLAH, A, B, ORACLE'",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET SQL_MODE='BLAH, A, B, XYZ_123'",
        P1::IS_SET_SQL_MODE,
        P2::SOMETHING
    },
    {
        "SET VAR1=1234, VAR2=3456, SQL_MODE='A,B, ORACLE'",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET SQL_MODE=ORACLE, VAR1=3456, VAR2='A=b, c=d', SQL_MODE='A,B, ORACLE'",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET GLOBAL SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET SESSION SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET LOCAL SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET @@GLOBAL.SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET @@SESSION.SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET @@LOCAL.SQL_MODE=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET @@LOCAL . SQL_MODE = ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
    {
        "SET @@SESSION.blah = 1234, @@GLOBAL.blahblah = something, sql_mode=ORACLE",
        P1::IS_SET_SQL_MODE,
        P2::ORACLE
    },
};

const int N_TEST_CASES = sizeof(test_cases) / sizeof(test_cases[0]);

int test(const GWBUF& stmt,
         SqlModeParser::sql_mode_t expected_sql_mode,
         SetParser::status_t expected_status)
{
    int rv = EXIT_SUCCESS;

    SetParser set_parser;
    SetParser::Result result;

    SetParser::status_t status = set_parser.check(mariadb::get_sql(stmt), &result);

    if (status == expected_status)
    {
        if (status == SetParser::IS_SET_SQL_MODE)
        {
            const SetParser::Result::Items& values = result.values();

            for (auto i = values.begin(); i != values.end(); ++i)
            {
                const SetParser::Result::Item& item = *i;

                SqlModeParser sql_mode_parser;
                SqlModeParser::sql_mode_t sql_mode = sql_mode_parser.get_sql_mode(item.first, item.second);

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
            }
        }
        else
        {
            cout << "OK";
        }
    }
    else
    {
        cout << "ERROR: Expected "
             << "'" << SetParser::to_string(expected_status) << "'"
             << ", got "
             << "'" << SetParser::to_string(status) << "'"
             << ".";
        rv = EXIT_FAILURE;
    }

    cout << endl;

    return rv;
}

int test(const TEST_CASE& test_case)
{
    int rv = EXIT_SUCCESS;

    cout << test_case.zStmt << ": ";

    GWBUF stmt = mariadb::create_query(test_case.zStmt);
    rv = test(stmt, test_case.sql_mode, test_case.status);

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

    mxs::set_datadir("/tmp");
    mxs::set_langdir(".");
    mxs::set_process_datadir("/tmp");

    if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
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
