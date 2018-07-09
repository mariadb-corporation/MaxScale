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

/**
 * @file schemarouter.hh - Common schemarouter definitions
 */

#define MXS_MODULE_NAME "schemarouter"

#include <maxscale/cdefs.h>

#include <limits>
#include <list>
#include <set>
#include <string>
#include <memory>

#include <maxscale/buffer.hh>
#include <maxscale/pcre2.h>
#include <maxscale/service.h>
#include <maxscale/backend.hh>

namespace schemarouter
{
/**
 * Configuration values
 */
struct Config
{
    double            refresh_min_interval; /**< Minimum required interval between
                                             * refreshes of databases */
    bool              refresh_databases;    /**< Are databases refreshed when
                                             * they are not found in the hashtable */
    bool              debug;                /**< Enable verbose debug messages to clients */
    pcre2_code*       ignore_regex;         /**< Regular expression used to ignore databases */
    pcre2_match_data* ignore_match_data;    /**< Match data for @c ignore_regex */
    std::set<std::string> ignored_dbs;      /**< Set of ignored databases */
    SERVER*           preferred_server;     /**< Server to prefer in conflict situations */

    Config(MXS_CONFIG_PARAMETER* conf);

    ~Config()
    {
        pcre2_match_data_free(ignore_match_data);
        pcre2_code_free(ignore_regex);
    }
};

typedef std::shared_ptr<Config> SConfig;

/**
 * Router statistics
 */
struct Stats
{
    int    n_queries;        /*< Number of queries forwarded    */
    int    n_sescmd;         /*< Number of session commands */
    int    longest_sescmd;   /*< Longest chain of stored session commands */
    int    n_hist_exceeded;  /*< Number of sessions that exceeded session
                              * command history limit */
    int    sessions;         /*< Number of sessions */
    int    shmap_cache_hit;  /*< Shard map was found from the cache */
    int    shmap_cache_miss; /*< No shard map found from the cache */
    double ses_longest;      /*< Longest session */
    double ses_shortest;     /*< Shortest session */
    double ses_average;      /*< Average session length */

    Stats():
        n_queries(0),
        n_sescmd(0),
        longest_sescmd(0),
        n_hist_exceeded(0),
        sessions(0),
        shmap_cache_hit(0),
        shmap_cache_miss(0),
        ses_longest(0.0),
        ses_shortest(std::numeric_limits<double>::max()),
        ses_average(0.0)
    {
    }
};

/**
 * Reference to a backend
 *
 * Owned by router client session.
 */
class SRBackend: public mxs::Backend
{
public:

    SRBackend(SERVER_REF *ref):
        mxs::Backend(ref),
        m_mapped(false)
    {
    }

    ~SRBackend()
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
    bool               m_mapped; /**< Whether the backend has been mapped */
};

typedef std::shared_ptr<SRBackend> SSRBackend;
typedef std::list<SSRBackend> SSRBackendList;

}
