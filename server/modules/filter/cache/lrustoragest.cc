/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "lrustoragest.hh"

LRUStorageST::LRUStorageST(const Config& config, Storage* pStorage)
    : LRUStorage(config, pStorage)
{
    MXS_NOTICE("Created single threaded LRU storage.");
}

LRUStorageST::~LRUStorageST()
{
}

LRUStorageST* LRUStorageST::create(const Config& config, Storage* pStorage)
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

cache_result_t LRUStorageST::get_value(Token* pToken,
                                       const CacheKey& key,
                                       uint32_t flags,
                                       uint32_t soft_ttl,
                                       uint32_t hard_ttl,
                                       GWBUF** ppValue,
                                       const std::function<void (cache_result_t, GWBUF*)>&)
{
    return LRUStorage::do_get_value(pToken, key, flags, soft_ttl, hard_ttl, ppValue);
}

cache_result_t LRUStorageST::put_value(Token* pToken,
                                       const CacheKey& key,
                                       const std::vector<std::string>& invalidation_words,
                                       const GWBUF* pValue,
                                       const std::function<void (cache_result_t)>&)
{
    return LRUStorage::do_put_value(pToken, key, invalidation_words, pValue);
}

cache_result_t LRUStorageST::del_value(Token* pToken,
                                       const CacheKey& key,
                                       const std::function<void (cache_result_t)>&)
{
    return LRUStorage::do_del_value(pToken, key);
}

cache_result_t LRUStorageST::invalidate(Token* pToken,
                                        const std::vector<std::string>& words,
                                        const std::function<void (cache_result_t)>&)
{
    return LRUStorage::do_invalidate(pToken, words);
}

cache_result_t LRUStorageST::clear(Token* pToken)
{
    return LRUStorage::do_clear(pToken);
}

cache_result_t LRUStorageST::get_head(CacheKey* pKey, GWBUF** ppValue)
{
    return LRUStorage::do_get_head(pKey, ppValue);
}

cache_result_t LRUStorageST::get_tail(CacheKey* pKey, GWBUF** ppValue)
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
