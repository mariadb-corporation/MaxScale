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

#include "testerrawstorage.hh"
#include "storage.hh"
#include "storagefactory.hh"


TesterRawStorage::TesterRawStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

int TesterRawStorage::execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    int rv1 = test_smoke(cache_items);

    int rv2 = EXIT_FAILURE;
    CacheStorageConfig config(CACHE_THREAD_MODEL_MT);

    Storage* pStorage = get_storage(config);

    if (pStorage)
    {
        rv2 = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);
        delete pStorage;
    }

    return combine_rvs(rv1, rv2);
}

Storage* TesterRawStorage::get_storage(const CACHE_STORAGE_CONFIG& config) const
{
    return m_factory.createRawStorage("unspecified", config);
}

