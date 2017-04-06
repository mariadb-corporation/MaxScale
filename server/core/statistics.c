/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <statistics.h>
#include <maxconfig.h>
#include <string.h>
#include <platform.h>

thread_local int current_thread_id = 0;

static int thread_count = 0;
static bool initialized = false;

/**
 * Initialize the statistics gathering
 */
void ts_stats_init()
{
    ss_dassert(!initialized);
    thread_count = config_threadcount();
    initialized = true;
}

/**
 * End the statistics gathering
 */
void ts_stats_end()
{
    ss_dassert(initialized);
}

/**
 * Create a new statistics object
 *
 * @return New stats_t object or NULL if memory allocation failed
 */
ts_stats_t ts_stats_alloc()
{
    ss_dassert(initialized);
    return calloc(thread_count, sizeof(int64_t));
}

/**
 * Free a statistics object
 *
 * @param stats Stats to free
 */
void ts_stats_free(ts_stats_t stats)
{
    ss_dassert(initialized);
    free(stats);
}

/**
 * Set the current thread id
 *
 * This should only be called only once by each thread.
 * @param id Thread id
 */
void ts_stats_set_thread_id(int id)
{
    ss_dassert(initialized);
    current_thread_id = id;
}

/**
 * Add @c value to @c stats
 *
 * @param stats Statistics to add to
 * @param value Value to add
 */
void ts_stats_add(ts_stats_t stats, int64_t value)
{
    ss_dassert(initialized);
    ((int64_t*)stats)[current_thread_id] += value;
}

/**
 * Assign a value to the statistics
 *
 * This sets the value for the current thread only.
 * @param stats Statistics to set
 * @param value Value to set to
 */
void ts_stats_set(ts_stats_t stats, int64_t value)
{
    ss_dassert(initialized);
    ((int64_t*)stats)[current_thread_id] = value;
}

/**
 * Read the total value of the statistics object
 *
 * @param stats Statistics to read
 * @return Value of statistics
 */
int64_t ts_stats_sum(ts_stats_t stats)
{
    ss_dassert(initialized);
    int64_t sum = 0;
    for (int i = 0; i < thread_count; i++)
    {
        sum += ((int64_t*)stats)[i];
    }
    return sum;
}
