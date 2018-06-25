#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/cppdefs.hh>
#include <functional>
#include <string>
#include <tr1/functional>
#include "cache_storage_api.h"


namespace std
{

template<>
struct equal_to<CACHE_KEY>
{
    bool operator()(const CACHE_KEY& lhs, const CACHE_KEY& rhs) const
    {
        return cache_key_equal_to(&lhs, &rhs);
    }
};

namespace tr1
{

template<>
struct hash<CACHE_KEY>
{
    size_t operator()(const CACHE_KEY& key) const
    {
        return cache_key_hash(&key);
    }
};

}

}

std::string cache_key_to_string(const CACHE_KEY& key);

inline bool operator == (const CACHE_KEY& lhs, const CACHE_KEY& rhs)
{
    return lhs.data == rhs.data;
}

inline bool operator != (const CACHE_KEY& lhs, const CACHE_KEY& rhs)
{
    return !(lhs == rhs);
}

class CacheKey : public CACHE_KEY
{
public:
    CacheKey()
    {
        data = 0;
    }
};

class CacheStorageConfig : public CACHE_STORAGE_CONFIG
{
public:
    CacheStorageConfig(cache_thread_model_t thread_model,
                       uint32_t hard_ttl = 0,
                       uint32_t soft_ttl = 0,
                       uint32_t max_count = 0,
                       uint64_t max_size = 0)
    {
        this->thread_model = thread_model;
        this->hard_ttl = hard_ttl;
        this->soft_ttl = soft_ttl;
        this->max_count = max_count;
        this->max_size = max_size;
    }

    CacheStorageConfig()
    {
        thread_model = CACHE_THREAD_MODEL_MT;
        hard_ttl = 0;
        soft_ttl = 0;
        max_count = 0;
        max_size = 0;
    }

    CacheStorageConfig(const CACHE_STORAGE_CONFIG& config)
    {
        thread_model = config.thread_model;
        hard_ttl = config.hard_ttl;
        soft_ttl = config.soft_ttl;
        max_count = config.max_count;
        max_size = config.max_size;
    }
};
