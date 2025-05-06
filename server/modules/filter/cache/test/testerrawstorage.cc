/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "testerrawstorage.hh"
#include "storage.hh"
#include "storagefactory.hh"


TesterRawStorage::TesterRawStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

int TesterRawStorage::execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    auto smoke_res = run_task([&](){
        return test_smoke(cache_items);
    });

    auto task_res = run_task([&](){
        int rv = EXIT_FAILURE;
        Storage::Config config(CACHE_THREAD_MODEL_MT);

        Storage* pStorage = get_storage(config);

        if (pStorage)
        {
            rv = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);
            delete pStorage;
        }

        return rv;
    });

    int rv1 = smoke_res.get();
    int rv2 = task_res.get();

    return combine_rvs(rv1, rv2);
}

Storage* TesterRawStorage::get_storage(const Storage::Config& config) const
{
    return m_factory.create_raw_storage("unspecified", config);
}
