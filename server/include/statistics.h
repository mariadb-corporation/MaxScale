#ifndef _STATISTICS_HG
#define _STATISTICS_HG
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
