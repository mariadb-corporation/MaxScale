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

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystoragemt.h"

InMemoryStorageMT::InMemoryStorageMT(const std::string& name, uint32_t ttl)
    : InMemoryStorage(name, ttl)
{
    spinlock_init(&lock_);
}

InMemoryStorageMT::~InMemoryStorageMT()
{
}

// static
InMemoryStorageMT* InMemoryStorageMT::create(const std::string& name,
                                             uint32_t ttl,
                                             int argc, char* argv[])
{
    return new InMemoryStorageMT(name, ttl);
}

cache_result_t InMemoryStorageMT::get_info(uint32_t what, json_t** ppInfo) const
{
    LockGuard guard(&lock_);

    return do_get_info(what, ppInfo);
}

cache_result_t InMemoryStorageMT::get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult)
{
    LockGuard guard(&lock_);

    return do_get_value(key, flags, ppresult);
}

cache_result_t InMemoryStorageMT::put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    LockGuard guard(&lock_);

    return do_put_value(key, pvalue);
}

cache_result_t InMemoryStorageMT::del_value(const CACHE_KEY& key)
{
    LockGuard guard(&lock_);

    return do_del_value(key);
}
