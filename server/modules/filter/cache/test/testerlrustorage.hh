#pragma once
#pragma once
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
#include "testerstorage.hh"


class TesterLRUStorage : public TesterStorage
{
public:
    /**
     * Constructor
     *
     * @param pOut      Pointer to the stream to be used for (user) output.
     * @param pFactory  Pointer to factory to be used.
     */
    TesterLRUStorage(std::ostream* pOut, StorageFactory* pFactory);

    /**
     * @see TesterStorage::run
     */
    int execute(size_t n_threads, size_t n_seconds, const CacheItems& cache_items);

private:
    TesterLRUStorage(const TesterLRUStorage&);
    TesterLRUStorage& operator = (const TesterLRUStorage&);
};
