/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_redis"
#include "redisstorage.hh"
#include <hiredis/hiredis.h>

using std::string;

RedisStorage::RedisStorage(const string& name,
                           const Config& config)
    : m_name(name)
    , m_config(config)
{
}

RedisStorage::~RedisStorage()
{
}

//static
bool RedisStorage::initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities)
{
    *pKind = CACHE_STORAGE_SHARED;
    *pCapabilities = (CACHE_STORAGE_CAP_ST | CACHE_STORAGE_CAP_MT);
    return true;
}

//static
void RedisStorage::finalize()
{
}

//static
RedisStorage* RedisStorage::create(const string& name,
                                   const Config& config,
                                   const std::string& arguments)
{
    RedisStorage* pStorage = nullptr;

    pStorage = new (std::nothrow) RedisStorage(name, config);

    return pStorage;
}

bool RedisStorage::create_token(std::shared_ptr<Storage::Token>* psToken)
{
    return false;
}

void RedisStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

cache_result_t RedisStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_value(Storage::Token* pToken,
                                       const CACHE_KEY& key,
                                       uint32_t flags,
                                       uint32_t soft_ttl,
                                       uint32_t hard_ttl,
                                       GWBUF** ppValue,
                                       std::function<void (cache_result_t, GWBUF*)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::put_value(Token* pToken,
                                       const CACHE_KEY& key,
                                       const std::vector<string>& invalidation_words,
                                       const GWBUF* pValue,
                                       std::function<void (cache_result_t)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::del_value(Token* pToken,
                                       const CACHE_KEY& key,
                                       std::function<void (cache_result_t)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::invalidate(Token* pToken,
                                        const std::vector<string>& words)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::clear(Token* pToken)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_head(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_tail(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t RedisStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}
