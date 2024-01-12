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
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <maxscale/paths.hh>
#include "storagefactory.hh"
#include "cache.hh"
#include "cache_storage_api.hh"
#include "tester.hh"
#include "../../../../core/test/test_utils.hh"

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
        typedef unordered_map<CacheKey, string> Keys;
        Keys keys;

        size_t n_keys = 0;
        size_t n_collisions = 0;

        for (Statements::iterator i = statements.begin(); i < statements.end(); ++i)
        {
            string statement = *i;
            GWBUF query =  mariadb::create_query(statement);
            mxb_assert(query);

            if (query)
            {
                CacheKey key;
                cache_result_t result = Cache::get_default_key(NULL, query, &key);

                if (result == CACHE_RESULT_OK)
                {
                    Keys::iterator it = keys.find(key);

                    if (it != keys.end())
                    {
                        if (it->second != statement)
                        {
                            ++n_collisions;
                            cerr << "error: Same key generated for '" << it->second << "' and '"
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
        init_test_env();

        const char* zModule = argv[1];
        mxs::set_libdir("../storage/storage_inmemory/");

        pFactory = StorageFactory::open(zModule);

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

            delete pFactory;
        }
        else
        {
            cerr << "error: Could not initialize factory." << endl;
        }
    }
    else
    {
        print_usage(argv[0]);
    }

    return rv;
}
