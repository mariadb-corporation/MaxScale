/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-12-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include "inmemorystorage.hh"

class InMemoryStorageST : public InMemoryStorage
{
public:
    ~InMemoryStorageST();

    typedef std::unique_ptr<InMemoryStorageST> SInMemoryStorageST;

    static SInMemoryStorageST create(const std::string& name,
                                     const Config& config);

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
                              const std::vector<std::string>& words,
                              std::function<void (cache_result_t)> cb) override final;
    cache_result_t clear(Token* pToken) override final;

private:
    InMemoryStorageST(const std::string& name, const Config& config);

private:
    InMemoryStorageST(const InMemoryStorageST&);
    InMemoryStorageST& operator=(const InMemoryStorageST&);
};
