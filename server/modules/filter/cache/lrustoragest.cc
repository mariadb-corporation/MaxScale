/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "lrustoragest.hh"

LRUStorageST::LRUStorageST(const CACHE_STORAGE_CONFIG& config, Storage* pStorage)
    : LRUStorage(config, pStorage)
{
    MXS_NOTICE("Created single threaded LRU storage.");
}

LRUStorageST::~LRUStorageST()
{
}

LRUStorageST* LRUStorageST::create(const CACHE_STORAGE_CONFIG& config, Storage* pStorage)
{
    LRUStorageST* plru_storage = NULL;

    MXS_EXCEPTION_GUARD(plru_storage = new LRUStorageST(config, pStorage));

    return plru_storage;
}

cache_result_t LRUStorageST::get_info(uint32_t what,
                                      json_t** ppInfo) const
{
    return LRUStorage::do_get_info(what, ppInfo);
}

cache_result_t LRUStorageST::get_value(const CACHE_KEY& key,
                                       uint32_t flags,
                                       GWBUF** ppValue) const
{
    return LRUStorage::do_get_value(key, flags, ppValue);
}

cache_result_t LRUStorageST::put_value(const CACHE_KEY& key, const GWBUF* pValue)
{
    return LRUStorage::do_put_value(key, pValue);
}

cache_result_t LRUStorageST::del_value(const CACHE_KEY& key)
{
    return LRUStorage::do_del_value(key);
}

cache_result_t LRUStorageST::get_head(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    return LRUStorage::do_get_head(pKey, ppValue);
}

cache_result_t LRUStorageST::get_tail(CACHE_KEY* pKey, GWBUF** ppValue) const
{
    return LRUStorage::do_get_tail(pKey, ppValue);
}

cache_result_t LRUStorageST::get_size(uint64_t* pSize) const
{
    return LRUStorage::do_get_size(pSize);
}

cache_result_t LRUStorageST::get_items(uint64_t* pItems) const
{
    return LRUStorage::do_get_items(pItems);
}
