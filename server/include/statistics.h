#ifndef _STATISTICS_HG
#define _STATISTICS_HG
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

typedef void* ts_stats_t;

/** stats_init should be called only once */
void ts_stats_init();

/** No-op for now */
void ts_stats_end();

ts_stats_t ts_stats_alloc();
void ts_stats_free(ts_stats_t stats);
int ts_stats_sum(ts_stats_t stats);

/**
 * @brief Increment thread statistics by one
 *
 * @param stats     Statistics to add to
 * @param thread_id ID of thread
 */
static void inline
ts_stats_increment(ts_stats_t stats, int thread_id)
{
    ((int*)stats)[thread_id]++;
}

/**
 * @brief Assign a value to a statistics element
 *
 * This sets the value for the specified thread.
 *
 * @param stats     Statistics to set
 * @param value     Value to set to
 * @param thread_id ID of thread
 *
 * @note Appears to be unused
 */
static void inline
ts_stats_set(ts_stats_t stats, int value, int thread_id)
{
    ((int*)stats)[thread_id] = value;
}

#endif
