/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include <iostream>
#include <fstream>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include "storagefactory.hh"
#include "testerlrustorage.hh"

using namespace std;

namespace
{

void print_usage(const char* zProgram)
{
    cout << "usage: " << zProgram << " time storage-module text-file\n"
         << "\n"
         << "where:\n"
         << "  time            is the number of seconds we should run,\n"
         << "  storage-module  is the name of a storage module,\n"
         << "  test-file       is the name of a text file." << endl;
}

}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;

    if ((argc == 3) || (argc == 4))
    {
        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            size_t n_seconds = atoi(argv[1]);

            if (qc_init(NULL, NULL))
            {
                const char* zModule = argv[2];

                const char FORMAT[] = "../storage/%s";
                char libdir[sizeof(FORMAT) + strlen(zModule)];
                sprintf(libdir, FORMAT, zModule);

                set_libdir(MXS_STRDUP_A(libdir));

                StorageFactory* pFactory = StorageFactory::Open(zModule);

                if (pFactory)
                {
                    TesterLRUStorage tester(&cout, pFactory);

                    size_t n_threads = get_processor_count() + 1;

                    if (argc == 3)
                    {
                        rv = tester.run(n_threads, n_seconds, cin);
                    }
                    else
                    {
                        fstream in(argv[3]);

                        if (in)
                        {
                            rv = tester.run(n_threads, n_seconds, in);
                        }
                        else
                        {
                            cerr << "error: Could not open " << argv[3] << "." << endl;
                        }
                    }

                    delete pFactory;
                }
                else
                {
                    cerr << "error: Could not initialize factory " << zModule << "." << endl;
                }
            }
            else
            {
                cerr << "error: Could not initialize query classifier." << endl;
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
        print_usage(argv[0]);
    }

    return rv;
}
