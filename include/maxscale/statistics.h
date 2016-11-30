#pragma once
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
 * @file statistics.h  - Lock-free statistics gathering
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                 Description
 * 21/01/16     Markus Makela       Initial implementation
 * 15/06/16     Martin Brampton     Frequently used functions inlined
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <stdint.h>

MXS_BEGIN_DECLS

typedef void* ts_stats_t;

/** Enum values for ts_stats_get */
enum ts_stats_type
{
    TS_STATS_MAX, /**< Maximum value */
    TS_STATS_MIX, /**< Minimum value */
    TS_STATS_SUM, /**< Sum of all value */
    TS_STATS_AVG  /**< Average of all values */
};

/** stats_init should be called only once */
void ts_stats_init();

/** No-op for now */
void ts_stats_end();

ts_stats_t ts_stats_alloc();
void ts_stats_free(ts_stats_t stats);

/**
 * @brief Get statistics
 *
 * @param stats Statistics to read
 * @param type Type of statistics to get
 * @return Statistics value
 *
 * @see enum ts_stats_type
 */
int64_t ts_stats_get(ts_stats_t stats, enum ts_stats_type type);

/**
 * @brief Increment thread statistics by one
 *
 * @param stats     Statistics to add to
 * @param thread_id ID of thread
 */
static void inline
ts_stats_increment(ts_stats_t stats, int thread_id)
{
    ((int64_t*)stats)[thread_id]++;
}

/**
 * @brief Assign a value to a statistics element
 *
 * This sets the value for the specified thread.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
static void inline
ts_stats_set(ts_stats_t stats, int value, int thread_id)
{
    ((int64_t*)stats)[thread_id] = value;
}

/**
 * @brief Assign the maximum value to a statistics element
 *
 * This sets the value for the specified thread if the current value is smaller.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
static void inline
ts_stats_set_max(ts_stats_t stats, int value, int thread_id)
{
    int64_t *p = (int64_t*) stats;

    if (value > p[thread_id])
    {
        p[thread_id] = value;
    }
}

/**
 * @brief Assign the minimum value to a statistics element
 *
 * This sets the value for the specified thread if the current value is larger.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
static void inline
ts_stats_set_min(ts_stats_t stats, int value, int thread_id)
{
    int64_t *p = (int64_t*) stats;

    if (value < p[thread_id])
    {
        p[thread_id] = value;
    }
}

MXS_END_DECLS
