/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <mutex>

#include "lrustorage.hh"

class LRUStorageMT : public LRUStorage
{
public:
    ~LRUStorageMT();

    static LRUStorageMT* create(const Config& config, Storage* pstorage);

    cache_result_t get_info(uint32_t what,
                            json_t** ppInfo) const override final;

    cache_result_t get_value(Token* pToken,
                             const CacheKey& key,
                             uint32_t flags,
                             uint32_t soft_ttl,
                             uint32_t hard_ttl,
                             GWBUF** ppValue,
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
                              const std::vector<std::string>& invalidation_words,
                              const std::function<void (cache_result_t)>& cb) override final;

    cache_result_t clear(Token* pToken) override final;

    cache_result_t get_head(CacheKey* pKey,
                            GWBUF** ppValue) override final;

    cache_result_t get_tail(CacheKey* pKey,
                            GWBUF** ppValue) override final;

    cache_result_t get_size(uint64_t* pSize) const override final;

    cache_result_t get_items(uint64_t* pItems) const override final;

private:
    LRUStorageMT(const Config& config, Storage* pStorage);

    LRUStorageMT(const LRUStorageMT&);
    LRUStorageMT& operator=(const LRUStorageMT&);

private:
    mutable std::mutex m_lock;
};
