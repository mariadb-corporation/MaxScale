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

#include <maxscale/ccdefs.hh>

#include <stdlib.h>
#include <iostream>

#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/query_classifier.hh>
#include <maxscale/utils.h>

#include "storagefactory.hh"
#include "teststorage.hh"

using namespace std;

TestStorage::TestStorage(ostream* pOut,
                         size_t   threads,
                         size_t   seconds,
                         size_t   items,
                         size_t   min_size,
                         size_t   max_size)
    : m_out(*pOut)
    , m_threads(threads)
    , m_seconds(seconds)
    , m_items(items)
    , m_min_size(min_size)
    , m_max_size(max_size)
{
}

TestStorage::~TestStorage()
{
}

int TestStorage::run(int argc, char** argv)
{
    int rv = EXIT_FAILURE;

    if ((argc >= 2) || (argc <= 7))
    {
        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            if (qc_setup(NULL, QC_SQL_MODE_DEFAULT, NULL, NULL) && qc_process_init(QC_INIT_BOTH))
            {
                const char* zModule = NULL;
                size_t threads = m_threads;
                size_t seconds = m_seconds;
                size_t items = m_items;
                size_t min_size = m_min_size;
                size_t max_size = m_max_size;

                switch (argc)
                {
                default:
                    mxb_assert(!true);

                case 7:
                    max_size = atoi(argv[6]);

                case 6:
                    min_size = atoi(argv[5]);

                case 5:
                    items = atoi(argv[4]);

                case 4:
                    seconds = atoi(argv[3]);

                case 3:
                    threads = atoi(argv[2]);

                case 2:
                    zModule = argv[1];
                }

                if (threads == 0)
                {
                    threads = get_processor_count() + 1;
                }

                if (items == 0)
                {
                    items = threads * seconds * 10;
                }

                const char FORMAT[] = "../storage/%s";
                char libdir[sizeof(FORMAT) + strlen(zModule)];
                sprintf(libdir, FORMAT, zModule);

                set_libdir(MXS_STRDUP_A(libdir));

                StorageFactory* pFactory = StorageFactory::Open(zModule);

                if (pFactory)
                {
                    out() << "Module  : " << zModule << "\n"
                          << "Threads : " << threads << "\n"
                          << "Seconds : " << seconds << "\n"
                          << "Items   : " << items << "\n"
                          << "Min-Size: " << min_size << "\n"
                          << "Max-Size: " << max_size << "\n"
                          << endl;

                    rv = execute(*pFactory, threads, seconds, items, min_size, max_size);

                    delete pFactory;
                }
                else
                {
                    cerr << "error: Could not initialize factory " << zModule << "." << endl;
                }

                qc_process_end(QC_INIT_BOTH);
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

void TestStorage::print_usage(const char* zProgram)
{
    cout << "usage: " << zProgram << " storage-module [threads [time [items [min-size [max-size]]]]]\n"
         << "\n"
         << "where:\n"
         << "  storage-module  is the name of a storage module,\n"
         << "  threads         is the number of threads to use (if 0, #cores + 1 is used,\n"
         << "  time            is the number of seconds we should run,\n"
         << "  items           is the number of items to use when populating the cache,\n"
         << "                  if 0, threads * seconds * 10 is used\n"
         << "  min-size        is the minimum size of a cache value, and\n"
         << "  max-size        is the maximum size of a cache value." << endl;
}
