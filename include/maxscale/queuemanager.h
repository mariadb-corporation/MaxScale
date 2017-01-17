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
 * @file queuemanager.h  The Queue Manager header file
 *
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 27/04/2016   Martin Brampton         Initial implementation
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <stdbool.h>
#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

#define CONNECTION_QUEUE_LIMIT 1000

typedef struct queue_entry
{
    void            *queued_object;
    long            heartbeat;
#if defined(SS_DEBUG)
    long            sequence_check;
#endif /* SS_DEBUG */
} QUEUE_ENTRY;

typedef struct queue_config
{
    int             queue_limit;
    int             start;
    int             end;
    int             timeout;
    bool            has_entries;
    SPINLOCK        queue_lock;
    QUEUE_ENTRY     *queue_array;
#if defined(SS_DEBUG)
    long            sequence_number;
#endif /* SS_DEBUG */
} QUEUE_CONFIG;

QUEUE_CONFIG *mxs_queue_alloc(int limit, int timeout);
void mxs_queue_free(QUEUE_CONFIG *queue_config);
bool mxs_enqueue(QUEUE_CONFIG *queue_config, void *new_entry);
bool mxs_dequeue(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result);
bool mxs_dequeue_if_expired(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result);

static inline int
mxs_queue_count(QUEUE_CONFIG *queue_config)
{
    int count = queue_config->end - queue_config->start;
    return count < 0 ? (count + queue_config->queue_limit + 1) : count;
}

MXS_END_DECLS
