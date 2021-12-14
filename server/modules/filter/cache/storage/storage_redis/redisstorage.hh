/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "../../cache_storage_api.hh"

class RedisStorage : public Storage
{
public:
    RedisStorage(const RedisStorage&) = delete;
    RedisStorage& operator=(const RedisStorage&) = delete;

    static bool initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities);
    static void finalize();

    ~RedisStorage();

    static RedisStorage* create(const std::string& name,
                                const Config& config,
                                const std::string& arguments);

    bool create_token(std::shared_ptr<Token>* psToken) override final;

    void get_config(Config* pConfig) override final;
    void get_limits(Limits* pLimits) override final;

    cache_result_t get_info(uint32_t what, json_t** ppInfo) const override final;
    cache_result_t get_value(Token* pToken,
                             const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppResult,
                             const std::function<void (cache_result_t, GWBUF*)>& cb) override final;
    cache_result_t put_value(Token* pToken,
                             const CacheKey& key,
                             const std::vector<std::string>& invalidation_words,
                             const GWBUF* pValue,
                             const std::function<void (cache_result_t)>& cb) override final;
    cache_result_t del_value(Token* pToken,
                             const CacheKey& key,
                             const std::function<void (cache_result_t)>& cb) override final;
    cache_result_t invalidate(Token* pToken,
                              const std::vector<std::string>& words,
                              const std::function<void (cache_result_t)>& cb) override final;
    cache_result_t clear(Token* pToken) override final;

    cache_result_t get_head(CacheKey* pKey, GWBUF** ppHead) override final;
    cache_result_t get_tail(CacheKey* pKey, GWBUF** ppHead) override final;
    cache_result_t get_size(uint64_t* pSize) const override final;
    cache_result_t get_items(uint64_t* pItems) const override final;

private:
    RedisStorage(const std::string& name,
                 const Config& config,
                 const std::string& host,
                 int port);

    const std::string m_name;
    const Config      m_config;
    const std::string m_host;
    const int         m_port;
    bool              m_invalidate;
    uint32_t          m_ttl;
};
