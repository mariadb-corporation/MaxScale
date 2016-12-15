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
#include "storagefactory.hh"
#include "testerrawstorage.hh"


TesterRawStorage::TesterRawStorage(std::ostream* pOut, StorageFactory* pFactory)
    : TesterStorage(pOut, pFactory)
{
}

Storage* TesterRawStorage::get_storage()
{
    return m_factory.createRawStorage(CACHE_THREAD_MODEL_MT,
                                      "unspecified",
                                      0, // No TTL
                                      0, // No max count
                                      0, // No max size
                                      0, NULL);
}

size_t TesterRawStorage::get_n_items(size_t n_threads, size_t n_seconds)
{
    return n_threads * n_seconds * 10; // From the sleeve...
}
