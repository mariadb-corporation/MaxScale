/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include "cache_storage_api.hh"

typedef enum cache_selects
{
    CACHE_SELECTS_ASSUME_CACHEABLE,
    CACHE_SELECTS_VERIFY_CACHEABLE,
} cache_selects_t;

const cache_selects_t CACHE_DEFAULT_SELECTS = CACHE_SELECTS_ASSUME_CACHEABLE;

typedef enum cache_in_trxs
{
    // Do NOT change the order. Code relies upon NEVER < READ_ONLY < ALL.
    CACHE_IN_TRXS_NEVER,
    CACHE_IN_TRXS_READ_ONLY,
    CACHE_IN_TRXS_ALL,
} cache_in_trxs_t;

const cache_thread_model CACHE_DEFAULT_THREAD_MODEL = CACHE_THREAD_MODEL_ST;


class CacheConfig : public config::Configuration
{
public:
    CacheConfig(const std::string& name);
    ~CacheConfig();

    CacheConfig(const CacheConfig&) = delete;
    CacheConfig& operator=(const CacheConfig&) = delete;

    using milliseconds = std::chrono::milliseconds;

    config::String                     storage;
    config::String                     storage_options;
    config::Duration<milliseconds>     hard_ttl;
    config::Duration<milliseconds>     soft_ttl;
    config::Count                      max_resultset_rows;
    config::Size                       max_resultset_size;
    config::Count                      max_count;
    config::Size                       max_size;
    config::Path                       rules;
    config::BitMask                    debug;
    config::Enum<cache_thread_model_t> thread_model;
    config::Enum<cache_selects_t>      selects;
    config::Enum<cache_in_trxs_t>      cache_in_trxs;
    config::Bool                       enabled;
    char*                              zStorage_options {nullptr};  /**< Raw options for storage module. */
    char**                             storage_argv {nullptr};      /**< Cooked options for storage module. */
    int                                storage_argc {0};            /**< Number of cooked options. */

    static const config::Specification& specification()
    {
        return s_specification;
    }

private:
    bool post_configure(const MXS_CONFIG_PARAMETER& params) override;

private:
    static config::Specification s_specification;

    static config::ParamString                     s_storage;
    static config::ParamString                     s_storage_options;
    static config::ParamDuration<milliseconds>     s_hard_ttl;
    static config::ParamDuration<milliseconds>     s_soft_ttl;
    static config::ParamCount                      s_max_resultset_rows;
    static config::ParamSize                       s_max_resultset_size;
    static config::ParamCount                      s_max_count;
    static config::ParamSize                       s_max_size;
    static config::ParamPath                       s_rules;
    static config::ParamBitMask                    s_debug;
    static config::ParamEnum<cache_thread_model_t> s_thread_model;
    static config::ParamEnum<cache_selects_t>      s_selects;
    static config::ParamEnum<cache_in_trxs_t>      s_cache_in_trxs;
    static config::ParamBool                       s_enabled;
};
