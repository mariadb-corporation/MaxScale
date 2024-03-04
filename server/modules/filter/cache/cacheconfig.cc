/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "cache"
#include "cacheconfig.hh"
#include "cache.hh"
#include "cachefilter.hh"
#include "storagefactory.hh"

namespace config = mxs::config;

namespace
{

namespace cache
{

class ParamStorage final : public config::ParamString
{
public:
    using ParamString::ParamString;

    bool takes_parameters() const override
    {
        return true;
    }

    bool validate_parameters(const std::string& value,
                             const mxs::ConfigParameters& params,
                             mxs::ConfigParameters* pUnrecognized) const override
    {
        return do_validate_parameters(value, params, pUnrecognized);
    }

    bool validate_parameters(const std::string& value,
                             json_t* pParams,
                             std::set<std::string>* pUnrecognized) const override
    {
        return do_validate_parameters(value, pParams, pUnrecognized);
    }

private:
    template<class Params, class Unrecognized>
    bool do_validate_parameters(const std::string& value,
                                Params& params,
                                Unrecognized* pUnrecognized) const
    {
        bool rv = false;

        std::unique_ptr<StorageFactory> sFactory(StorageFactory::open(value));

        if (sFactory)
        {
            rv = sFactory->specification().validate(nullptr, params, pUnrecognized);
        }

        return rv;
    }
};

class Specification final : public config::Specification
{
public:
    using config::Specification::Specification;

private:
    template<class Params>
    bool do_post_validate(const CacheConfig* pConfig, Params& params) const;

    bool post_validate(const config::Configuration* pConfig,
                       const mxs::ConfigParameters& params,
                       const std::map<std::string, mxs::ConfigParameters>& nested_params) const override
    {
        return do_post_validate(static_cast<const CacheConfig*>(pConfig), params);
    }

    bool post_validate(const config::Configuration* pConfig,
                       json_t* json,
                       const std::map<std::string, json_t*>& nested_params) const override
    {
        return do_post_validate(static_cast<const CacheConfig*>(pConfig), json);
    }
};

Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);

ParamStorage storage(
    &specification,
    "storage",
    "The name of the module that provides the storage implementation for the cache.",
    "storage_inmemory"
    );

config::ParamString storage_options(
    &specification,
    "storage_options",
    "A comma separated list of arguments to be provided to the storage module "
    "specified with 'storage'.",
    ""
    );

config::ParamDuration<std::chrono::milliseconds> hard_ttl(
    &specification,
    "hard_ttl",
    "Hard time to live; the maximum amount of time the cached result is "
    "used before it is discarded and the result is fetched from the backend. "
    "See also 'soft_ttl'.",
    std::chrono::milliseconds {0}
    );

config::ParamDuration<std::chrono::milliseconds> soft_ttl(
    &specification,
    "soft_ttl",
    "Soft time to live; the maximum amount of time the cached result is "
    "used before the first client querying for the result is used for refreshing "
    "the cached data from the backend. See also 'hard_ttl'.",
    std::chrono::milliseconds {0}
    );

config::ParamCount max_resultset_rows(
    &specification,
    "max_resultset_rows",
    "Specifies the maximum number of rows a resultset can have in order to be "
    "stored in the cache. A resultset larger than this, will not be stored.",
    0
    );

config::ParamSize max_resultset_size(
    &specification,
    "max_resultset_size",
    "Specifies the maximum size of a resultset, for it to be stored in the cache. "
    "A resultset larger than this, will not be stored.",
    0
    );

config::ParamCount max_count(
    &specification,
    "max_count",
    "The maximum number of items the cache may contain. If the limit has been "
    "reached and a new item should be stored, then an older item will be evicted.",
    0
    );

config::ParamSize max_size(
    &specification,
    "max_size",
    "The maximum size the cache may occupy. If the limit has been reached and a new "
    "item should be stored, then some older item(s) will be evicted to make space.",
    0
    );

config::ParamPath rules(
    &specification,
    "rules",
    "Specifies the path of the file where the caching rules are stored. A relative "
    "path is interpreted relative to the data directory of MariaDB MaxScale.",
    config::ParamPath::R,
    "",
    config::Param::Modifiable::AT_RUNTIME
    );

config::ParamBitMask debug(
    &specification,
    "debug",
    "An integer value, using which the level of debug logging made by the cache "
    "can be controlled.",
    0,
    CACHE_DEBUG_MIN,
    CACHE_DEBUG_MAX,
    config::Param::Modifiable::AT_RUNTIME
    );

config::ParamEnum<cache_thread_model_t> thread_model(
    &specification,
    "cached_data",
    "An enumeration option specifying how data is shared between threads.",
{
    {CACHE_THREAD_MODEL_MT, "shared"},
    {CACHE_THREAD_MODEL_ST, "thread_specific"}
},
    CACHE_THREAD_MODEL_ST
    );

config::ParamEnum<cache_selects_t> selects(
    &specification,
    "selects",
    "An enumeration option specifying what approach the cache should take with "
    "respect to SELECT statements.",
{
    {CACHE_SELECTS_ASSUME_CACHEABLE, "assume_cacheable"},
    {CACHE_SELECTS_VERIFY_CACHEABLE, "verify_cacheable"}
},
    CACHE_SELECTS_ASSUME_CACHEABLE,
    config::Param::Modifiable::AT_RUNTIME
    );

config::ParamEnum<cache_in_trxs_t> cache_in_trxs(
    &specification,
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

config::ParamEnum<cache_invalidate_t> invalidate(
    &specification,
    "invalidate",
    "An enumeration options specifying how the cache should perform cache invalidation.",
{
    {CACHE_INVALIDATE_NEVER, "never"},
    {CACHE_INVALIDATE_CURRENT, "current"},
},
    CACHE_INVALIDATE_NEVER
    );

config::ParamBool enabled(
    &specification,
    "enabled",
    "Specifies whether the cache is initially enabled or disabled.",
    true
    );

config::ParamBool clear_cache_on_parse_errors(
    &specification,
    "clear_cache_on_parse_errors",
    "Specifies whether the cache should be cleared if an UPDATE/INSERT/DELETE statement "
    "cannot be parsed. This setting has only effect if invalidation has been enabled.",
    true
    );

config::ParamEnum<cache_users_t> users(
    &specification,
    "users",
    "Specifies whether cached data is shared between users.",
{
    {CACHE_USERS_ISOLATED, "isolated"},
    {CACHE_USERS_MIXED, "mixed"}
},
    CACHE_USERS_MIXED
    );

config::ParamDuration<std::chrono::milliseconds> timeout(
    &specification,
    "timeout",
    "The timeout when performing operations to distributed storages.",
    CACHE_DEFAULT_TIMEOUT
    );

template<class Params>
bool Specification::do_post_validate(const CacheConfig* pConfig, Params& params) const
{
    bool ok = true;

    std::string rules_path = rules.get(params);

    if (!rules_path.empty())
    {
        CacheRules::SVector sRules;

        if (pConfig)
        {
            sRules = CacheRules::load(pConfig, rules_path);
        }
        else
        {
            CacheConfig config("dummy", nullptr);
            sRules = CacheRules::load(&config, rules_path);
        }

        ok = (sRules.get() != nullptr);
    }

    return ok;
}
}
}


CacheConfig::CacheConfig(const std::string& name, CacheFilter* filter)
    : config::Configuration(name, &cache::specification)
    , m_pFilter(filter)
{
    add_native(&CacheConfig::storage, &cache::storage);
    add_native(&CacheConfig::storage_options, &cache::storage_options);
    add_native(&CacheConfig::hard_ttl, &cache::hard_ttl);
    add_native(&CacheConfig::soft_ttl, &cache::soft_ttl);
    add_native(&CacheConfig::max_resultset_rows, &cache::max_resultset_rows);
    add_native(&CacheConfig::max_resultset_size, &cache::max_resultset_size);
    add_native(&CacheConfig::max_count, &cache::max_count);
    add_native(&CacheConfig::max_size, &cache::max_size);
    add_native(&CacheConfig::rules, &cache::rules);
    add_native(&CacheConfig::debug, &cache::debug);
    add_native(&CacheConfig::thread_model, &cache::thread_model);
    add_native(&CacheConfig::selects, &cache::selects);
    add_native(&CacheConfig::cache_in_trxs, &cache::cache_in_trxs);
    add_native(&CacheConfig::enabled, &cache::enabled);
    add_native(&CacheConfig::invalidate, &cache::invalidate);
    add_native(&CacheConfig::clear_cache_on_parse_errors, &cache::clear_cache_on_parse_errors);
    add_native(&CacheConfig::users, &cache::users);
    add_native(&CacheConfig::timeout, &cache::timeout);
}

CacheConfig::~CacheConfig()
{
}

//static
const config::Specification* CacheConfig::specification()
{
    return &cache::specification;
}

bool CacheConfig::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool configured = is_config_valid(nested_params);

    if (configured)
    {
        make_config_adjustements();

        // The check for m_pFilter here is for the unit tests that don't allocate a
        // CacheFilter instance. This should be changed in some way so that the
        // post-configuration step is handled in a more abstract manner.
        if (m_pFilter)
        {
            configured = m_pFilter->post_configure();
        }
    }

    return configured;
}

bool CacheConfig::is_config_valid(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool valid = true;

    auto it = nested_params.find(this->storage);

    if (it != nested_params.end())
    {
        if (this->storage_options.empty())
        {
            if (nested_params.size() == 1)
            {
                this->storage_params = it->second;
            }
            else
            {
                MXB_ERROR("In section %s, nested parameters can only be provided for %s.",
                          name().c_str(), this->storage.c_str());
                valid = false;
            }
        }
        else
        {
            MXB_ERROR("In section %s, the storage parameters of %s must either be provided using "
                      "'storage_options' (deprecated) or using nested parameters (e.g. '%s.server=...').",
                      name().c_str(), this->storage.c_str(), this->storage.c_str());
            valid = false;
        }
    }
    else
    {
        if (!this->storage_options.empty())
        {
            MXB_WARNING("In section %s, providing storage parameters using 'storage_options' has "
                        "been deprecated. Use nested parameters (e.g. '%s.server=...') instead.",
                        name().c_str(), this->storage.c_str());
        }

        valid = Storage::parse_argument_string(this->storage_options, &this->storage_params);
    }

    return valid;
}

void CacheConfig::make_config_adjustements()
{
    if (this->soft_ttl > this->hard_ttl)
    {
        MXB_WARNING("The value of 'soft_ttl' must be less than or equal to that of 'hard_ttl'. "
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
            MXB_WARNING("The value of 'max_resultset_size' %ld should not be larger than "
                        "the value of 'max_size' %ld. Adjusting the value of 'max_resultset_size' "
                        "down to %ld.",
                        this->max_resultset_size,
                        this->max_size,
                        this->max_size);
            this->max_resultset_size = this->max_size;
        }
    }
}
