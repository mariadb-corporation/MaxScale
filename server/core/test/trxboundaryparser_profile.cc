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
#include <iomanip>
#include <iostream>
#include <maxscale/paths.h>
#include "../maxscale/trxboundaryparser.hh"

using namespace std;

namespace
{

char USAGE[] = "usage: trxboundaryparser -n count -s statement\n";

timespec timespec_subtract(const timespec& later, const timespec& earlier)
{
    timespec result = { 0, 0 };

    ss_dassert((later.tv_sec > earlier.tv_sec) ||
               ((later.tv_sec == earlier.tv_sec) && (later.tv_nsec > earlier.tv_nsec)));

    if (later.tv_nsec >= earlier.tv_nsec)
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec;
        result.tv_nsec = later.tv_nsec - earlier.tv_nsec;
    }
    else
    {
        result.tv_sec = later.tv_sec - earlier.tv_sec - 1;
        result.tv_nsec = 1000000000 + later.tv_nsec - earlier.tv_nsec;
    }

    return result;
}

}

int main(int argc, char* argv[])
{
    int rc = EXIT_SUCCESS;

    int nCount = 0;
    const char* zStatement = NULL;

    int c;
    while ((c = getopt(argc, argv, "n:s:")) != -1)
    {
        switch (c)
        {
        case 'n':
            nCount = atoi(optarg);
            break;

        case 's':
            zStatement = optarg;
            break;

        default:
            rc = EXIT_FAILURE;
        }
    }

    if ((rc == EXIT_SUCCESS) && zStatement && (nCount > 0))
    {
        rc = EXIT_FAILURE;

        set_datadir(strdup("/tmp"));
        set_langdir(strdup("."));
        set_process_datadir(strdup("/tmp"));

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            size_t len = strlen(zStatement);
            maxscale::TrxBoundaryParser parser;

            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC_RAW, &start);

            for (int i = 0; i < nCount; ++i)
            {
                parser.type_mask_of(zStatement, len);
            }

            struct timespec finish;
            clock_gettime(CLOCK_MONOTONIC_RAW, &finish);

            struct timespec diff = timespec_subtract(finish, start);

            cout << "Time:" << diff.tv_sec << "." << setfill('0') << setw(9) << diff.tv_nsec << endl;

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
