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

#include "testerlrustorage.hh"
#include "storage.hh"
#include "storagefactory.hh"

using namespace std;

TesterLRUStorage::TesterLRUStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

int TesterLRUStorage::execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    int rv1 = test_max_count(n_threads, n_seconds, cache_items);
    out() << endl;
    int rv2 = test_max_size(n_threads, n_seconds, cache_items);

    return ((rv1 == EXIT_SUCCESS) && (rv2 == EXIT_SUCCESS)) ? EXIT_SUCCESS : EXIT_FAILURE;
}

int TesterLRUStorage::test_max_count(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage;

    size_t max_count = cache_items.size() / 4;

    out() << "LRU max-count: " << max_count << "\n" << endl;

    pStorage = m_factory.createStorage(CACHE_THREAD_MODEL_MT,
                                       "unspecified",
                                       0, // No TTL
                                       max_count,
                                       0, // No max size
                                       0, NULL);

    if (pStorage)
    {
        rv = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);

        uint64_t items;
        cache_result_t result = pStorage->get_items(&items);
        ss_dassert(result == CACHE_RESULT_OK);

        out() << "Max count: " << max_count << ", count: " << items << "." << endl;

        if (items > max_count)
        {
            rv = EXIT_FAILURE;
        }

        delete pStorage;
    }

    return rv;
}

int TesterLRUStorage::test_max_size(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage;

    uint64_t size = 0;

    for (CacheItems::const_iterator i = cache_items.begin(); i < cache_items.end(); ++i)
    {
        size += gwbuf_length(i->second);
    }

    size_t max_size = size / 10;

    out() << "LRU max-size: " << max_size << "\n" << endl;

    pStorage = m_factory.createStorage(CACHE_THREAD_MODEL_MT,
                                       "unspecified",
                                       0, // No TTL
                                       0, // No max count
                                       max_size,
                                       0, NULL);

    if (pStorage)
    {
        rv = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);

        uint64_t size;
        cache_result_t result = pStorage->get_size(&size);
        ss_dassert(result == CACHE_RESULT_OK);

        out() << "Max size: " << max_size << ", size: " << size << "." << endl;

        if (size > max_size)
        {
            rv = EXIT_FAILURE;
        }

        delete pStorage;
    }

    return rv;
}
