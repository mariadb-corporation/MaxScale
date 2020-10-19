/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "storage_inmemory"
#include "inmemorystoragest.hh"

using std::unique_ptr;

InMemoryStorageST::InMemoryStorageST(const std::string& name,
                                     const Config& config)
    : InMemoryStorage(name, config)
{
}

InMemoryStorageST::~InMemoryStorageST()
{
}

unique_ptr<InMemoryStorageST> InMemoryStorageST::create(const std::string& name,
                                                        const Config& config)
{
    return unique_ptr<InMemoryStorageST>(new InMemoryStorageST(name, config));
}

cache_result_t InMemoryStorageST::get_info(uint32_t what, json_t** ppInfo) const
{
    return do_get_info(what, ppInfo);
}

cache_result_t InMemoryStorageST::get_value(Token* pToken,
                                            const CacheKey& key,
                                            uint32_t flags,
                                            uint32_t soft_ttl,
                                            uint32_t hard_ttl,
                                            GWBUF** ppResult,
                                            const std::function<void (cache_result_t, GWBUF*)>&)
{
    return do_get_value(pToken, key, flags, soft_ttl, hard_ttl, ppResult);
}

cache_result_t InMemoryStorageST::put_value(Token* pToken,
                                            const CacheKey& key,
                                            const std::vector<std::string>& invalidation_words,
                                            const GWBUF* pValue,
                                            const std::function<void (cache_result_t)>&)
{
    return do_put_value(pToken, key, invalidation_words, pValue);
}

cache_result_t InMemoryStorageST::del_value(Token* pToken,
                                            const CacheKey& key,
                                            const std::function<void (cache_result_t)>&)
{
    return do_del_value(pToken, key);
}

cache_result_t InMemoryStorageST::invalidate(Token* pToken,
                                             const std::vector<std::string>& words,
                                             const std::function<void (cache_result_t)>&)
{
    return do_invalidate(pToken, words);
}

cache_result_t InMemoryStorageST::clear(Token* pToken)
{
    return do_clear(pToken);
}
