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
 * @file core/maxscale/queuemanager.h - The private queuemanager interface
 */

#include <maxscale/queuemanager.h>

#include <maxscale/spinlock.h>

MXS_BEGIN_DECLS

typedef struct queue_entry
{
    void            *queued_object;
    long            heartbeat;
#if defined(SS_DEBUG)
    long            sequence_check;
#endif /* SS_DEBUG */
} QUEUE_ENTRY;

struct queue_config
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
};

QUEUE_CONFIG *mxs_queue_alloc(int limit, int timeout);
void mxs_queue_free(QUEUE_CONFIG *queue_config);
bool mxs_enqueue(QUEUE_CONFIG *queue_config, void *new_entry);
bool mxs_dequeue(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result);
bool mxs_dequeue_if_expired(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result);

MXS_END_DECLS
