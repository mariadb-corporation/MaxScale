/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include "../../cache_storage_api.hh"

class InMemoryStorage : public Storage
{
public:
    virtual ~InMemoryStorage();

    static bool initialize(cache_storage_kind_t* pKind, uint32_t* pCapabilities);
    static void finalize();

    static InMemoryStorage* create(const char* zName,
                                   const Config& config,
                                   const std::string& arguments);

    bool create_token(std::shared_ptr<Token>* psToken) override final;

    void get_config(Config* pConfig) override final;
    void get_limits(Limits* pLimits) override final;

    cache_result_t get_head(CacheKey* pKey, GWBUF** ppHead) override final;
    cache_result_t get_tail(CacheKey* pKey, GWBUF** ppHead) override final;
    cache_result_t get_size(uint64_t* pSize) const override final;
    cache_result_t get_items(uint64_t* pItems) const override final;

protected:
    InMemoryStorage(const std::string& name,
                    const Config& config);

    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;
    cache_result_t do_get_value(Token* pToken,
                                const CacheKey& key,
                                uint32_t flags,
                                uint32_t soft_ttl,
                                uint32_t hard_ttl,
                                GWBUF** ppResult);
    cache_result_t do_put_value(Token* pToken,
                                const CacheKey& key,
                                const std::vector<std::string>& invalidation_words,
                                const GWBUF* pValue);
    cache_result_t do_del_value(Token* pToken,
                                const CacheKey& key);
    cache_result_t do_invalidate(Token* pToken,
                                 const std::vector<std::string>& words);
    cache_result_t do_clear(Token* pToken);

private:
    InMemoryStorage(const InMemoryStorage&);
    InMemoryStorage& operator=(const InMemoryStorage&);

private:
    typedef std::vector<uint8_t> Value;

    struct Entry
    {
        Entry()
            : time(0)
        {
        }

        int64_t time;
        Value   value;
    };

    struct Stats
    {
        Stats()
            : size(0)
            , items(0)
            , hits(0)
            , misses(0)
            , updates(0)
            , deletes(0)
        {
        }

        void fill(json_t* pObject) const;

        uint64_t size;      /*< The total size of the stored values. */
        uint64_t items;     /*< The number of stored items. */
        uint64_t hits;      /*< How many times a key was found in the cache. */
        uint64_t misses;    /*< How many times a key was not found in the cache. */
        uint64_t updates;   /*< How many times an existing key in the cache was updated. */
        uint64_t deletes;   /*< How many times an existing key in the cache was deleted. */
    };

    typedef std::unordered_map<CacheKey, Entry> Entries;

    std::string  m_name;
    const Config m_config;
    Entries      m_entries;
    Stats        m_stats;
};
