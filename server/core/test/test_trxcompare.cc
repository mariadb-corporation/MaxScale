/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <string>
#include "../maxscale/query_classifier.h"
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/protocol/mysql.h>
#include "../../../query_classifier/test/testreader.hh"

using namespace std;

namespace
{

char USAGE[] =
    "test_trxcompare [-v] (-s stmt)|[file]"
    "\n"
    "-s    test single statement\n"
    "-v 0, only return code\n"
    "   1, failed cases (default)\n"
    "   2, successful transactional cases\n"
    "   4, successful cases\n"
    "   7, all cases\n";

enum verbosity_t
{
    VERBOSITY_NOTHING                  = 0, // 000
    VERBOSITY_FAILED                   = 1, // 001
    VERBOSITY_SUCCESSFUL_TRANSACTIONAL = 2, // 010
    VERBOSITY_SUCCESSFUL               = 4, // 100
    VERBOSITY_ALL                      = 7, // 111
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


class Tester
{
public:
    Tester(uint32_t verbosity)
        : m_verbosity(verbosity)
    {
    }

    int run(const char* zStmt)
    {
        int rc = EXIT_SUCCESS;

        GWBUF* pStmt = create_gwbuf(zStmt);

        uint32_t type_mask_qc = qc_get_trx_type_mask_using(pStmt, QC_TRX_PARSE_USING_QC);
        uint32_t type_mask_parser = qc_get_trx_type_mask_using(pStmt, QC_TRX_PARSE_USING_PARSER);

        gwbuf_free(pStmt);

        if (type_mask_qc == type_mask_parser)
        {
            if ((m_verbosity & VERBOSITY_SUCCESSFUL) ||
                ((m_verbosity & VERBOSITY_SUCCESSFUL_TRANSACTIONAL) && (type_mask_qc != 0)))
            {
                char* zType_mask = qc_typemask_to_string(type_mask_qc);

                cout << zStmt << ": " << zType_mask << endl;

                MXS_FREE(zType_mask);
            }
        }
        else
        {
            if (m_verbosity & VERBOSITY_FAILED)
            {
                char* zType_mask_qc = qc_typemask_to_string(type_mask_qc);
                char* zType_mask_parser = qc_typemask_to_string(type_mask_parser);

                cout << zStmt << "\n"
                     << "  QC    : " << zType_mask_qc << "\n"
                     << "  PARSER: " << zType_mask_parser << endl;

                MXS_FREE(zType_mask_qc);
                MXS_FREE(zType_mask_parser);
            }

            rc = EXIT_FAILURE;
        }

        return rc;
    }

    int run(istream& in)
    {
        int rc = EXIT_SUCCESS;

        maxscale::TestReader reader(in);

        string stmt;

        while (reader.get_statement(stmt) == maxscale::TestReader::RESULT_STMT)
        {
            if (run(stmt.c_str()) == EXIT_FAILURE)
            {
                rc = EXIT_FAILURE;
            }
        }

        return rc;
    }

private:
    Tester(const Tester&);
    Tester& operator = (const Tester&);

private:
    uint32_t m_verbosity;
};

}



int main(int argc, char* argv[])
{
    int rc = EXIT_SUCCESS;

    int verbosity = VERBOSITY_FAILED;
    const char* zStatement = NULL;

    int c;
    while ((c = getopt(argc, argv, "s:v:")) != -1)
    {
        switch (c)
        {
        case 's':
            zStatement = optarg;
            break;

        case 'v':
            verbosity = atoi(optarg);
            break;

        default:
            rc = EXIT_FAILURE;
        }
    }

    if ((rc == EXIT_SUCCESS) && (verbosity >= VERBOSITY_NOTHING) && (verbosity <= VERBOSITY_ALL))
    {
        rc = EXIT_FAILURE;

        set_datadir(strdup("/tmp"));
        set_langdir(strdup("."));
        set_process_datadir(strdup("/tmp"));

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            // We have to setup something in order for the regexes to be compiled.
            if (qc_setup("qc_sqlite", QC_SQL_MODE_DEFAULT, NULL) && qc_process_init(QC_INIT_BOTH))
            {
                Tester tester(verbosity);

                int n = argc - (optind - 1);

                if (zStatement)
                {
                    rc = tester.run(zStatement);
                }
                else if (n == 1)
                {
                    rc = tester.run(cin);
                }
                else
                {
                    ss_dassert(n == 2);

                    ifstream in(argv[argc - 1]);

                    if (in)
                    {
                        rc = tester.run(in);
                    }
                    else
                    {
                        cerr << "error: Could not open " << argv[argc - 1] << "." << endl;
                    }
                }

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
    }
    else
    {
        cout << USAGE << endl;
    }

    return rc;
}
