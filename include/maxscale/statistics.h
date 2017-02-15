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
 * @file statistics.h  - Lock-free statistics gathering
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

/**
 * @brief Allocate a new statistics object
 *
 * @return New statistics object or NULL if memory allocation failed
 */
ts_stats_t ts_stats_alloc();

/**
 * @brief Free statistics
 *
 * @param stats Statistics to free
 */
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
void ts_stats_increment(ts_stats_t stats, int thread_id);

/**
 * @brief Assign a value to a statistics element
 *
 * This sets the value for the specified thread.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
void ts_stats_set(ts_stats_t stats, int value, int thread_id);

/**
 * @brief Assign the maximum value to a statistics element
 *
 * This sets the value for the specified thread if the current value is smaller.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
void ts_stats_set_max(ts_stats_t stats, int value, int thread_id);

/**
 * @brief Assign the minimum value to a statistics element
 *
 * This sets the value for the specified thread if the current value is larger.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 */
void ts_stats_set_min(ts_stats_t stats, int value, int thread_id);

MXS_END_DECLS
