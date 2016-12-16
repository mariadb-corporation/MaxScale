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
#include <set>
#include <sstream>
#include <vector>
#include <pthread.h>
#include <tr1/unordered_map>
#include <maxscale/alloc.h>
#include <maxscale/gwdirs.h>
#include <maxscale/log_manager.h>
#include <maxscale/query_classifier.h>
#include "storagefactory.hh"
#include "storage.hh"
#include "cache_storage_api.hh"
// TODO: Move this to a common place.
#include "../../../../../query_classifier/test/testreader.hh"


using namespace std;
using namespace std::tr1;
using maxscale::TestReader;

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

GWBUF* gwbuf_from_string(const string& s)
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

typedef vector<string> Statements;
typedef vector<pair<CACHE_KEY, GWBUF*> > CacheItems;

size_t get_num_statements(size_t n_threads, size_t n_seconds)
{
    return n_threads * n_seconds * 10;
}

size_t get_statements(istream& in, size_t n_statements, Statements* pStatements)
{
    bool success = true;
    typedef std::set<string> StatementsSet;

    StatementsSet statements;

    TestReader reader(in);

    size_t n = 0;
    string statement;
    while (success &&
           (n < n_statements) &&
           (reader.get_statement(statement) == TestReader::RESULT_STMT))
    {
        if (statements.find(statement) == statements.end())
        {
            // Not seen before
            statements.insert(statement);

            pStatements->push_back(statement);
            ++n;
        }
    }

    return n;
}

bool get_cache_items(const Statements& statements, const Storage& storage, CacheItems* pItems)
{
    bool success = true;

    Statements::const_iterator i = statements.begin();

    while (success && (i != statements.end()))
    {
        GWBUF* pQuery = gwbuf_from_string(*i);
        if (pQuery)
        {
            CACHE_KEY key;
            cache_result_t result = storage.get_key(NULL, pQuery, &key);

            if (result == CACHE_RESULT_OK)
            {
                pItems->push_back(std::make_pair(key, pQuery));
            }
            else
            {
                ss_dassert(!true);
                success = false;
            }
        }
        else
        {
            ss_dassert(!true);
            success = false;
        }

        ++i;
    }

    return success;
}

enum storage_action_t
{
    STORAGE_PUT,
    STORAGE_GET,
    STORAGE_DEL
};

storage_action_t get_action()
{
    storage_action_t action;
    long l = random();

    if (l < RAND_MAX / 3)
    {
        action = STORAGE_PUT;
    }
    else if (l < 2 * (RAND_MAX / 3))
    {
        action = STORAGE_GET;
    }
    else
    {
        action = STORAGE_DEL;
    }

    return action;
}

struct ThreadData
{
    ThreadData()
        : pStorage(0)
        , pCache_items(0)
        , thread(0)
        , terminate(false)
        , rv(EXIT_SUCCESS)
    {}

    Storage*          pStorage;
    const CacheItems* pCache_items;
    pthread_t         thread;
    bool              terminate;
    int               rv;
};

/**
 * Thread function for test_thread_hitting
 *
 * The thread will loop over the provided statements and get, put and delete
 * the corresponding item from the storage, and keep doing that until the
 * specified time has elapsed.
 *
 * @param pData  Pointer to a ThreadData instance.
 */
void* test_thread_hitting_thread(void* pData)
{
    int rv = EXIT_SUCCESS;
    cout << "Thread starting.\n" << flush;
    ThreadData* pThread_data = static_cast<ThreadData*>(pData);

    Storage& storage = *pThread_data->pStorage;
    const CacheItems& cache_items = *pThread_data->pCache_items;

    size_t n = cache_items.size();
    ss_dassert(n > 0);

    size_t n_puts = 0;
    size_t n_gets = 0;
    size_t n_dels = 0;
    size_t n_misses = 0;

    size_t i = 0;

    while (!pThread_data->terminate)
    {
        if (i >= n)
        {
            i = 0;
        }

        const CacheItems::value_type& cache_item = cache_items[i];

        storage_action_t action = get_action();

        switch (action)
        {
        case STORAGE_PUT:
            {
                cache_result_t result = storage.put_value(cache_item.first, cache_item.second);
                if (result == CACHE_RESULT_OK)
                {
                    ++n_puts;
                }
                else
                {
                    ss_dassert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        case STORAGE_GET:
            {
                GWBUF* pQuery;
                cache_result_t result = storage.get_value(cache_item.first, 0, &pQuery);

                if (result == CACHE_RESULT_OK)
                {
                    ss_dassert(GWBUF_LENGTH(pQuery) == GWBUF_LENGTH(cache_item.second));
                    ss_dassert(memcmp(GWBUF_DATA(pQuery), GWBUF_DATA(cache_item.second),
                                      GWBUF_LENGTH(pQuery)) == 0);

                    gwbuf_free(pQuery);
                    ++n_gets;
                }
                else if (result == CACHE_RESULT_NOT_FOUND)
                {
                    ++n_misses;
                }
                else
                {
                    ss_dassert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        case STORAGE_DEL:
            {
                cache_result_t result = storage.del_value(cache_item.first);

                if (result == CACHE_RESULT_OK)
                {
                    ++n_dels;
                }
                else if (result == CACHE_RESULT_NOT_FOUND)
                {
                    ++n_misses;
                }
                else
                {
                    ss_dassert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        default:
            ss_dassert(!true);
        }

        ++i;
    }

    pThread_data->rv = rv;

    stringstream ss;
    ss << "Thread ending: " << n_gets << ", " << n_puts << ", " << n_dels << ", " << n_misses << "\n";
    cout << ss.str() << flush;
    return 0;
}

/**
 * test_thread_hitting
 *
 * This test will create a number of threads that will keep on hitting the
 * provided storage until the specified time has elapsed.
 *
 * The purpose of the test is to reveal locking issues that may cause
 * deadlocks or crashes, and leaks (when run under valgrind).
 *
 * @param n_threads   The number of threads that should be used.
 * @param n_seconds   The number of seconds the test should run.
 * @param storage     The storage instance to use.
 * @param cache_items The cache items to be used.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_thread_hitting(size_t n_threads, size_t n_seconds, Storage& storage, const CacheItems& cache_items)
{
    int rv = EXIT_SUCCESS;

    ThreadData thread_datas[n_threads];

    for (size_t i = 0; i < n_threads; ++i)
    {
        ThreadData* pThread_data = &thread_datas[i];

        pThread_data->pStorage = &storage;
        pThread_data->pCache_items = &cache_items;

        if (pthread_create(&pThread_data->thread, NULL, test_thread_hitting_thread, pThread_data) != 0)
        {
            // This is impossible, so we just return.
            return EXIT_FAILURE;
        }
    }

    stringstream ss;
    ss << "Main thread started " << n_threads << " threads.\n";

    cout << ss.str() << flush;

    sleep(n_seconds);

    cout << "Woke up, now waiting for workers to terminate.\n" << flush;

    for (size_t i = 0; i < n_threads; ++i)
    {
        thread_datas[i].terminate = true;
        pthread_join(thread_datas[i].thread, NULL);

        if (rv == EXIT_SUCCESS)
        {
            rv = thread_datas[i].rv;
        }
    }

    cout << "Waited for workers.\n" << flush;

    return rv;
}

/**
 * test_thread_hitting
 *
 * @see test_thread_hitting above.
 *
 * @param n_threads   The number of threads that should be used.
 * @param n_seconds   The number of seconds the test should run.
 * @param storage     The storage instance to use.
 * @param statements  The statements to be used.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_thread_hitting(size_t n_threads, size_t n_seconds, Storage& storage, const Statements& statements)
{
    int rv = EXIT_FAILURE;

    CacheItems cache_items;

    if (get_cache_items(statements, storage, &cache_items))
    {
        rv = test_thread_hitting(n_threads, n_seconds, storage, cache_items);

        for (CacheItems::iterator i = cache_items.begin(); i < cache_items.end(); ++i)
        {
            gwbuf_free(i->second);
        }
    }
    else
    {
        cerr << "Could not convert statements to cache items." << endl;
    }

    return rv;
}

/**
 * test_raw_storage
 *
 * This function will run the tests relevant for raw storage.
 *
 * @param n_threads   The number of threads that should be used.
 * @param n_seconds   The number of seconds the test should run.
 * @param factory     The storage factory using which to create the storage.
 * @param statements  The statements that should be used.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_raw_storage(size_t n_threads,
                     size_t n_seconds,
                     StorageFactory& factory,
                     const Statements& statements)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage = factory.createRawStorage(CACHE_THREAD_MODEL_MT,
                                                 "unspecified",
                                                 0, // No TTL
                                                 0, // No max count
                                                 0, // No max size
                                                 0, NULL);

    if (pStorage)
    {
        rv = test_thread_hitting(n_threads, n_seconds, *pStorage, statements);

        delete pStorage;
    }

    return rv;
}

int test_lru_storage(size_t n_threads,
                     size_t n_seconds,
                     StorageFactory& factory,
                     const Statements& statements)
{
    int rv = EXIT_FAILURE;

    const uint64_t max_count = get_num_statements(n_threads, n_seconds) / 10;

    cout << "Statements: " << statements.size() << ", max_count: " << max_count << "." << endl;

    Storage* pStorage = factory.createStorage(CACHE_THREAD_MODEL_MT,
                                              "unspecified",
                                              0, // No TTL
                                              max_count,
                                              0, // No max size
                                              0, NULL);

    if (pStorage)
    {
        rv = test_thread_hitting(n_threads, n_seconds, *pStorage, statements);

        uint64_t items;
        cache_result_t result = pStorage->get_items(&items);
        ss_dassert(result == CACHE_RESULT_OK);

        if (items != max_count)
        {
            cout << "Expected " << max_count << ", found " << items << "." << endl;
            rv = EXIT_FAILURE;
        }

        delete pStorage;
    }

    return rv;
}

int test(size_t n_threads, size_t n_seconds, StorageFactory& factory, istream& in)
{
    int rv = EXIT_FAILURE;

    Statements statements;
    size_t n_statements = get_num_statements(n_threads, n_seconds);
    size_t n = get_statements(in, n_statements, &statements);

    if (n != 0)
    {
        cout << "Requested " << n_statements << " statements, got " << n << "." << endl;

        cout << "Testing raw storage." << endl;
        int rv1 = test_raw_storage(n_threads, n_seconds, factory, statements);
        cout << "Testing LRU storage." << endl;
        int rv2 = test_lru_storage(n_threads, n_seconds, factory, statements);

        rv = (rv1 == EXIT_FAILURE) || (rv2 == EXIT_FAILURE) ? EXIT_FAILURE : EXIT_SUCCESS;
    }
    else
    {
        cerr << "Could not read any statements." << endl;
    }

    return rv;
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
                    size_t n_threads = get_processor_count() + 1;

                    if (argc == 3)
                    {
                        rv = test(n_threads, n_seconds, *pFactory, cin);
                    }
                    else
                    {
                        fstream in(argv[3]);

                        if (in)
                        {
                            rv = test(n_threads, n_seconds, *pFactory, in);
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
