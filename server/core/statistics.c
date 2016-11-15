/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
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

#include <maxscale/statistics.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/config.h>
#include <maxscale/debug.h>
#include <maxscale/platform.h>

static int thread_count = 0;
static bool stats_initialized = false;

/**
 * @brief Initialize the statistics gathering
 */
void ts_stats_init()
{
    ss_dassert(!stats_initialized);
    thread_count = config_threadcount();
    stats_initialized = true;
}

/**
 * @brief End the statistics gathering
 */
void ts_stats_end()
{
    ss_dassert(stats_initialized);
}

/**
 * @brief Create a new statistics object
 *
 * @return New stats_t object or NULL if memory allocation failed
 */
ts_stats_t ts_stats_alloc()
{
    ss_dassert(stats_initialized);
    return MXS_CALLOC(thread_count, sizeof(int64_t));
}

/**
 * Free a statistics object
 *
 * @param stats Stats to free
 */
void ts_stats_free(ts_stats_t stats)
{
    ss_dassert(stats_initialized);
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
    ss_dassert(stats_initialized);
    int64_t sum = 0;
    for (int i = 0; i < thread_count; i++)
    {
        sum += ((int64_t*)stats)[i];
    }
    return sum;
}
