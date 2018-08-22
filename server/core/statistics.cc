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
 * @file statistics.c  - Functions to aid in the compilation of statistics
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 15/06/16     Martin Brampton Removed some functions to statistics.h for inline
 *
 * @endverbatim
 */

#include "internal/statistics.h"
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/log.h>
#include <maxscale/utils.h>

static int thread_count = 0;
static size_t cache_linesize = 0;
static size_t stats_size = 0;
static bool stats_initialized = false;

static size_t get_cache_line_size()
{
    size_t rval = 64; // Cache lines are 64 bytes for x86

#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    rval = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
#endif

    if (rval < sizeof(int64_t))
    {
        MXS_WARNING("Cache line size reported to be %lu bytes when a 64-bit "
                    "integer is %lu bytes. Increasing statistics to the minimum "
                    "size of %lu bytes.", rval, sizeof(int64_t), sizeof(int64_t));
        rval = sizeof(int64_t);
    }

    return rval;
}

/**
 * @brief Initialize the statistics gathering
 */
void ts_stats_init()
{
    mxb_assert(!stats_initialized);
    thread_count = config_threadcount();
    cache_linesize = get_cache_line_size();
    stats_size = thread_count * cache_linesize;
    stats_initialized = true;
}

/**
 * @brief End the statistics gathering
 */
void ts_stats_end()
{
    mxb_assert(stats_initialized);
}

/**
 * @brief Create a new statistics object
 *
 * @return New stats_t object or NULL if memory allocation failed
 */
ts_stats_t ts_stats_alloc()
{
    mxb_assert(stats_initialized);
    return MXS_CALLOC(thread_count, cache_linesize);
}

/**
 * Free a statistics object
 *
 * @param stats Stats to free
 */
void ts_stats_free(ts_stats_t stats)
{
    mxb_assert(stats_initialized);
    MXS_FREE(stats);
}

/**
 * @brief Read the total value of the statistics object
 *
 * Add up the individual thread statistics to get the total for all threads.
 *
 * @param stats Statistics to read
 * @return Value of statistics
 */
int64_t ts_stats_sum(ts_stats_t stats)
{
    mxb_assert(stats_initialized);
    int64_t sum = 0;

    for (size_t i = 0; i < stats_size; i += cache_linesize)
    {
        sum += *((int64_t*)MXS_PTR(stats, i));
    }

    return sum;
}

/**
 * @brief Read the value of the statistics object
 *
 * Calculate
 *
 * @param stats Statistics to read
 * @param type  The statistics type
 * @return Value of statistics
 */
int64_t ts_stats_get(ts_stats_t stats, enum ts_stats_type type)
{
    mxb_assert(stats_initialized);
    int64_t best = type == TS_STATS_MAX ? LONG_MIN : (type == TS_STATS_MIX ? LONG_MAX : 0);

    for (size_t i = 0; i < stats_size; i += cache_linesize)
    {
        int64_t value = *((int64_t*)MXS_PTR(stats, i));

        switch (type)
        {
        case TS_STATS_MAX:
            if (value > best)
            {
                best = value;
            }
            break;

        case TS_STATS_MIX:
            if (value < best)
            {
                best = value;
            }
            break;

        case TS_STATS_AVG:
        case TS_STATS_SUM:
            best += value;
            break;
        }
    }

    return type == TS_STATS_AVG ? best / thread_count : best;
}

void ts_stats_increment(ts_stats_t stats, int thread_id)
{
    mxb_assert(thread_id < thread_count);
    int64_t *item = (int64_t*)MXS_PTR(stats, thread_id * cache_linesize);
    *item += 1;
}

void ts_stats_set(ts_stats_t stats, int value, int thread_id)
{
    mxb_assert(thread_id < thread_count);
    int64_t *item = (int64_t*)MXS_PTR(stats, thread_id * cache_linesize);
    *item = value;
}

void ts_stats_set_max(ts_stats_t stats, int value, int thread_id)
{
    mxb_assert(thread_id < thread_count);
    int64_t *item = (int64_t*)MXS_PTR(stats, thread_id * cache_linesize);

    if (value > *item)
    {
        *item = value;
    }
}

void ts_stats_set_min(ts_stats_t stats, int value, int thread_id)
{
    mxb_assert(thread_id < thread_count);
    int64_t *item = (int64_t*)MXS_PTR(stats, thread_id * cache_linesize);

    if (value < *item)
    {
        *item = value;
    }
}
