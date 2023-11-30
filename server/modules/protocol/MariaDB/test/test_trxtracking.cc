/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/ccdefs.hh>
#include <getopt.h>
#include <algorithm>
#include <iostream>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/testparser.hh>

using namespace std;

namespace
{

const mxs::Parser* pParser = nullptr;

enum test_target_t
{
    TEST_PARSER = 0x1,
    TEST_QC     = 0x2,
    TEST_ALL    = (TEST_PARSER | TEST_QC)
};

GWBUF* create_gwbuf(const char* zStmt)
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

uint32_t get_default_trx_type_mask(GWBUF* pBuf)
{
    return pParser->get_trx_type_mask_using(*pBuf, mxs::Parser::ParseTrxUsing::DEFAULT);
}

uint32_t get_custom_trx_type_mask(GWBUF* pBuf)
{
    return pParser->get_trx_type_mask_using(*pBuf, mxs::Parser::ParseTrxUsing::CUSTOM);
}
}

namespace
{

struct test_case
{
    const char* zStmt;
    uint32_t    type_mask;
} test_cases[] =
{
    // Keep these all uppercase, lowercase are tested programmatically.
    {"BEGIN",
     mxs::sql::TYPE_BEGIN_TRX},
    {"BEGIN WORK",
     mxs::sql::TYPE_BEGIN_TRX},

    {"COMMIT",
     mxs::sql::TYPE_COMMIT},
    {"COMMIT WORK",
     mxs::sql::TYPE_COMMIT},

    {"ROLLBACK",
     mxs::sql::TYPE_ROLLBACK},
    {"ROLLBACK WORK",
     mxs::sql::TYPE_ROLLBACK},

    {"START TRANSACTION",
     mxs::sql::TYPE_BEGIN_TRX},

    {"START TRANSACTION READ ONLY",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_READ},
    {"START TRANSACTION READ WRITE",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_WRITE},

    {"START TRANSACTION WITH CONSISTENT SNAPSHOT",
     mxs::sql::TYPE_BEGIN_TRX},

    {"START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_READ},

    {"SET AUTOCOMMIT=true",
     mxs::sql::TYPE_COMMIT
     | mxs::sql::TYPE_ENABLE_AUTOCOMMIT},

    {"SET AUTOCOMMIT=1",
     mxs::sql::TYPE_COMMIT
     | mxs::sql::TYPE_ENABLE_AUTOCOMMIT},

    {"SET AUTOCOMMIT=false",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_DISABLE_AUTOCOMMIT},

    {"SET AUTOCOMMIT=0",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_DISABLE_AUTOCOMMIT},
    {"SET @@AUTOCOMMIT=0",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_DISABLE_AUTOCOMMIT},
    {"SET GLOBAL AUTOCOMMIT=0",
     0},
    {"SET SESSION AUTOCOMMIT=0",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_DISABLE_AUTOCOMMIT},
    {"SET @@SESSION . AUTOCOMMIT=0",
     mxs::sql::TYPE_BEGIN_TRX
     | mxs::sql::TYPE_DISABLE_AUTOCOMMIT},
    {"SET @@GLOBAL . AUTOCOMMIT=0",
     0},
};

const size_t N_TEST_CASES = sizeof(test_cases) / sizeof(test_cases[0]);


bool test(uint32_t (* getter)(GWBUF*), const char* zStmt, uint32_t expected_type_mask)
{
    int rc = true;

    GWBUF* pBuf = create_gwbuf(zStmt);

    uint32_t type_mask = getter(pBuf);

    if (type_mask != expected_type_mask)
    {
        cerr << "\"" << zStmt << "\""
             << ": expected " << expected_type_mask << ", but got " << type_mask << "." << endl;
        rc = false;
    }

    gwbuf_free(pBuf);

    return rc;
}


const char* prefixes[] =
{
    " ",
    "  ",
    "\n",
    " \n",
    "\n ",
    "-- comment\n"
};

const int N_PREFIXES = sizeof(prefixes) / sizeof(prefixes[0]);

bool test_with_prefixes(uint32_t (* getter)(GWBUF*), const string& base, uint32_t type_mask)
{
    bool rc = true;

    for (int i = 0; i < N_PREFIXES; ++i)
    {
        string s = prefixes[i] + base;

        if (!test(getter, s.c_str(), type_mask))
        {
            rc = false;
        }
    }

    return rc;
}


const char* suffixes[] =
{
    " ",
    "  ",
    "\n",
    " \n",
    "\n ",
    ";",
    " ;",
    "  ;",
    " ;",
    "  ;",
    " ; ",
    ";\n",
    "  ;  ",
    "-- comment this, comment that",
    // "# comment this, comment that" /* pp_sqlite does not handle this */
};

const int N_SUFFIXES = sizeof(suffixes) / sizeof(suffixes[0]);

bool test_with_suffixes(uint32_t (* getter)(GWBUF*), const string& base, uint32_t type_mask)
{
    bool rc = true;

    for (int i = 0; i < N_SUFFIXES; ++i)
    {
        string s = base + suffixes[i];

        if (!test(getter, s.c_str(), type_mask))
        {
            rc = false;
        }
    }

    return rc;
}


const char* whitespace[] =
{
    "  ",
    "\n",
    "/**/",
    "/***/",
    "/****/",
    "/* / * */",
    "-- comment\n"
};

const int N_WHITESPACE = sizeof(whitespace) / sizeof(whitespace[0]);

bool test_with_whitespace(uint32_t (* getter)(GWBUF*), const string& base, uint32_t type_mask)
{
    bool rc = true;

    string::const_iterator i = base.begin();
    string::const_iterator end = base.end();

    string head;

    while (i != end)
    {
        if (*i == ' ')
        {
            string tail(i + 1, end);

            for (int j = 0; j < N_WHITESPACE; ++j)
            {
                string s = head + whitespace[j] + tail;

                if (!test(getter, s.c_str(), type_mask))
                {
                    rc = false;
                }
            }
        }

        head += *i;

        ++i;
    }

    return rc;
}


const char* commas[] =
{
    " ,",
    "  ,",
    " , ",
    " ,   ",
};

const int N_COMMAS = sizeof(commas) / sizeof(commas[0]);

bool test_with_commas(uint32_t (* getter)(GWBUF*), const string& base, uint32_t type_mask)
{
    bool rc = true;

    string::const_iterator i = base.begin();
    string::const_iterator end = base.end();

    string head;

    while (i != end)
    {
        if (*i == ',')
        {
            string tail(i + 1, end);

            for (int j = 0; j < N_COMMAS; ++j)
            {
                string s = head + commas[j] + tail;

                if (!test(getter, s.c_str(), type_mask))
                {
                    rc = false;
                }
            }
        }

        head += *i;

        ++i;
    }

    return rc;
}


bool test(uint32_t (* getter)(GWBUF*), bool dont_bail_out)
{
    bool rc = true;

    test_case* pTest = test_cases;
    test_case* pEnd = pTest + N_TEST_CASES;

    while ((pTest < pEnd) && (dont_bail_out || rc))
    {
        string base(pTest->zStmt);
        cout << base << endl;

        if (!test(getter, base.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        if (dont_bail_out || rc)
        {
            // Test all lowercase.
            string lc(base);
            transform(lc.begin(), lc.end(), lc.begin(), ::tolower);

            if (!test(getter, lc.c_str(), pTest->type_mask))
            {
                rc = false;
            }
        }

        if (dont_bail_out || rc)
        {
            if (!test_with_prefixes(getter, base, pTest->type_mask))
            {
                rc = false;
            }
        }

        if (dont_bail_out || rc)
        {
            if (!test_with_whitespace(getter, base, pTest->type_mask))
            {
                rc = false;
            }
        }

        if (dont_bail_out || rc)
        {
            if (!test_with_commas(getter, base, pTest->type_mask))
            {
                rc = false;
            }
        }

        if (dont_bail_out || rc)
        {
            if (!test_with_suffixes(getter, base, pTest->type_mask))
            {
                rc = false;
            }
        }

        ++pTest;
    }

    return rc;
}
}

namespace
{

char USAGE[] =
    "usage: test_trxtracking [-p] [-q] [-r] [-d]\n"
    "\n"
    "-p  : Test using custom parser\n"
    "-q  : Test using query classifier\n"
    "-r  : Test using regex matching\n"
    "-d  : Don't bail out at first error\n"
    "\n"
    "If neither -p, -q or -r has been specified, then all will be tested.\n";
}

int main(int argc, char* argv[])
{
    int rc = EXIT_SUCCESS;

    bool test_all = true;
    uint32_t test_target = 0;
    bool dont_bail_out = false;

    int c;
    while ((c = getopt(argc, argv, "dpq")) != -1)
    {
        switch (c)
        {
        case 'p':
            test_all = false;
            test_target |= TEST_PARSER;
            break;

        case 'q':
            test_all = false;
            test_target = TEST_QC;
            break;

        case 'd':
            dont_bail_out = true;
            break;

        default:
            cout << USAGE << endl;
            rc = EXIT_FAILURE;
        }
    }

    if (rc == EXIT_SUCCESS)
    {
        rc = EXIT_FAILURE;

        if (test_all)
        {
            test_target = TEST_ALL;
        }

        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
        {
            mxs::set_libdir("../../../parser_plugin/pp_sqlite");

            // TODO: The following line is due to MXS-4548, remove when that has been fixed.
            mxs::CachingParser::set_thread_cache_enabled(false);

            mxs::TestParser parser;
            rc = EXIT_SUCCESS;

            pParser = &parser;

            if (test_target & TEST_QC)
            {
                cout << "QC" << endl;
                cout << "==" << endl;
                if (!test(get_default_trx_type_mask, dont_bail_out))
                {
                    rc = EXIT_FAILURE;
                }
                cout << endl;
            }

            if (test_target & TEST_PARSER)
            {
                cout << "Parser" << endl;
                cout << "======" << endl;
                if (!test(get_custom_trx_type_mask, dont_bail_out))
                {
                    rc = EXIT_FAILURE;
                }
                cout << endl;
            }

            mxs_log_finish();
        }
        else
        {
            cerr << "error: Could not initialize log." << endl;
        }
    }

    return rc;
}
