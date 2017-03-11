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

#include <maxscale/cppdefs.hh>
#include <iostream>
#include <maxscale/modutil.h>
#include <maxscale/paths.h>
#include <maxscale/protocol/mysql.h>
#include "../core/maxscale/query_classifier.h"

using namespace std;

namespace
{

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

uint32_t get_qc_trx_type_mask(GWBUF* pBuf)
{
    return qc_get_trx_type_mask_using(pBuf, QC_TRX_PARSE_USING_QC);
}

uint32_t get_regex_trx_type_mask(GWBUF* pBuf)
{
    return qc_get_trx_type_mask_using(pBuf, QC_TRX_PARSE_USING_REGEX);
}

uint32_t get_parser_trx_type_mask(GWBUF* pBuf)
{
    return qc_get_trx_type_mask_using(pBuf, QC_TRX_PARSE_USING_PARSER);
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
    { "BEGIN", QUERY_TYPE_BEGIN_TRX },
    { "BEGIN WORK", QUERY_TYPE_BEGIN_TRX },
    { "BEGIN  WORK", QUERY_TYPE_BEGIN_TRX },

    { "COMMIT", QUERY_TYPE_COMMIT },
    { "COMMIT WORK", QUERY_TYPE_COMMIT },
    { "COMMIT  WORK", QUERY_TYPE_COMMIT },

    { "ROLLBACK", QUERY_TYPE_ROLLBACK },
    { "ROLLBACK WORK", QUERY_TYPE_ROLLBACK },
    { "ROLLBACK  WORK", QUERY_TYPE_ROLLBACK },

    { "START TRANSACTION", QUERY_TYPE_BEGIN_TRX },
    { "START  TRANSACTION", QUERY_TYPE_BEGIN_TRX },

    { "START TRANSACTION READ ONLY", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },
    { "START  TRANSACTION READ ONLY", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },
    { "START  TRANSACTION  READ ONLY", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },
    { "START  TRANSACTION  READ  ONLY", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },

    { "START TRANSACTION READ WRITE", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },
    { "START  TRANSACTION READ WRITE", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },
    { "START  TRANSACTION  READ WRITE", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },
    { "START  TRANSACTION  READ  WRITE", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },

    { "START TRANSACTION WITH CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX },
    { "START  TRANSACTION WITH CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX },
    { "START  TRANSACTION  WITH CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX },
    { "START  TRANSACTION  WITH  CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX },
    { "START  TRANSACTION  WITH  CONSISTENT  SNAPSHOT", QUERY_TYPE_BEGIN_TRX },

    { "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ ONLY", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },
    { "START TRANSACTION READ ONLY, WITH CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ },
    { "START TRANSACTION WITH CONSISTENT SNAPSHOT, READ WRITE", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },
    { "START TRANSACTION READ WRITE, WITH CONSISTENT SNAPSHOT", QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE },

    { "SET AUTOCOMMIT=true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT=true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT =true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  = true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =  true", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },

    { "SET AUTOCOMMIT=1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT=1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT =1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  = 1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =  1", QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT },

    { "SET AUTOCOMMIT=false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT=false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT =false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  = false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =  false", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },

    { "SET AUTOCOMMIT=0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT=0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT =0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  = 0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
    { "SET  AUTOCOMMIT  =  0", QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT },
};

const size_t N_TEST_CASES = sizeof(test_cases)/sizeof(test_cases[0]);

bool test(uint32_t (*getter)(GWBUF*), const char* zStmt, uint32_t expected_type_mask)
{
    int rc = true;

    GWBUF* pBuf = create_gwbuf(zStmt);

    uint32_t type_mask = getter(pBuf);

    if (type_mask != expected_type_mask)
    {
        cerr << zStmt << ": expected " << expected_type_mask << ", but got " << type_mask << "." << endl;
        rc = false;
    }

    gwbuf_free(pBuf);

    return rc;
}

bool test(uint32_t (*getter)(GWBUF*))
{
    bool rc = true;

    test_case* pTest = test_cases;
    test_case* pEnd  = pTest + N_TEST_CASES;

    while (pTest < pEnd)
    {
        string base(pTest->zStmt);
        cout << base << endl;

        string s;

        // Just the string
        s = base;
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Prepended with one space.
        s = " " + base;
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Prepended with two spaces.
        s = "  " + base;
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with one space.
        s = base + " ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with two spaces.
        s = base + "  ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a ";".
        s = base + ";";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a " ;".
        s = base + " ;";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a "  ;".
        s = base + "  ;";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a "; ".
        s = base + "; ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a ";  ".
        s = base + ";  ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a " ; ".
        s = base + " ; ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        // Appended with a "  ;  ".
        s = base + "  ;  ";
        if (!test(getter, s.c_str(), pTest->type_mask))
        {
            rc = false;
        }

        ++pTest;
    }

    return rc;
}

}


int main(int argc, char* argv[])
{
    int rc = EXIT_FAILURE;

    set_datadir(strdup("/tmp"));
    set_langdir(strdup("."));
    set_process_datadir(strdup("/tmp"));

    if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
    {
        // We have to setup something in order for the regexes to be compiled.
        if (qc_setup("qc_sqlite", NULL) && qc_process_init(QC_INIT_BOTH))
        {
            rc = EXIT_SUCCESS;

            cout << "QC" << endl;
            cout << "==" << endl;
            if (!test(get_qc_trx_type_mask))
            {
                rc = EXIT_FAILURE;
            }
            cout << endl;

            cout << "Regex" << endl;
            cout << "=====" << endl;
            if (!test(get_regex_trx_type_mask))
            {
                rc = EXIT_FAILURE;
            }
            cout << endl;

            cout << "Parser" << endl;
            cout << "======" << endl;
            if (!test(get_parser_trx_type_mask))
            {
                rc = EXIT_FAILURE;
            }
            cout << endl;

            qc_process_end(QC_INIT_BOTH);
        }
        else
        {
            cerr << "error: Could not initialize qc_sqlite." << endl;
        }

        mxs_log_finish();
    }
    else
    {
        cerr << "error: Could not initialize log." << endl;
    }

    return rc;
}
