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

typedef vector<pair<CACHE_KEY, GWBUF*> > Statements;

bool get_statements(istream& in, Storage& storage, size_t n_statements, Statements* pStatements)
{
    bool success = true;
    typedef unordered_map<CACHE_KEY, GWBUF*> StatementsByKey;

    StatementsByKey statements_by_key;

    TestReader reader(in);

    size_t n = 0;
    string line;
    while (success &&
           (n < n_statements) &&
           (reader.get_statement(line) == TestReader::RESULT_STMT))
    {
        GWBUF* pStmt = create_gwbuf(line);

        CACHE_KEY key;
        cache_result_t result = storage.get_key(NULL, pStmt, &key);

        if (result == CACHE_RESULT_OK)
        {
            StatementsByKey::iterator i = statements_by_key.find(key);

            if (i == statements_by_key.end())
            {
                ++n;
                statements_by_key.insert(make_pair(key, pStmt));
            }
            else
            {
                // Duplicate
                gwbuf_free(pStmt);
            }
        }
        else
        {
            cerr << "error: Could not generate a key for '" << line << "'." << endl;
            success = false;
        }
    }

    if (success)
    {
        copy(statements_by_key.begin(), statements_by_key.end(), back_inserter(*pStatements));
    }
    else
    {
        StatementsByKey::iterator i = statements_by_key.begin();

        while (i != statements_by_key.end())
        {
            delete i->second;
            ++i;
        }
    }

    return success;
}

enum storage_action_t
{
    STORAGE_PUT,
    STORAGE_GET,
    STORAGE_DEL
};

inline storage_action_t& operator++ (storage_action_t& action)
{
    action = static_cast<storage_action_t>((action + 1) % 3);
    return action;
}

struct ThreadData
{
    ThreadData()
        : pStorage(0)
        , pStatements(0)
        , thread(0)
        , terminate(false)
        , rv(EXIT_SUCCESS)
        , start_action(STORAGE_PUT)
    {}

    Storage*          pStorage;
    const Statements* pStatements;
    pthread_t         thread;
    bool              terminate;
    int               rv;
    storage_action_t  start_action;
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
    cout << "Thread starting.\n" << flush;
    ThreadData* pThreadData = static_cast<ThreadData*>(pData);

    Storage& storage = *pThreadData->pStorage;
    const Statements& statements = *pThreadData->pStatements;
    bool& terminate = pThreadData->terminate;

    size_t n = statements.size();
    ss_dassert(n > 0);

    storage_action_t action = pThreadData->start_action;

    size_t n_puts = 0;
    size_t n_gets = 0;
    size_t n_dels = 0;
    size_t n_misses = 0;

    size_t i = 0;

    while (!terminate)
    {
        if (i >= n)
        {
            i = 0;
        }

        const Statements::value_type& statement = statements[i];

        switch (action)
        {
        case STORAGE_PUT:
            {
                cache_result_t result = storage.put_value(statement.first, statement.second);
                ss_dassert(result == CACHE_RESULT_OK);
                ++n_puts;
            }
            break;

        case STORAGE_GET:
            {
                GWBUF* pQuery;
                cache_result_t result = storage.get_value(statement.first, 0, &pQuery);

                if (result == CACHE_RESULT_OK)
                {
                    ss_dassert(GWBUF_LENGTH(pQuery) == GWBUF_LENGTH(statement.second));
                    ss_dassert(memcmp(GWBUF_DATA(pQuery), GWBUF_DATA(statement.second),
                                      GWBUF_LENGTH(pQuery)) == 0);

                    gwbuf_free(pQuery);
                    ++n_gets;
                }
                else
                {
                    ss_dassert(result == CACHE_RESULT_NOT_FOUND);
                    ++n_misses;
                }
            }
            break;

        case STORAGE_DEL:
            {
                cache_result_t result = storage.del_value(statement.first);

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
                }
            }
            break;

        default:
            ss_dassert(!true);
        }

        ++action;
    }

    pThreadData->rv = EXIT_SUCCESS;

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
 * @param statements  The statements to be used.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_thread_hitting(size_t n_threads, size_t n_seconds, Storage& storage, const Statements& statements)
{
    int rv = EXIT_SUCCESS;

    ThreadData threadDatas[n_threads];

    storage_action_t start_action = STORAGE_PUT;

    for (size_t i = 0; i < n_threads; ++i)
    {
        ThreadData* pThreadData = &threadDatas[i];

        pThreadData->pStorage = &storage;
        pThreadData->pStatements = &statements;
        pThreadData->start_action = start_action;

        if (pthread_create(&pThreadData->thread, NULL, test_thread_hitting_thread, pThreadData) != 0)
        {
            // This is impossible, so we just return.
            return EXIT_FAILURE;
        }

        ++start_action;
    }

    stringstream ss;
    ss << "Main thread started " << n_threads << " threads.\n";

    cout << ss.str() << flush;

    sleep(n_seconds);

    cout << "Woke up, now waiting for workers to terminate.\n" << flush;

    for (size_t i = 0; i < n_threads; ++i)
    {
        threadDatas[i].terminate = true;
        pthread_join(threadDatas[i].thread, NULL);

        if (rv == EXIT_SUCCESS)
        {
            rv = threadDatas[i].rv;
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
 * @param istream     Stream, expected to refer to a MySQL/MariaDB test file.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_thread_hitting(size_t n_threads, size_t n_seconds, Storage& storage, istream& in)
{
    int rv = EXIT_FAILURE;

    // Adjust the number of items according to number of threads and duration
    // of test-run, in the hope of ensuring collisions.
    size_t n_statements = n_threads * n_seconds * 50;

    Statements statements;
    if (get_statements(in, storage, n_statements, &statements))
    {
        rv = test_thread_hitting(n_threads, n_seconds, storage, statements);

        for (Statements::iterator i = statements.begin(); i < statements.end(); ++i)
        {
            gwbuf_free(i->second);
        }
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
 * @param istream     Stream, expected to refer to a MySQL/MariaDB test file.
 *
 * @return EXIT_SUCCESS if successful, otherwise EXIT_FAILURE.
 */
int test_raw_storage(size_t n_threads, size_t n_seconds, StorageFactory& factory, istream& in)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage = factory.createRawStorage(CACHE_THREAD_MODEL_MT,
                                                 "unspecified",
                                                 0,
                                                 0,
                                                 0,
                                                 0, NULL);

    if (pStorage)
    {
        rv = test_thread_hitting(n_threads, n_seconds, *pStorage, in);

        delete pStorage;
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
                        rv = test_raw_storage(n_threads, n_seconds, *pFactory, cin);
                    }
                    else
                    {
                        fstream in(argv[3]);

                        if (in)
                        {
                            rv = test_raw_storage(n_threads, n_seconds, *pFactory, in);
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
