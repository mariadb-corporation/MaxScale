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

#include "../setparser.hh"
#include <stdlib.h>
#include <time.h>
#include <iostream>
#include <maxscale/buffer.h>
#include <maxscale/paths.h>

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

typedef SetParser P;

struct TEST_CASE
{
    const char*         zStmt;
    SetParser::status_t status;
    struct EXPECTATION
    {
        const char* zVariable;
        const char* zValue;
    } expectations[10];
} test_cases[] =
{
    {
        "SET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE=DEFAULT;",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE=DEFAULT;   ",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }
        }
    },
    {
        "-- This is a comment\nSET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }

        }
    },
    {
        "#This is a comment\nSET SQL_MODE=DEFAULT",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }

        }
    },
    {
        "/*blah*/ SET /*blah*/ SQL_MODE /*blah*/ = /*blah*/ DEFAULT /*blah*/ ",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "DEFAULT"
            },
            { NULL, NULL }

        }
    },
    {
        "SET SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }

        }
    },
    {
        "SET SQL_MODE=BLAH",              // So short that it cannot be DEFAULT|ORACLE
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "BLAH"
            },
            { NULL, NULL }

        }
    },
    {
        "SET SQL_MODE='BLAH'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "'BLAH'"
            },
            { NULL, NULL }

        }
    },
    {
        "SET SQL_MODE=BLAHBLAH",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "BLAHBLAH"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE='ORACLE'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "'ORACLE'"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE='BLAH, A, B, ORACLE'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "'BLAH, A, B, ORACLE'"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE='BLAH, A, B, XYZ_123'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "'BLAH, A, B, XYZ_123'"
            },
            { NULL, NULL }
        }
    },
    {
        "SET VAR1=1234, VAR2=3456, SQL_MODE='A,B, ORACLE'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "'A,B, ORACLE'"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SQL_MODE=ORACLE, VAR1=3456, VAR2='A=b, c=d', SQL_MODE='A,B, ORACLE'",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET GLOBAL SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET SESSION SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET LOCAL SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @@GLOBAL.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @@SESSION.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @@LOCAL.SQL_MODE=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @@LOCAL . SQL_MODE = ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "SQL_MODE",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @@SESSION.blah = 1234, @@GLOBAL.blahblah = something, sql_mode=ORACLE",
        P::IS_SET_SQL_MODE,
        {
            {
                "sql_mode",
                "ORACLE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET MAXSCALE=",
        P::NOT_RELEVANT,
        {
            { NULL, NULL }
        }
    },
    {
        "SET MAXSCALE.CACHE.ENABLED=TRUE",
        P::NOT_RELEVANT,
        {
            { NULL, NULL }
        }
    },
    {
        "SET @MAXSCALE.CACHE.ENABLED=TRUE",
        P::IS_SET_MAXSCALE,
        {
            {
                "@MAXSCALE.CACHE.ENABLED",
                "TRUE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @MAXSCALE.CACHE.ENABLED = TRUE /*blah*/",
        P::IS_SET_MAXSCALE,
        {
            {
                "@MAXSCALE.CACHE.ENABLED",
                "TRUE"
            },
            { NULL, NULL }
        }
    },
    {
        "SET @MAXSCALE.CACHE.ENABLED = TRUE, @maxscale.cache.enabled = FALSE",
        P::IS_SET_MAXSCALE,
        {
            {
                "@MAXSCALE.CACHE.ENABLED",
                "TRUE"
            },
            {
                "@maxscale.cache.enabled",
                "FALSE"
            },
            { NULL, NULL }
        }
    },
};

const int N_TEST_CASES = sizeof(test_cases) / sizeof(test_cases[0]);

int test(GWBUF** ppStmt, SetParser::status_t expected_status, const TEST_CASE::EXPECTATION expectations[])
{
    int rv = EXIT_SUCCESS;

    SetParser parser;

    SetParser::Result result;
    SetParser::status_t status = parser.check(ppStmt, &result);

    if (status == expected_status)
    {
        if ((status != SetParser::ERROR) && (status != SetParser::NOT_RELEVANT))
        {
            const SetParser::Result::Items& variables = result.variables();
            const SetParser::Result::Items& values = result.values();
            mxb_assert(variables.size() == values.size());

            SetParser::Result::Items::const_iterator i = variables.begin();
            SetParser::Result::Items::const_iterator j = values.begin();
            int k = 0;

            while (i != variables.end())
            {
                const SetParser::Result::Item& variable = *i;
                const SetParser::Result::Item& value = *j;
                const TEST_CASE::EXPECTATION& expectation = expectations[k];

                if (expectation.zVariable)
                {
                    size_t l1 = variable.second - variable.first;

                    if ((l1 == strlen(expectation.zVariable)) &&
                        (memcmp(variable.first, expectation.zVariable, l1) == 0))
                    {
                        size_t l2 = value.second - value.first;

                        if ((l2 == strlen(expectation.zValue)) &&
                            (memcmp(value.first, expectation.zValue, l2) == 0))
                        {
                            cout << "OK";
                        }
                        else
                        {
                            cout << "ERROR: Expected value "
                                 << "'" << expectation.zValue << "'"
                                 << ", got '";
                            cout.write(value.first, l2);
                            cout << "'.";
                            rv = EXIT_FAILURE;
                        }
                    }
                    else
                    {
                        cout << "ERROR: Expected variable "
                             << "'" << expectation.zVariable << "'"
                             << ", got '";
                        cout.write(variable.first, l1);
                        cout << "'.";
                        rv = EXIT_FAILURE;
                    }
                }
                else
                {
                    cout << "ERROR: Nothing expected for variable '";
                    cout.write(variable.first, variable.second - variable.first);
                    cout << "'";
                    rv = EXIT_FAILURE;
                }

                ++i;
                ++j;
                ++k;
            }

            if (expectations[k].zVariable)
            {
                cout << "ERROR: "
                     << expectations[k].zVariable << " = " << expectations[k].zValue
                     << " not reported.";
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

    GWBUF* pStmt = gwbuf_create_com_query(test_case.zStmt);
    mxb_assert(pStmt);

    rv = test(&pStmt, test_case.status, test_case.expectations);

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
        mxb_assert(pTail);
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

        if (test(&pStmt, test_case.status, test_case.expectations) == EXIT_FAILURE)
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
