#pragma once
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

#include <maxscale/cppdefs.hh>
#include "testerstorage.hh"


class TesterRawStorage : public TesterStorage
{
public:
    /**
     * Constructor
     *
     * @param pOut      Pointer to the stream to be used for (user) output.
     * @param pFactory  Pointer to factory to be used.
     */
    TesterRawStorage(std::ostream* pOut, StorageFactory* pFactory);

    /**
     * @see TesterStorage::run
     */
    int execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items);

    /**
     * @see TesterStorage::get_storage
     */
    Storage* get_storage(const CACHE_STORAGE_CONFIG& config) const;

private:
    TesterRawStorage(const TesterRawStorage&);
    TesterRawStorage& operator = (const TesterRawStorage&);
};
