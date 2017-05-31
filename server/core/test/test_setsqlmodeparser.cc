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

#include "../core/maxscale/setsqlmodeparser.hh"
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <maxscale/buffer.h>
#include <maxscale/paths.h>

using namespace maxscale;
using namespace std;

namespace
{

GWBUF* gwbuf_create_com_query(const char* zStmt)
{
    size_t len = strlen(zStmt);
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* pBuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(pBuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(pBuf) + 5, zStmt, len);

    return pBuf;
}

}

namespace
{

typedef SetSqlModeParser P;

struct TEST_CASE
{
    const char*                  zStmt;
    SetSqlModeParser::result_t   result;
    SetSqlModeParser::sql_mode_t sql_mode;
} test_cases[] =
{
    {
        "SET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "SET SQL_MODE=DEFAULT;",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "SET SQL_MODE=DEFAULT;   ",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "-- This is a comment\nSET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "#This is a comment\nSET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "/*blah*/ SET /*blah*/ SQL_MODE /*blah*/ = /*blah*/ DEFAULT /*blah*/ ",
        P::IS_SET_SQL_MODE,
        P::DEFAULT
    },
    {
        "SET SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SQL_MODE=BLAH",              // So short that it cannot be DEFAULT|ORACLE
        P::NOT_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SQL_MODE='BLAH'",
        P::IS_SET_SQL_MODE,
        P::SOMETHING
    },
    {
        "SET SQL_MODE=BLAHBLAH",
        P::IS_SET_SQL_MODE,
        P::SOMETHING
    },
    {
        "SET SQL_MODE='ORACLE'",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SQL_MODE='BLAH, A, B, ORACLE'",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SQL_MODE='BLAH, A, B, XYZ_123'",
        P::IS_SET_SQL_MODE,
        P::SOMETHING
    },
    {
        "SET VAR1=1234, VAR2=3456, SQL_MODE='A,B, ORACLE'",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SQL_MODE=ORACLE, VAR1=3456, VAR2='A=b, c=d', SQL_MODE='A,B, ORACLE'",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET GLOBAL SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET SESSION SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET LOCAL SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET @@GLOBAL.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET @@SESSION.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET @@LOCAL.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET @@LOCAL . SQL_MODE = ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
    {
        "SET @@SESSION.blah = 1234, @@GLOBAL.blahblah = something, sql_mode=ORACLE",
        P::IS_SET_SQL_MODE,
        P::ORACLE
    },
};

const int N_TEST_CASES = sizeof(test_cases)/sizeof(test_cases[0]);

int test(GWBUF** ppStmt,
         SetSqlModeParser::sql_mode_t expected_sql_mode,
         SetSqlModeParser::result_t expected_result)
{
    int rv = EXIT_SUCCESS;

    SetSqlModeParser parser;

    SetSqlModeParser::sql_mode_t sql_mode;
    SetSqlModeParser::result_t result = parser.get_sql_mode(ppStmt, &sql_mode);

    if (result == expected_result)
    {
        if (result == SetSqlModeParser::IS_SET_SQL_MODE)
        {
            if (sql_mode == expected_sql_mode)
            {
                cout << "OK";
            }
            else
            {
                cout << "ERROR: Expected "
                     << "'" << SetSqlModeParser::to_string(expected_sql_mode) << "'"
                     << ", got "
                     << "'" << SetSqlModeParser::to_string(sql_mode) << "'"
                     << ".";
                rv = EXIT_FAILURE;
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
             << "'" << SetSqlModeParser::to_string(expected_result) << "'"
             << ", got "
             << "'" << SetSqlModeParser::to_string(result) << "'"
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

    GWBUF* pStmt = gwbuf_create_com_query(test_case.zStmt);
    ss_dassert(pStmt);

    rv = test(&pStmt, test_case.sql_mode, test_case.result);

    gwbuf_free(pStmt);

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

int test_non_contiguous()
{
    int rv = EXIT_SUCCESS;

    cout << "Test non-contiguous statements\n"
         << "------------------------------" << endl;

    for (int i = 0; i < N_TEST_CASES; ++i)
    {
        TEST_CASE& test_case = test_cases[i];

        cout << test_case.zStmt << "(" << strlen(test_case.zStmt) << ": ";

        GWBUF* pTail = gwbuf_create_com_query(test_case.zStmt);
        ss_dassert(pTail);
        GWBUF* pStmt = NULL;

        while (pTail)
        {
            size_t n = MYSQL_HEADER_LEN + rand() % 10; // Between 4 and 13 bytes long chunks.

            GWBUF* pHead = gwbuf_split(&pTail, n);

            cout << GWBUF_LENGTH(pHead);

            pStmt = gwbuf_append(pStmt, pHead);

            if (pTail)
            {
                cout << ", ";
            }
        }

        cout << "): " << flush;

        if (test(&pStmt, test_case.sql_mode, test_case.result) == EXIT_FAILURE)
        {
            rv = EXIT_FAILURE;
        }

        gwbuf_free(pStmt);
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

    if (test_non_contiguous() != EXIT_SUCCESS)
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
