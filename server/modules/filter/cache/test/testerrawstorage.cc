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

#include "testerrawstorage.hh"
#include "storage.hh"
#include "storagefactory.hh"


TesterRawStorage::TesterRawStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

int TesterRawStorage::execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items)
{
    int rv = EXIT_FAILURE;

    Storage* pStorage = m_factory.createRawStorage(CACHE_THREAD_MODEL_MT,
                                                   "unspecified",
                                                   0, // No TTL
                                                   0, // No max count
                                                   0, // No max size
                                                   0, NULL);

    if (pStorage)
    {
        rv = execute_tasks(n_threads, n_seconds, cache_items, *pStorage);
        delete pStorage;
    }

    return rv;
}
