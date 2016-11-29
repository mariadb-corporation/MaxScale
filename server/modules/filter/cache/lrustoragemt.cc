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

#define MXS_MODULE_NAME "cache"
#include "lrustoragemt.h"

LRUStorageMT::LRUStorageMT(Storage* pstorage, size_t max_count, size_t max_size)
    : LRUStorage(pstorage, max_count, max_size)
{
    spinlock_init(&lock_);

    MXS_NOTICE("Created multi threaded LRU storage.");
}

LRUStorageMT::~LRUStorageMT()
{
}

LRUStorageMT* LRUStorageMT::create(Storage* pstorage, size_t max_count, size_t max_size)
{
    LRUStorageMT* plru_storage = NULL;

    CPP_GUARD(plru_storage = new LRUStorageMT(pstorage, max_count, max_size));

    return plru_storage;
}

cache_result_t LRUStorageMT::get_value(const CACHE_KEY& key,
                                       uint32_t flags,
                                       GWBUF** ppvalue)
{
    spinlock_acquire(&lock_);
    cache_result_t rv =  LRUStorage::do_get_value(key, flags, ppvalue);
    spinlock_release(&lock_);

    return rv;
}

cache_result_t LRUStorageMT::put_value(const CACHE_KEY& key,
                                       const GWBUF* pvalue)
{
    spinlock_acquire(&lock_);
    cache_result_t rv =  LRUStorage::do_put_value(key, pvalue);
    spinlock_release(&lock_);

    return rv;
}

cache_result_t LRUStorageMT::del_value(const CACHE_KEY& key)
{
    spinlock_acquire(&lock_);
    cache_result_t rv =  LRUStorage::do_del_value(key);
    spinlock_release(&lock_);

    return rv;
}
