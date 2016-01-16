/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2016
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
    return calloc(thread_count, sizeof(int));
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
void ts_stats_add(ts_stats_t stats, int value)
{
    ss_dassert(initialized);
    ((int*)stats)[current_thread_id] += value;
}

/**
 * Assign a value to the statistics
 *
 * This sets the value for the current thread only.
 * @param stats Statistics to set
 * @param value Value to set to
 */
void ts_stats_set(ts_stats_t stats, int value)
{
    ss_dassert(initialized);
    ((int*)stats)[current_thread_id] = value;
}

/**
 * Read the total value of the statistics object
 *
 * @param stats Statistics to read
 * @return Value of statistics
 */
int ts_stats_sum(ts_stats_t stats)
{
    ss_dassert(initialized);
    int sum = 0;
    for (int i = 0; i < thread_count; i++)
    {
        sum += ((int*)stats)[i];
    }
    return sum;
}
