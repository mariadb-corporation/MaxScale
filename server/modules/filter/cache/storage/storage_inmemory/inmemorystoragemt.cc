/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystoragemt.hh"

using maxscale::SpinLockGuard;
using std::auto_ptr;

InMemoryStorageMT::InMemoryStorageMT(const std::string& name,
                                     const CACHE_STORAGE_CONFIG& config)
    : InMemoryStorage(name, config)
{
    spinlock_init(&m_lock);
}

InMemoryStorageMT::~InMemoryStorageMT()
{
}

auto_ptr<InMemoryStorageMT> InMemoryStorageMT::Create(const std::string& name,
                                                      const CACHE_STORAGE_CONFIG& config,
                                                      int argc, char* argv[])
{
    return auto_ptr<InMemoryStorageMT>(new InMemoryStorageMT(name, config));
}

cache_result_t InMemoryStorageMT::get_info(uint32_t what, json_t** ppInfo) const
{
    SpinLockGuard guard(m_lock);

    return do_get_info(what, ppInfo);
}

cache_result_t InMemoryStorageMT::get_value(const CACHE_KEY& key,
                                            uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                                            GWBUF** ppResult)
{
    SpinLockGuard guard(m_lock);

    return do_get_value(key, flags, soft_ttl, hard_ttl, ppResult);
}

cache_result_t InMemoryStorageMT::put_value(const CACHE_KEY& key, const GWBUF& value)
{
    SpinLockGuard guard(m_lock);

    return do_put_value(key, value);
}

cache_result_t InMemoryStorageMT::del_value(const CACHE_KEY& key)
{
    SpinLockGuard guard(m_lock);

    return do_del_value(key);
}
