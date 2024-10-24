/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
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

namespace
{

unsigned int millisleep(unsigned int milliseconds)
{
    unsigned int seconds = milliseconds / 1000;
    milliseconds -= (seconds * 1000);

    // Parentheses required by uncrustify, otherwise it'll think it's a pointer
    timespec req = {seconds, (milliseconds * 1000000)};
    timespec rem = {0, 0};

    nanosleep(&req, &rem);

    return rem.tv_sec * 1000 + rem.tv_nsec / 1000000;
}
}

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
    MXB_AT_DEBUG(bool rv=) m_storage.create_token(&m_sToken);
    mxb_assert(rv);
    mxb_assert(m_cache_items.size() > 0);
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
                std::vector<std::string> invalidation_words;
                cache_result_t result = m_storage.put_value(m_sToken.get(),
                                                            cache_item.first,
                                                            invalidation_words,
                                                            cache_item.second);
                if (CACHE_RESULT_IS_OK(result))
                {
                    ++m_puts;
                }
                else
                {
                    mxb_assert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        case STORAGE_GET:
            {
                GWBUF query;
                cache_result_t result = m_storage.get_value(m_sToken.get(), cache_item.first, 0, &query);

                if (CACHE_RESULT_IS_OK(result))
                {
                    mxb_assert(query.compare(cache_item.second) == 0);
                    ++m_gets;
                }
                else if (CACHE_RESULT_IS_NOT_FOUND(result))
                {
                    ++m_misses;
                }
                else
                {
                    mxb_assert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        case STORAGE_DEL:
            {
                cache_result_t result = m_storage.del_value(m_sToken.get(), cache_item.first);

                if (CACHE_RESULT_IS_OK(result))
                {
                    ++m_dels;
                }
                else if (CACHE_RESULT_IS_NOT_FOUND(result))
                {
                    ++m_misses;
                }
                else
                {
                    mxb_assert(!true);
                    rv = EXIT_FAILURE;
                }
            }
            break;

        default:
            mxb_assert(!true);
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

int TesterStorage::run(size_t n_threads, size_t n_seconds, size_t n_max_items, std::istream& in)
{
    int rv = EXIT_FAILURE;

    CacheItems cache_items;

    if (get_cache_items(in, n_max_items, m_factory, &cache_items))
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
        size_t size = n_min_size + static_cast<size_t>((static_cast<double>(random()) / RAND_MAX)
                                                       * (n_max_size - n_min_size));
        mxb_assert(size >= n_min_size);
        mxb_assert(size <= n_max_size);

        CacheKey key;

        key.data_hash = i;
        key.full_hash = i;

        vector<uint8_t> value(size, static_cast<uint8_t>(i));

        GWBUF buf = mariadb::create_packet(0, value.data(), value.size());

        if (buf)
        {
            cache_items.push_back(std::make_pair(key, std::move(buf)));
        }
        else
        {
            rv = EXIT_FAILURE;
        }

        ++i;
    }

    if (rv == EXIT_SUCCESS)
    {
        rv = execute(n_threads, n_seconds, cache_items);
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

// static
int TesterStorage::test_smoke(const CacheItems& cache_items)
{
    return test_ttl(cache_items);
}

int TesterStorage::test_ttl(const CacheItems& cache_items)
{
    Storage::Config config;

    out() << "ST" << endl;

    config.thread_model = CACHE_THREAD_MODEL_ST;
    config.hard_ttl = 6000;
    config.soft_ttl = 3000;

    Storage* pStorage;

    int rv1 = EXIT_FAILURE;
    pStorage = get_storage(config);

    if (pStorage)
    {
        rv1 = test_ttl(cache_items, *pStorage);
        delete pStorage;
    }

    out() << "MT" << endl;

    config.thread_model = CACHE_THREAD_MODEL_MT;
    config.hard_ttl = 6000;
    config.soft_ttl = 3000;

    int rv2 = EXIT_FAILURE;
    pStorage = get_storage(config);

    if (pStorage)
    {
        rv2 = test_ttl(cache_items, *pStorage);
        delete pStorage;
    }

    return combine_rvs(rv1, rv2);
}

int TesterStorage::test_ttl(const CacheItems& cache_items, Storage& storage)
{
    int rv = EXIT_SUCCESS;

    out() << "Testing ttl." << endl;

    shared_ptr<Storage::Token> sToken;
    MXB_AT_DEBUG(bool created=) storage.create_token(&sToken);
    mxb_assert(created);

    Storage::Config config;
    storage.get_config(&config);

    uint32_t hard_ttl = config.hard_ttl;
    uint32_t soft_ttl = config.soft_ttl;
    uint32_t diff = hard_ttl - soft_ttl;

    if (diff != 0)
    {
        mxb_assert(cache_items.size() > 0);

        out() << "Hard TTL: " << hard_ttl << endl;
        out() << "Soft TTL: " << soft_ttl << endl;

        uint32_t slept = 0;

        const CacheItems::value_type& cache_item = cache_items[0];

        std::vector<std::string> invalidation_words;
        cache_result_t result = storage.put_value(sToken.get(),
                                                  cache_item.first,
                                                  invalidation_words,
                                                  cache_item.second);

        if (!CACHE_RESULT_IS_OK(result))
        {
            out() << "Could not put item." << endl;
            rv = EXIT_FAILURE;
        }
        else
        {
            // Let's stay just below the soft_ttl value.
            millisleep(soft_ttl - 1000);
            slept += soft_ttl - 1000;

            GWBUF value;
            result = storage.get_value(sToken.get(), cache_item.first, 0, &value);

            // We should get the item normally as we are below the soft ttl, i.e. no stale bit.
            if (result != CACHE_RESULT_OK)
            {
                out() << "Expected to be found, and without stale bit." << endl;
                rv = EXIT_FAILURE;
            }

            millisleep(2000);   // Expected to get us passed the soft ttl.
            slept += 2000;

            value.clear();
            result = storage.get_value(sToken.get(), cache_item.first, 0, &value);

            // We should not get the item and the stale bit should be on.
            if (!(CACHE_RESULT_IS_NOT_FOUND(result) && CACHE_RESULT_IS_STALE(result)))
            {
                out() << "Expected not to be found, and with stale bit." << endl;
                rv = EXIT_FAILURE;
            }

            value.clear();
            result = storage.get_value(sToken.get(), cache_item.first, CACHE_FLAGS_INCLUDE_STALE, &value);

            if (!(CACHE_RESULT_IS_OK(result) && CACHE_RESULT_IS_STALE(result)))
            {
                out() << "Expected to be found, and with stale bit." << endl;
                rv = EXIT_FAILURE;
            }

            millisleep(hard_ttl - slept + 1000);    // Expected to get us passed the hard ttl.
            slept += hard_ttl - slept + 1000;

            value.clear();
            result = storage.get_value(sToken.get(), cache_item.first, CACHE_FLAGS_INCLUDE_STALE, &value);

            if (!CACHE_RESULT_IS_NOT_FOUND(result))
            {
                out() << "Expected not to be found, and without stale bit." << endl;
                rv = EXIT_FAILURE;
            }

            value.clear();
            result = storage.get_value(sToken.get(), cache_item.first, 0, &value);

            if (!CACHE_RESULT_IS_NOT_FOUND(result))
            {
                out() << "Expected not to be found, and without stale bit." << endl;
                rv = EXIT_FAILURE;
            }
        }
    }
    else
    {
        out() << "No difference between soft and hard ttl, not testing." << endl;
    }

    return rv;
}
