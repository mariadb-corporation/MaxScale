/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include "cache_storage_api.hh"

namespace config = maxscale::config;

enum cache_selects_t
{
    CACHE_SELECTS_ASSUME_CACHEABLE,
    CACHE_SELECTS_VERIFY_CACHEABLE,
};

const cache_selects_t CACHE_DEFAULT_SELECTS = CACHE_SELECTS_ASSUME_CACHEABLE;

enum cache_in_trxs_t
{
    // Do NOT change the order. Code relies upon NEVER < READ_ONLY < ALL.
    CACHE_IN_TRXS_NEVER,
    CACHE_IN_TRXS_READ_ONLY,
    CACHE_IN_TRXS_ALL,
};

enum cache_users_t
{
    CACHE_USERS_ISOLATED,
    CACHE_USERS_MIXED
};

const cache_thread_model_t CACHE_DEFAULT_THREAD_MODEL = CACHE_THREAD_MODEL_ST;

const std::chrono::milliseconds CACHE_DEFAULT_TIMEOUT {5000};

class CacheFilter;

class CacheConfig : public config::Configuration
{
public:
    CacheConfig(const CacheConfig&) = delete;
    CacheConfig& operator=(const CacheConfig&) = delete;

    CacheConfig(const std::string& name, CacheFilter* filter);
    ~CacheConfig();

    CacheConfig(CacheConfig&& rhs) = default;

    using milliseconds = std::chrono::milliseconds;

    // Startup configured
    std::string           storage;
    std::string           storage_options;
    milliseconds          hard_ttl;
    milliseconds          soft_ttl;
    int64_t               max_resultset_rows;
    int64_t               max_resultset_size;
    int64_t               max_count;
    int64_t               max_size;
    std::string           rules;
    cache_thread_model_t  thread_model;
    cache_in_trxs_t       cache_in_trxs;
    bool                  enabled;
    cache_invalidate_t    invalidate;
    bool                  clear_cache_on_parse_errors;
    cache_users_t         users;
    milliseconds          timeout;
    mxs::ConfigParameters storage_params;

    // Runtime modifiable
    int64_t               debug;   // Atomicity does not matter.
    cache_selects_t       selects; // Atomicity does not matter.

    static const config::Specification* specification();

private:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override;

    bool is_config_valid(const std::map<std::string, mxs::ConfigParameters>& nested_params);
    void make_config_adjustements();

private:
    CacheFilter* m_pFilter;
};
