/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

/**
 * @file schemarouter.hh - Common schemarouter definitions
 */

#define MXS_MODULE_NAME "schemarouter"

#include <maxscale/ccdefs.hh>

#include <limits>
#include <list>
#include <set>
#include <string>
#include <memory>

#include <maxscale/buffer.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/service.hh>
#include <maxscale/backend.hh>
#include <maxscale/protocol/mariadb/rwbackend.hh>
#include <maxscale/config2.hh>

namespace schemarouter
{
/**
 * Configuration values
 */
struct Config : public mxs::config::Configuration
{
    Config(const char* name);

    struct Values
    {
        std::chrono::seconds     refresh_interval;
        bool                     refresh_databases;
        bool                     debug;
        std::vector<std::string> ignore_tables;
        mxs::config::RegexValue  ignore_tables_regex;
    };

    const mxs::WorkerGlobal<Values>& values() const
    {
        return m_values;
    }

private:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override
    {
        m_values.assign(m_v);
        return true;
    }

    Values                    m_v;
    mxs::WorkerGlobal<Values> m_values;
};

/**
 * Router statistics
 */
struct Stats
{
    int n_queries;          /*< Number of queries forwarded    */
    int n_sescmd;           /*< Number of session commands */
    int longest_sescmd;     /*< Longest chain of stored session commands */
    int n_hist_exceeded;    /*< Number of sessions that exceeded session
                             * command history limit */
    int    sessions;        /*< Number of sessions */
    int    shmap_cache_hit; /*< Shard map was found from the cache */
    int    shmap_cache_miss;/*< No shard map found from the cache */
    double ses_longest;     /*< Longest session */
    double ses_shortest;    /*< Shortest session */
    double ses_average;     /*< Average session length */

    Stats()
        : n_queries(0)
        , n_sescmd(0)
        , longest_sescmd(0)
        , n_hist_exceeded(0)
        , sessions(0)
        , shmap_cache_hit(0)
        , shmap_cache_miss(0)
        , ses_longest(0.0)
        , ses_shortest(std::numeric_limits<double>::max())
        , ses_average(0.0)
    {
    }
};

/**
 * Reference to a backend
 *
 * Owned by router client session.
 */
class SRBackend : public mxs::RWBackend
{
public:

    SRBackend(mxs::Endpoint* ref)
        : mxs::RWBackend(ref)
        , m_mapped(false)
    {
    }

    /**
     * @brief Set the mapping state of the backend
     *
     * @param value Value to set
     */
    void set_mapped(bool value);

    /**
     * @brief Check if the backend has been mapped
     *
     * @return True if the backend has been mapped
     */
    bool is_mapped() const;

private:
    bool m_mapped;      /**< Whether the backend has been mapped */
};

using SRBackendList = std::vector<std::unique_ptr<SRBackend>>;
}
