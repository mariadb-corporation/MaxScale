/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <unistd.h>
#include <chrono>
#include <iostream>
#include <maxscale/log.h>
#include <maxscale/paths.h>
#include <maxscale/query_classifier.h>
#include <maxscale/protocol/mysql.h>

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

int run(const char* zStatement, int n)
{
    std::chrono::duration<double> diff;

    for (int i = 0; i < n; ++i)
    {
        GWBUF* pStatement = create_gwbuf(zStatement);

        auto start = std::chrono::steady_clock::now();
        int rc = qc_parse(pStatement, QC_COLLECT_ALL);
        auto end = std::chrono::steady_clock::now();

        gwbuf_free(pStatement);

        if (rc != QC_QUERY_PARSED)
        {
            return EXIT_FAILURE;
        }

        diff += (end - start);
    }

    cout << "Time: " << diff.count() << " s" << endl;

    return EXIT_SUCCESS;
}

}

int main(int argc, char* argv[])
{
    int rv = EXIT_SUCCESS;

    QC_CACHE_PROPERTIES* pCache_properties = nullptr;
    const char* zStatement = nullptr;
    int n = 0;

    int c;
    while ((c = getopt(argc, argv, "cns:#:")) != -1)
    {
        switch (c)
        {
        case 'c':
            {
                static QC_CACHE_PROPERTIES cache_properties;
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

        set_datadir(strdup("/tmp"));
        set_langdir(strdup("."));
        set_process_datadir(strdup("/tmp"));

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            cout << n
                 << " iterations, while "
                 << (pCache_properties ? "using " : "NOT using ")
                 << "the query classification cache." << endl;

            if (qc_setup(pCache_properties, QC_SQL_MODE_DEFAULT, "qc_sqlite", NULL) &&
                qc_process_init(QC_INIT_BOTH) &&
                qc_thread_init(QC_INIT_BOTH))
            {
                rv = run(zStatement, n);

                if (rv != EXIT_SUCCESS)
                {
                    cerr << "error: Could not parse '" << zStatement << "'." << endl;
                }
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
        cerr << "usage: qc_cache [-(c|n)] -s statement -# iterations" << endl;
    }

    return rv;
}
