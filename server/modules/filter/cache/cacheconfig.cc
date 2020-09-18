/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "cache"
#include "cacheconfig.hh"
#include "cache.hh"

config::Specification CacheConfig::s_specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamString CacheConfig::s_storage(
    &s_specification,
    "storage",
    "The name of the module that provides the storage implementation for the cache.",
    "storage_inmemory"
    );

config::ParamString CacheConfig::s_storage_options(
    &s_specification,
    "storage_options",
    "A comma separated list of arguments to be provided to the storage module "
    "specified with 'storage'.",
    ""
    );

config::ParamDuration<std::chrono::milliseconds> CacheConfig::s_hard_ttl(
    &s_specification,
    "hard_ttl",
    "Hard time to live; the maximum amount of time the cached result is "
    "used before it is discarded and the result is fetched from the backend. "
    "See also 'soft_ttl'.",
    mxs::config::INTERPRET_AS_SECONDS,
    std::chrono::milliseconds {0}
    );

config::ParamDuration<std::chrono::milliseconds> CacheConfig::s_soft_ttl(
    &s_specification,
    "soft_ttl",
    "Soft time to live; the maximum amount of time the cached result is "
    "used before the first client querying for the result is used for refreshing "
    "the cached data from the backend. See also 'hard_ttl'.",
    mxs::config::INTERPRET_AS_SECONDS,
    std::chrono::milliseconds {0}
    );

config::ParamCount CacheConfig::s_max_resultset_rows(
    &s_specification,
    "max_resultset_rows",
    "Specifies the maximum number of rows a resultset can have in order to be "
    "stored in the cache. A resultset larger than this, will not be stored.",
    0
    );

config::ParamSize CacheConfig::s_max_resultset_size(
    &s_specification,
    "max_resultset_size",
    "Specifies the maximum size of a resultset, for it to be stored in the cache. "
    "A resultset larger than this, will not be stored.",
    0
    );

config::ParamCount CacheConfig::s_max_count(
    &s_specification,
    "max_count",
    "The maximum number of items the cache may contain. If the limit has been "
    "reached and a new item should be stored, then an older item will be evicted.",
    0
    );

config::ParamSize CacheConfig::s_max_size(
    &s_specification,
    "max_size",
    "The maximum size the cache may occupy. If the limit has been reached and a new "
    "item should be stored, then some older item(s) will be evicted to make space.",
    0
    );

config::ParamPath CacheConfig::s_rules(
    &s_specification,
    "rules",
    "Specifies the path of the file where the caching rules are stored. A relative "
    "path is interpreted relative to the data directory of MariaDB MaxScale.",
    0,
    ""
    );

config::ParamBitMask CacheConfig::s_debug(
    &s_specification,
    "debug",
    "An integer value, using which the level of debug logging made by the cache "
    "can be controlled.",
    0
    );

config::ParamEnum<cache_thread_model_t> CacheConfig::s_thread_model(
    &s_specification,
    "cached_data",
    "An enumeration option specifying how data is shared between threads.",
{
    {CACHE_THREAD_MODEL_MT, "shared"},
    {CACHE_THREAD_MODEL_ST, "thread_specific"}
},
    CACHE_THREAD_MODEL_ST
    );

config::ParamEnum<cache_selects_t> CacheConfig::s_selects(
    &s_specification,
    "selects",
    "An enumeration option specifying what approach the cache should take with "
    "respect to SELECT statements.",
{
    {CACHE_SELECTS_ASSUME_CACHEABLE, "assume_cacheable"},
    {CACHE_SELECTS_VERIFY_CACHEABLE, "verify_cacheable"}
},
    CACHE_SELECTS_ASSUME_CACHEABLE
    );

config::ParamEnum<cache_in_trxs_t> CacheConfig::s_cache_in_trxs(
    &s_specification,
    "cache_in_transactions",
    "An enumeration option specifying how the cache should behave when there "
    "are active transactions.",
{
    {CACHE_IN_TRXS_NEVER, "never"},
    {CACHE_IN_TRXS_READ_ONLY, "read_only_transactions"},
    {CACHE_IN_TRXS_ALL, "all_transactions"}
},
    CACHE_IN_TRXS_ALL
    );

config::ParamEnum<cache_invalidate_t> CacheConfig::s_invalidate(
    &s_specification,
    "invalidate",
    "An enumeration options specifying how the cache should perform cache invalidation.",
    {
        {CACHE_INVALIDATE_NEVER, "never"},
        {CACHE_INVALIDATE_CURRENT, "current"},
    },
    CACHE_INVALIDATE_NEVER
    );

config::ParamBool CacheConfig::s_enabled(
    &s_specification,
    "enabled",
    "Specifies whether the cache is initially enabled or disabled.",
    true
    );

config::ParamBool CacheConfig::s_clear_cache_on_parse_errors(
    &s_specification,
    "clear_cache_on_parse_errors",
    "Specifies whether the cache should be cleared if an UPDATE/INSERT/DELETE statement "
    "cannot be parsed. This setting has only effect if invalidation has been enabled.",
    true
    );

config::ParamEnum<cache_users_t> CacheConfig::s_users(
    &s_specification,
    "users",
    "Specifies whether cached data is shared between users.",
    {
        {CACHE_USERS_ISOLATED, "isolated"},
        {CACHE_USERS_MIXED, "mixed"}
    },
    CACHE_USERS_MIXED
    );

CacheConfig::CacheConfig(const std::string& name)
    : config::Configuration(name, &s_specification)
{
    add_native(&CacheConfig::storage, &s_storage);
    add_native(&CacheConfig::storage_options, &s_storage_options);
    add_native(&CacheConfig::hard_ttl, &s_hard_ttl);
    add_native(&CacheConfig::soft_ttl, &s_soft_ttl);
    add_native(&CacheConfig::max_resultset_rows, &s_max_resultset_rows);
    add_native(&CacheConfig::max_resultset_size, &s_max_resultset_size);
    add_native(&CacheConfig::max_count, &s_max_count);
    add_native(&CacheConfig::max_size, &s_max_size);
    add_native(&CacheConfig::rules, &s_rules);
    add_native(&CacheConfig::debug, &s_debug);
    add_native(&CacheConfig::thread_model, &s_thread_model);
    add_native(&CacheConfig::selects, &s_selects);
    add_native(&CacheConfig::cache_in_trxs, &s_cache_in_trxs);
    add_native(&CacheConfig::enabled, &s_enabled);
    add_native(&CacheConfig::invalidate, &s_invalidate);
    add_native(&CacheConfig::clear_cache_on_parse_errors, &s_clear_cache_on_parse_errors);
    add_native(&CacheConfig::users, &s_users);
}

CacheConfig::~CacheConfig()
{
}

bool CacheConfig::post_configure()
{
    bool configured = true;

    if ((this->debug < CACHE_DEBUG_MIN) || (this->debug > CACHE_DEBUG_MAX))
    {
        MXS_ERROR("The value of the configuration entry 'debug' must "
                  "be between %d and %d, inclusive.",
                  CACHE_DEBUG_MIN,
                  CACHE_DEBUG_MAX);
        configured = false;
    }

    if (this->soft_ttl > this->hard_ttl)
    {
        MXS_WARNING("The value of 'soft_ttl' must be less than or equal to that of 'hard_ttl'. "
                    "Setting 'soft_ttl' to the same value as 'hard_ttl'.");
        this->soft_ttl = this->hard_ttl;
    }

    if (this->max_resultset_size == 0)
    {
        if (this->max_size != 0)
        {
            // If a specific size has been configured for 'max_size' but 'max_resultset_size'
            // has not been specifically set, then we silently set it to the same as 'max_size'.
            this->max_resultset_size = this->max_size;
        }
    }
    else
    {
        if ((this->max_size != 0) && (this->max_resultset_size > this->max_size))
        {
            MXS_WARNING("The value of 'max_resultset_size' %ld should not be larger than "
                        "the value of 'max_size' %ld. Adjusting the value of 'max_resultset_size' "
                        "down to %ld.",
                        this->max_resultset_size,
                        this->max_size,
                        this->max_size);
            this->max_resultset_size = this->max_size;
        }
    }

    return configured;
}
