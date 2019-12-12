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

#include "memcachedstorage.hh"


MemcachedStorage::MemcachedStorage(const std::string& name, const Config& config)
    : m_name(name)
    , m_config(config)
{
}

MemcachedStorage::~MemcachedStorage()
{
}

//static
bool MemcachedStorage::initialize(uint32_t* pCapabilities)
{
    *pCapabilities = CACHE_STORAGE_CAP_MT;
    return true;
}

//static
void MemcachedStorage::finalize()
{
}

//static
MemcachedStorage* MemcachedStorage::create(const std::string& name,
                                           const Config& config,
                                           int argc,
                                           char* argv[])
{
    return nullptr;
}

std::unique_ptr<Storage::Token> MemcachedStorage::create_token()
{
    return nullptr;
}

void MemcachedStorage::get_config(Config* pConfig)
{
    *pConfig = m_config;
}

cache_result_t MemcachedStorage::get_info(uint32_t what, json_t** ppInfo) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_value(Token* pToken,
                                           const CACHE_KEY& key,
                                           uint32_t flags,
                                           uint32_t soft_ttl,
                                           uint32_t hard_ttl,
                                           GWBUF** ppResult,
                                           std::function<void (cache_result_t, GWBUF*)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::put_value(Token* pToken,
                                           const CACHE_KEY& key,
                                           const std::vector<std::string>& invalidation_words,
                                           const GWBUF* pValue,
                                           std::function<void (cache_result_t)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::del_value(Token* pToken,
                                           const CACHE_KEY& key,
                                           std::function<void (cache_result_t)> cb)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::invalidate(Token* pToken,
                                            const std::vector<std::string>& words)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::clear(Token* pToken)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_head(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_tail(CACHE_KEY* pKey, GWBUF** ppHead)
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_size(uint64_t* pSize) const
{
    return CACHE_RESULT_ERROR;
}

cache_result_t MemcachedStorage::get_items(uint64_t* pItems) const
{
    return CACHE_RESULT_ERROR;
}

