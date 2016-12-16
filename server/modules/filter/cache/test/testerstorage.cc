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

#include "testerstorage.hh"
#include <algorithm>
#include <sstream>
#include "storage.hh"

using namespace std;

//
// class TesterStorage::HitTask
//

TesterStorage::HitTask::HitTask(ostream* pOut,
                                Storage* pStorage,
                                const CacheItems* pCache_items)
    : Tester::Task(pOut)
    , m_storage(*pStorage)
    , m_cache_items(*pCache_items)
    , m_puts(0)
    , m_gets(0)
    , m_dels(0)
    , m_misses(0)
{
    ss_dassert(m_cache_items.size() > 0);
}

int TesterStorage::HitTask::run()
{
    int rv = EXIT_SUCCESS;

    size_t n = m_cache_items.size();
    size_t i = 0;

    while (!should_terminate())
    {
        if (i >= n)
        {
            i = 0;
        }

        const CacheItems::value_type& cache_item = m_cache_items[i];

        storage_action_t action = TesterStorage::get_random_action();

        switch (action)
        {
        case STORAGE_PUT:
            {
                cache_result_t result = m_storage.put_value(cache_item.first, cache_item.second);
                if (result == CACHE_RESULT_OK)
                {
                    ++m_puts;
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
                cache_result_t result = m_storage.get_value(cache_item.first, 0, &pQuery);

                if (result == CACHE_RESULT_OK)
                {
                    ss_dassert(GWBUF_LENGTH(pQuery) == GWBUF_LENGTH(cache_item.second));
                    ss_dassert(memcmp(GWBUF_DATA(pQuery), GWBUF_DATA(cache_item.second),
                                      GWBUF_LENGTH(pQuery)) == 0);

                    gwbuf_free(pQuery);
                    ++m_gets;
                }
                else if (result == CACHE_RESULT_NOT_FOUND)
                {
                    ++m_misses;
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
                cache_result_t result = m_storage.del_value(cache_item.first);

                if (result == CACHE_RESULT_OK)
                {
                    ++m_dels;
                }
                else if (result == CACHE_RESULT_NOT_FOUND)
                {
                    ++m_misses;
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

    stringstream ss;
    ss << "HitTask ending: "
       << m_gets << ", " << m_puts << ", " << m_dels << ", " << m_misses << "\n";

    out() << ss.str() << flush;

    return rv;
}

//
// class TesterStorage
//

TesterStorage::TesterStorage(std::ostream* pOut, StorageFactory* pFactory)
    : Tester(pOut)
    , m_factory(*pFactory)
{
}

int TesterStorage::run(size_t n_threads, size_t n_seconds, std::istream& in)
{
    int rv = EXIT_FAILURE;

    size_t n_items = get_n_items(n_threads, n_seconds);

    CacheItems cache_items;

    if (get_cache_items(in, n_items, m_factory, &cache_items))
    {
        rv = execute(n_threads, n_seconds, cache_items);
    }

    return rv;
}

int TesterStorage::run(size_t n_threads,
                       size_t n_seconds,
                       size_t n_items,
                       size_t n_min_size,
                       size_t n_max_size)
{
    int rv = EXIT_SUCCESS;

    CacheItems cache_items;

    size_t i = 0;

    while ((rv == EXIT_SUCCESS) && (i < n_items))
    {
        size_t size = n_min_size + ((static_cast<double>(random()) / RAND_MAX) * (n_max_size - n_min_size));
        ss_dassert(size >= n_min_size);
        ss_dassert(size <= n_max_size);

        CacheKey key;

        sprintf(key.data, "%lu", i);

        vector<uint8_t> value(size, static_cast<uint8_t>(i));

        GWBUF* pBuf = gwbuf_from_vector(value);

        if (pBuf)
        {
            cache_items.push_back(std::make_pair(key, pBuf));
        }
        else
        {
            rv = EXIT_FAILURE;
        }
    }

    clear_cache_items(cache_items);

    return rv;
}

int TesterStorage::execute_tasks(size_t n_threads,
                                 size_t n_seconds,
                                 const CacheItems& cache_items,
                                 Storage& storage)
{
    int rv = EXIT_FAILURE;

    // Just one, for now.
    rv = execute_hit_task(n_threads, n_seconds, cache_items, storage);

    return rv;
}

int TesterStorage::execute_hit_task(size_t n_threads,
                                    size_t n_seconds,
                                    const CacheItems& cache_items,
                                    Storage& storage)
{
    int rv = EXIT_FAILURE;

    Tasks tasks;

    for (size_t i = 0; i < n_threads; ++i)
    {
        tasks.push_back(new HitTask(&out(), &storage, &cache_items));
    }

    rv = Tester::execute(out(), n_seconds, tasks);

    for_each(tasks.begin(), tasks.end(), Task::free);

    return rv;
}

// static
TesterStorage::storage_action_t TesterStorage::get_random_action()
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

size_t TesterStorage::get_n_items(size_t n_threads, size_t n_seconds)
{
    return n_threads * n_seconds * 10; // From the sleeve...
}
