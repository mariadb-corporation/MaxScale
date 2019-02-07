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
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <maxscale/alloc.h>
#include <maxscale/paths.h>
#include <maxscale/query_classifier.hh>
#include "storagefactory.hh"
#include "cache.hh"
#include "cache_storage_api.hh"
#include "tester.hh"

using namespace std;

namespace
{

void print_usage(const char* zProgram)
{
    cout << "usage: " << zProgram << " storage-module text-file\n"
         << "\n"
         << "where:\n"
         << "  storage-module  is the name of a storage module,\n"
         << "  test-file       is the name of a text file." << endl;
}

int test(StorageFactory& factory, istream& in)
{
    int rv = EXIT_SUCCESS;

    typedef vector<string> Statements;
    Statements statements;

    if (Tester::get_statements(in, 0, &statements))
    {
        typedef unordered_map<CACHE_KEY, string> Keys;
        Keys keys;

        size_t n_keys = 0;
        size_t n_collisions = 0;

        for (Statements::iterator i = statements.begin(); i < statements.end(); ++i)
        {
            string statement = *i;
            GWBUF* pQuery = Tester::gwbuf_from_string(statement);
            mxb_assert(pQuery);

            if (pQuery)
            {
                CACHE_KEY key;
                cache_result_t result = Cache::get_default_key(NULL, pQuery, &key);

                if (result == CACHE_RESULT_OK)
                {
                    Keys::iterator i = keys.find(key);

                    if (i != keys.end())
                    {
                        if (i->second != statement)
                        {
                            ++n_collisions;
                            cerr << "error: Same key generated for '" << i->second << "' and '"
                                 << statement << "'." << endl;
                        }
                    }
                    else
                    {
                        ++n_keys;
                        keys.insert(make_pair(key, statement));
                    }
                }
                else
                {
                    cerr << "error: Could not generate a key for '" << statement << "'." << endl;
                    rv = EXIT_FAILURE;
                }

                gwbuf_free(pQuery);
            }
            else
            {
                rv = EXIT_FAILURE;
            }
        }

        cout << statements.size() << " statements, "
             << n_keys << " unique keys, "
             << n_collisions << " collisions."
             << endl;


        if (rv == EXIT_SUCCESS)
        {
            if (n_collisions != 0)
            {
                rv = EXIT_FAILURE;
            }
        }
    }
    else
    {
        rv = EXIT_FAILURE;
    }

    return rv;
}
}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;
    StorageFactory* pFactory = nullptr;
    if ((argc == 2) || (argc == 3))
    {
        char* libdir = MXS_STRDUP("../../../../../query_classifier/qc_sqlite/");
        set_libdir(libdir);

        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            if (qc_setup(NULL, QC_SQL_MODE_DEFAULT, NULL, NULL) && qc_process_init(QC_INIT_BOTH))
            {
                const char* zModule = argv[1];
                libdir = MXS_STRDUP("../storage/storage_inmemory/");
                set_libdir(libdir);

                pFactory = StorageFactory::Open(zModule);

                if (pFactory)
                {
                    if (argc == 2)
                    {
                        rv = test(*pFactory, cin);
                    }
                    else
                    {
                        fstream in(argv[2]);

                        if (in)
                        {
                            rv = test(*pFactory, in);
                        }
                        else
                        {
                            cerr << "error: Could not open " << argv[2] << "." << endl;
                        }
                    }
                }
                else
                {
                    cerr << "error: Could not initialize factory." << endl;
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

        // TODO: Remove this once globally allocated memory is freed
        MXS_FREE(libdir);
        delete pFactory;
    }
    else
    {
        print_usage(argv[0]);
    }

    return rv;
}
