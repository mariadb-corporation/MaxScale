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

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystoragest.hh"

using std::auto_ptr;

InMemoryStorageST::InMemoryStorageST(const std::string& name,
                                     const CACHE_STORAGE_CONFIG& config)
    : InMemoryStorage(name, config)
{
}

InMemoryStorageST::~InMemoryStorageST()
{
}

auto_ptr<InMemoryStorageST> InMemoryStorageST::Create(const std::string& name,
                                                      const CACHE_STORAGE_CONFIG& config,
                                                      int argc,
                                                      char* argv[])
{
    return auto_ptr<InMemoryStorageST>(new InMemoryStorageST(name, config));
}

cache_result_t InMemoryStorageST::get_info(uint32_t what, json_t** ppInfo) const
{
    return do_get_info(what, ppInfo);
}

cache_result_t InMemoryStorageST::get_value(const CACHE_KEY& key,
                                            uint32_t flags,
                                            uint32_t soft_ttl,
                                            uint32_t hard_ttl,
                                            GWBUF**  ppResult)
{
    return do_get_value(key, flags, soft_ttl, hard_ttl, ppResult);
}

cache_result_t InMemoryStorageST::put_value(const CACHE_KEY& key, const GWBUF& value)
{
    return do_put_value(key, value);
}

cache_result_t InMemoryStorageST::del_value(const CACHE_KEY& key)
{
    return do_del_value(key);
}
