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

#include <maxscale/ccdefs.hh>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <string>
#include <maxbase/alloc.hh>
#include <maxscale/parser.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/testparser.hh>
#include "../../../parser_plugin/test/testreader.hh"

using namespace std;
using mxs::Parser;

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

class Tester
{
public:
    Tester(const Parser& parser, uint32_t verbosity)
        : m_parser(parser)
        , m_verbosity(verbosity)
    {
    }

    int run(const char* zStmt)
    {
        int rc = EXIT_SUCCESS;

        GWBUF stmt = mariadb::create_query(zStmt);

        uint32_t type_mask_default = m_parser.get_trx_type_mask_using(stmt, Parser::ParseTrxUsing::DEFAULT);
        uint32_t type_mask_custom = m_parser.get_trx_type_mask_using(stmt, Parser::ParseTrxUsing::CUSTOM);

        if (type_mask_default == type_mask_custom)
        {
            if ((m_verbosity & VERBOSITY_SUCCESSFUL)
                || ((m_verbosity & VERBOSITY_SUCCESSFUL_TRANSACTIONAL) && (type_mask_default != 0)))
            {
                string type_mask_default_str = Parser::type_mask_to_string(type_mask_default);

                cout << zStmt << ": " << type_mask_default_str << endl;
            }
        }
        else
        {
            if (m_verbosity & VERBOSITY_FAILED)
            {
                string type_mask_default_str = Parser::type_mask_to_string(type_mask_default);
                string type_mask_custom_str = Parser::type_mask_to_string(type_mask_custom);

                cout << zStmt << "\n"
                     << "  QC    : " << type_mask_default_str << "\n"
                     << "  PARSER: " << type_mask_custom_str << endl;
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
    Tester& operator=(const Tester&);

private:
    const Parser& m_parser;
    uint32_t      m_verbosity;
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

        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
        {
            mxs::set_libdir("../../../parser_plugin/pp_sqlite");

            mxs::TestParser parser;

            Tester tester(parser, verbosity);

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
                mxb_assert(n == 2);

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
