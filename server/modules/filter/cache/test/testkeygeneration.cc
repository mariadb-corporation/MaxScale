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
#include <tr1/unordered_map>
#include <maxscale/query_classifier.h>
#include <maxscale/log_manager.h>
#include "storagefactory.hh"
#include "storage.hh"
#include "cache_storage_api.hh"
// TODO: Move this to a common place.
#include "../../../../../query_classifier/test/testreader.hh"


using namespace std;
using namespace std::tr1;

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

GWBUF* create_gwbuf(const string& s)
{
    size_t len = s.length();
    size_t payload_len = len + 1;
    size_t gwbuf_len = MYSQL_HEADER_LEN + payload_len;

    GWBUF* pBuf = gwbuf_alloc(gwbuf_len);

    *((unsigned char*)((char*)GWBUF_DATA(pBuf))) = payload_len;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 1)) = (payload_len >> 8);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 2)) = (payload_len >> 16);
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 3)) = 0x00;
    *((unsigned char*)((char*)GWBUF_DATA(pBuf) + 4)) = 0x03;
    memcpy((char*)GWBUF_DATA(pBuf) + 5, s.c_str(), len);

    return pBuf;
}

int test(Storage& storage, istream& in)
{
    int rv = EXIT_SUCCESS;

    typedef unordered_map<CACHE_KEY, string> Keys;
    Keys keys;

    maxscale::TestReader reader(in);

    size_t n_statements = 0;
    size_t n_keys = 0;
    size_t n_collisions = 0;

    string line;
    while ((rv == EXIT_SUCCESS) && (reader.get_statement(line) == maxscale::TestReader::RESULT_STMT))
    {
        ++n_statements;

        GWBUF* pQuery = create_gwbuf(line);

        CACHE_KEY key;
        cache_result_t result = storage.get_key(NULL, pQuery, &key);

        if (result == CACHE_RESULT_OK)
        {
            Keys::iterator i = keys.find(key);

            if (i != keys.end())
            {
                if (i->second != line)
                {
                    ++n_collisions;
                    cerr << "error: Same key generated for '" << i->second << "' and '"
                         << line <<  "'." << endl;
                }
            }
            else
            {
                ++n_keys;
                keys.insert(make_pair(key, line));
            }
        }
        else
        {
            cerr << "error: Could not generate a key for '" << line << "'." << endl;
            rv = EXIT_FAILURE;
        }
    }

    cout << n_statements << " statements, "
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

    return rv;
}

int test(StorageFactory& factory, istream& in)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage = factory.createStorage(CACHE_THREAD_MODEL_ST,
                                              "unspecified",
                                              INT_MAX,
                                              INT_MAX,
                                              INT_MAX,
                                              0, NULL);

    if (pStorage)
    {
        rv = test(*pStorage, in);

        delete pStorage;
    }

    return rv;
}

}

int main(int argc, char* argv[])
{
    int rv = EXIT_FAILURE;

    if ((argc == 2) || (argc == 3))
    {
        if (mxs_log_init(NULL, ".", MXS_LOG_TARGET_DEFAULT))
        {
            if (qc_init(NULL, NULL))
            {
                const char* zModule = argv[1];

                StorageFactory* pFactory = StorageFactory::Open(zModule);

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
