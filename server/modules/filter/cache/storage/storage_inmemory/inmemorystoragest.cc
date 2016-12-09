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
#include "inmemorystoragest.hh"

using std::auto_ptr;

InMemoryStorageST::InMemoryStorageST(const std::string& name, uint32_t ttl)
    : InMemoryStorage(name, ttl)
{
}

InMemoryStorageST::~InMemoryStorageST()
{
}

// static
auto_ptr<InMemoryStorageST> InMemoryStorageST::create(const std::string& name,
                                                      uint32_t ttl,
                                                      int argc, char* argv[])
{
    return auto_ptr<InMemoryStorageST>(new InMemoryStorageST(name, ttl));
}

cache_result_t InMemoryStorageST::get_info(uint32_t what, json_t** ppinfo) const
{
    return do_get_info(what, ppinfo);
}

cache_result_t InMemoryStorageST::get_value(const CACHE_KEY& key, uint32_t flags, GWBUF** ppresult)
{
    return do_get_value(key, flags, ppresult);
}

cache_result_t InMemoryStorageST::put_value(const CACHE_KEY& key, const GWBUF* pvalue)
{
    return do_put_value(key, pvalue);
}

cache_result_t InMemoryStorageST::del_value(const CACHE_KEY& key)
{
    return do_del_value(key);
}
