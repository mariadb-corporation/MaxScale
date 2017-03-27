#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
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
#include <set>
#include <string>

#include <maxscale/pcre2.h>

using std::set;
using std::string;

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
    set<string>       ignored_dbs;          /**< Set of ignored databases */

    Config():
        refresh_min_interval(0.0),
        refresh_databases(false),
        debug(false),
        ignore_regex(NULL),
        ignore_match_data(NULL)
    {
    }
};

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
}
