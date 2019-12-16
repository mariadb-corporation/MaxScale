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
#pragma once

#include <maxscale/ccdefs.hh>
#include "../../cache_storage_api.hh"

class MemcachedStorage : public Storage
{
public:
    MemcachedStorage(const MemcachedStorage&) = delete;
    MemcachedStorage& operator=(const MemcachedStorage&) = delete;

    static bool initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities);
    static void finalize();

    ~MemcachedStorage();

    static MemcachedStorage* create(const std::string& name,
                                    const Config& config,
                                    int argc,
                                    char* argv[]);

    bool create_token(std::unique_ptr<Token>* psToken) override final;

    void get_config(Config* pConfig) override final;

    cache_result_t get_info(uint32_t what, json_t** ppInfo) const override final;
    cache_result_t get_value(Token* pToken,
                             const CACHE_KEY& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppResult,
                             std::function<void (cache_result_t, GWBUF*)> cb) override final;
    cache_result_t put_value(Token* pToken,
                             const CACHE_KEY& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             std::function<void (cache_result_t)> cb) override final;
    cache_result_t del_value(Token* pToken,
                             const CACHE_KEY& key,
                             std::function<void (cache_result_t)> cb) override final;
    cache_result_t invalidate(Token* pToken,
                              const std::vector<std::string>& words) override final;
    cache_result_t clear(Token* pToken) override final;

    cache_result_t get_head(CACHE_KEY* pKey, GWBUF** ppHead) override final;
    cache_result_t get_tail(CACHE_KEY* pKey, GWBUF** ppHead) override final;
    cache_result_t get_size(uint64_t* pSize) const override final;
    cache_result_t get_items(uint64_t* pItems) const override final;

private:
    MemcachedStorage(const std::string& name,
                     const Config& config,
                     const std::string& memcached_config);

    std::string  m_name;
    const Config m_config;
    std::string  m_memcached_config;
};
