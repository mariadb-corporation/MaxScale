#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>
#include "tester.hh"

class Storage;
class StorageFactory;

class TesterStorage : public Tester
{
public:
    enum storage_action_t
    {
        STORAGE_PUT, /*< Put an item to the storage. */
        STORAGE_GET, /*< Get an item from the storage. */
        STORAGE_DEL  /*< Delete an item from the storage. */
    };

    /**
     * @class HitTask
     *
     * A task whose sole purpose is to hit a Storage continuously
     * and intensly.
     */
    class HitTask : public Tester::Task
    {
    public:
        /**
         * Constructor
         *
         * @param pOut          The stream to use for (user) output.
         * @param pStorage      The storage to hit.
         * @param pCache_items  The cache items to use when hitting the storage.
         */
        HitTask(std::ostream* pOut,
                Storage* pStorage,
                const CacheItems* pCache_items);

        /**
         * Runs continuously until the task is terminated.
         *
         * @return EXIT_SUCCESS or EXIT_FAILURE
         */
        int run();

    private:
        HitTask(const HitTask&);
        HitTask& operator = (const HitTask&);

    private:
        Storage& m_storage;               /*< The storage that is hit. */
        const CacheItems& m_cache_items;  /*< The cache items that are used. */
        size_t m_puts;                    /*< How many puts. */
        size_t m_gets;                    /*< How many gets. */
        size_t m_dels;                    /*< How many deletes. */
        size_t m_misses;                  /*< How many misses. */
    };

    /**
     * Reads statements from the provided stream, converts them to cache items and
     * runs all storage tasks using as many threads as specified for the specified
     * number of seconds.
     *
     * Will call back into the virtual @c execute function below.
     *
     * @param n_threads    How many threads to use.
     * @param n_seconds    For how many seconds to run the test.
     * @param n_max_items  How many items to read from the stream at most; 0 means
     *                     no limit.
     * @param in           Stream, assumed to refer to a file containing statements.
     *
     * @return EXIT_SUCCESS or EXIT_FAILURE.
     */
    virtual int run(size_t n_threads, size_t n_seconds, size_t n_max_items, std::istream& in);

    /**
     * Creates cache items with the size varying between the specified minimum
     * and maximum sizes.
     *
     * Will call back into the virtual @c execute function below.
     *
     * @param n_threads   How many threads to use.
     * @param n_seconds   For how many seconds to run the test.
     * @param n_items     How many items to create.
     * @param n_min_size  The minimum size of a cache value.
     * @param n_max_size  The maximum size of a cache value.
     *
     * @return EXIT_SUCCESS or EXIT_FAILURE.
     *
     */
    virtual int run(size_t n_threads,
                    size_t n_seconds,
                    size_t n_items,
                    size_t n_min_size,
                    size_t n_max_size);

    /**
     * Execute tests; implemented by derived class.
     *
     * @param n_threads    How many threads to use.
     * @param n_seconds    For how many seconds to run the test.
     * @param cache_items  The cache items to use.
     *
     * @return EXIT_SUCCESS or EXIT_FAILURE.
     */
    virtual int execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items) = 0;

    /**
     * Executes all tasks, using as many threads as specified, for the specified
     * number of seconds.
     *
     * @param n_threads    How many threads to use.
     * @param n_seconds    For how many seconds to run the test.
     * @param cache_items  The cache items to use.
     * @param storage      The storage to use.
     *
     * @return EXIT_SUCCESS or EXIT_FAILURE.
     */
    virtual int execute_tasks(size_t n_threads,
                              size_t n_seconds,
                              const CacheItems& cache_items,
                              Storage& storage);
    /**
     * Executes the HitTask using as many threads as specified, for the specified
     * number of seconds.
     *
     * @param n_threads    How many threads to use.
     * @param n_seconds    For how many seconds to run the test.
     * @param cache_items  The cache items to use.
     * @param storage      The storage to use.
     *
     * @return EXIT_SUCCESS or EXIT_FAILURE.
     */
    virtual int execute_hit_task(size_t n_threads,
                                 size_t n_seconds,
                                 const CacheItems& cache_items,
                                 Storage& storage);

    /**
     * Return a storage.
     *
     * @param config  The storage configuration
     *
     * @return A storage or NULL in case of error.
     */
    virtual Storage* get_storage(const CACHE_STORAGE_CONFIG& config) const = 0;

    /**
     * Get a random action.
     *
     * @return Some storage action.
     */
    static storage_action_t get_random_action();

    int test_smoke(const CacheItems& cache_items);

    int test_ttl(const CacheItems& cache_items);
    int test_ttl(const CacheItems& cache_items, Storage& storage);

protected:
    /**
     * Constructor
     *
     * @param pOut      Pointer to the stream to be used for (user) output.
     * @param pFactory  Pointer to factory to be used.
     */
    TesterStorage(std::ostream* pOut, StorageFactory* pFactory);

protected:
    StorageFactory& m_factory;  /*< The storage factory that is used. */

private:
    TesterStorage(const TesterStorage&);
    TesterStorage& operator = (const TesterStorage&);
};
