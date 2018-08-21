/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

class InMemoryStorage
{
public:
    virtual ~InMemoryStorage();

    static bool Initialize(uint32_t* pCapabilities);

    static InMemoryStorage* Create_instance(const char* zName,
                                            const CACHE_STORAGE_CONFIG& config,
                                            int argc, char* argv[]);

    void get_config(CACHE_STORAGE_CONFIG* pConfig);
    virtual cache_result_t get_info(uint32_t what, json_t** ppInfo) const = 0;
    virtual cache_result_t get_value(const CACHE_KEY& key,
                                     uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                                     GWBUF** ppResult) = 0;
    virtual cache_result_t put_value(const CACHE_KEY& key, const GWBUF& value) = 0;
    virtual cache_result_t del_value(const CACHE_KEY& key) = 0;

    cache_result_t get_head(CACHE_KEY* pKey, GWBUF** ppHead) const;
    cache_result_t get_tail(CACHE_KEY* pKey, GWBUF** ppHead) const;
    cache_result_t get_size(uint64_t* pSize) const;
    cache_result_t get_items(uint64_t* pItems) const;

protected:
    InMemoryStorage(const std::string& name,
                    const CACHE_STORAGE_CONFIG& config);

    cache_result_t do_get_info(uint32_t what, json_t** ppInfo) const;
    cache_result_t do_get_value(const CACHE_KEY& key,
                                uint32_t flags, uint32_t soft_ttl, uint32_t hard_ttl,
                                GWBUF** ppResult);
    cache_result_t do_put_value(const CACHE_KEY& key, const GWBUF& value);
    cache_result_t do_del_value(const CACHE_KEY& key);

private:
    InMemoryStorage(const InMemoryStorage&);
    InMemoryStorage& operator = (const InMemoryStorage&);

private:
    typedef std::vector<uint8_t> Value;

    struct Entry
    {
        Entry()
            : time(0)
        {}

        uint32_t time;
        Value    value;
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
        {}

        void fill(json_t* pObject) const;

        uint64_t size;       /*< The total size of the stored values. */
        uint64_t items;      /*< The number of stored items. */
        uint64_t hits;       /*< How many times a key was found in the cache. */
        uint64_t misses;     /*< How many times a key was not found in the cache. */
        uint64_t updates;    /*< How many times an existing key in the cache was updated. */
        uint64_t deletes;    /*< How many times an existing key in the cache was deleted. */
    };

    typedef std::unordered_map<CACHE_KEY, Entry> Entries;

    std::string                m_name;
    const CACHE_STORAGE_CONFIG m_config;
    Entries                    m_entries;
    Stats                      m_stats;
};
