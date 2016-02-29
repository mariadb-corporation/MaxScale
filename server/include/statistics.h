#ifndef _STATISTICS_HG
#define _STATISTICS_HG
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

/**
 * @file statistics.h  - Lock-free statistics gathering
 *
 * @verbatim
 * Revision History
 *
 * Date         Who              Description
 * 21/01/16     Markus Makela    Initial implementation
 * @endverbatim
 */

typedef void* ts_stats_t;

/** stats_init should be called only once */
void ts_stats_init();

/** No-op for now */
void ts_stats_end();

/** Every thread should call set_current_thread_id only once */
void ts_stats_set_thread_id(int id);

ts_stats_t ts_stats_alloc();
void ts_stats_free(ts_stats_t stats);
void ts_stats_add(ts_stats_t stats, int value);
void ts_stats_set(ts_stats_t stats, int value);
int ts_stats_sum(ts_stats_t stats);

#endif
