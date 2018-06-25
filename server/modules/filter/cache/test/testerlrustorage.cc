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

#include "testerlrustorage.hh"
#include "storage.hh"
#include "storagefactory.hh"

using namespace std;
using namespace maxscale;

TesterLRUStorage::TesterLRUStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

int TesterLRUStorage::execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    uint64_t size = 0;

    for (CacheItems::const_iterator i = cache_items.begin(); i < cache_items.end(); ++i)
    {
        size += gwbuf_length(i->second);
    }

    int rv1 = test_smoke(cache_items);
    out() << endl;
    int rv2 = test_lru(cache_items, size);
    out() << endl;
    int rv3 = test_max_count(n_threads, n_seconds, cache_items, size);
    out() << endl;
    int rv4 = test_max_size(n_threads, n_seconds, cache_items, size);
    out() << endl;
    int rv5 = test_max_count_and_size(n_threads, n_seconds, cache_items, size);

    return combine_rvs(rv1, rv2, rv3, rv4, rv5);
}

Storage* TesterLRUStorage::get_storage(const CACHE_STORAGE_CONFIG& config) const
{
    return m_factory.createStorage("unspecified", config);
}

int TesterLRUStorage::test_lru(const CacheItems& cache_items, uint64_t size)
{
    int rv = EXIT_FAILURE;
    out() << "LRU\n" << endl;

    size_t items = cache_items.size() > 100 ? 100 : cache_items.size();

    CacheStorageConfig config(CACHE_THREAD_MODEL_MT);

    Storage* pStorage = get_storage(config);

    if (pStorage)
    {
        rv = EXIT_SUCCESS;

        cache_result_t result;

        for (size_t i = 0; i < items; ++i)
        {
            const CacheItems::value_type& cache_item = cache_items[i];

            result = pStorage->put_value(cache_item.first, cache_item.second);

            if (result == CACHE_RESULT_OK)
            {
                CACHE_KEY key;
                GWBUF* pValue;
                result = pStorage->get_head(&key, &pValue);

                if (result == CACHE_RESULT_OK)
                {
                    if (key != cache_item.first)
                    {
                        out() << "Last put value did not become the head." << endl;
                        rv = EXIT_FAILURE;
                    }
                    else if (gwbuf_compare(pValue, cache_item.second) != 0)
                    {
                        out() << "Obtained value not the same as that which was put." << endl;
                        rv = EXIT_FAILURE;
                    }

                    gwbuf_free(pValue);
                }
                else
                {
                    ss_dassert(!true);
                    rv = EXIT_FAILURE;
                }

                result = pStorage->get_tail(&key, &pValue);

                if (result == CACHE_RESULT_OK)
                {
                    if (key != cache_items[0].first)
                    {
                        out() << "First put value is not the tail." << endl;
                    }
                    else if (gwbuf_compare(pValue, cache_items[0].second))
                    {
                        out() << "Obtained value not the same as that which was put." << endl;
                        rv = EXIT_FAILURE;
                    }

                    gwbuf_free(pValue);
                }
            }
            else
            {
                ss_dassert(!true);
                rv = EXIT_FAILURE;
            }
        }

        CACHE_KEY key;
        GWBUF* pValue;
        result = pStorage->get_tail(&key, &pValue);

        if (result == CACHE_RESULT_OK)
        {
            if (key != cache_items[0].first)
            {
                out() << "First put value is not the tail." << endl;
            }

            gwbuf_free(pValue);
        }

        delete pStorage;
    }

    return rv;
}

int TesterLRUStorage::test_max_count(size_t n_threads, size_t n_seconds,
                                     const CacheItems& cache_items, uint64_t size)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage;

    size_t max_count = cache_items.size() / 4;

    out() << "LRU max-count: " << max_count << "\n" << endl;

    CacheStorageConfig config(CACHE_THREAD_MODEL_MT);
    config.max_count = max_count;

    pStorage = get_storage(config);

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

int TesterLRUStorage::test_max_size(size_t n_threads, size_t n_seconds,
                                    const CacheItems& cache_items, uint64_t size)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage;

    size_t max_size = size / 10;

    out() << "LRU max-size: " << max_size << "\n" << endl;

    CacheStorageConfig config(CACHE_THREAD_MODEL_MT);
    config.max_size = max_size;

    pStorage = get_storage(config);

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

int TesterLRUStorage::test_max_count_and_size(size_t n_threads, size_t n_seconds,
                                              const CacheItems& cache_items, uint64_t size)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage;

    size_t max_count = cache_items.size() / 4;
    size_t max_size = size / 10;

    out() << "LRU max-count: " << max_count << "\n" << endl;
    out() << "LRU max-size : " << max_size << "\n" << endl;

    CacheStorageConfig config(CACHE_THREAD_MODEL_MT);
    config.max_count = max_count;
    config.max_size = max_size;

    pStorage = get_storage(config);

    if (pStorage)
    {
        rv = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);

        ss_debug(cache_result_t result);
        uint64_t items;
        ss_debug(result = ) pStorage->get_items(&items);
        ss_dassert(result == CACHE_RESULT_OK);

        out() << "Max count: " << max_count << ", count: " << items << "." << endl;

        if (items > max_count)
        {
            rv = EXIT_FAILURE;
        }

        uint64_t size;
        ss_debug(result = ) pStorage->get_size(&size);
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
