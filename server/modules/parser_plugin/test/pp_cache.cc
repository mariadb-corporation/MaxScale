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

#include <unistd.h>
#include <chrono>
#include <iostream>
#include <maxbase/stopwatch.hh>
#include <maxscale/cachingparser.hh>
#include <maxscale/log.hh>
#include <maxscale/paths.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/testparser.hh>

using namespace std;

namespace
{

GWBUF* create_gwbuf(const char* z, size_t len)
{
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* gwbuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(gwbuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(gwbuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(gwbuf) + 5, z, len);

    return gwbuf;
}

inline GWBUF* create_gwbuf(const char* z)
{
    return create_gwbuf(z, strlen(z));
}

int run(const mxs::Parser& parser, const char* zStatement, int n)
{
    maxbase::Duration diff {};

    for (int i = 0; i < n; ++i)
    {
        GWBUF* pStatement = create_gwbuf(zStatement);

        maxbase::StopWatch sw;
        mxs::Parser::Result rc = parser.parse(*pStatement, mxs::Parser::COLLECT_ALL);
        diff += sw.split();

        gwbuf_free(pStatement);

        if (rc != mxs::Parser::Result::PARSED)
        {
            return EXIT_FAILURE;
        }
    }

    cout << "Time: " << mxb::to_secs(diff) << " s" << endl;

    return EXIT_SUCCESS;
}
}

int main(int argc, char* argv[])
{
    int rv = EXIT_SUCCESS;

    mxs::CachingParser::Properties* pCache_properties = nullptr;
    const char* zStatement = nullptr;
    int n = 0;

    int c;
    while ((c = getopt(argc, argv, "cns:#:")) != -1)
    {
        switch (c)
        {
        case 'c':
            {
                static mxs::CachingParser::Properties cache_properties;
                pCache_properties = &cache_properties;
            }
            break;

        case 'n':
            pCache_properties = nullptr;
            break;

        case 's':
            zStatement = optarg;
            break;

        case '#':
            n = atoi(optarg);
            break;

        default:
            rv = EXIT_FAILURE;
        }
    }

    if ((rv == EXIT_SUCCESS) && zStatement && (n > 0))
    {
        rv = EXIT_FAILURE;

        mxs::set_datadir("/tmp");
        mxs::set_langdir(".");
        mxs::set_process_datadir("/tmp");

        if (mxs_log_init(NULL, ".", MXB_LOG_TARGET_DEFAULT))
        {
            cout << n
                 << " iterations, while "
                 << (pCache_properties ? "using " : "NOT using ")
                 << "the query classification cache." << endl;

            mxs::TestParser parser;

            rv = run(parser, zStatement, n);

            if (rv != EXIT_SUCCESS)
            {
                cerr << "error: Could not parse '" << zStatement << "'." << endl;
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
        cerr << "usage: qc_cache [-(c|n)] -s statement -# iterations" << endl;
    }

    return rv;
}
