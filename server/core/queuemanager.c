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
 * @file queuemanager.c  -  Logic for FIFO queue handling
 *
 * MaxScale contains a number of FIFO queues. This code attempts to provide
 * standard functions for handling them.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 27/04/16     Martin Brampton         Initial implementation
 *
 * @endverbatim
 */
#include <maxscale/queuemanager.h>

#include <stdlib.h>
#include <stdio.h>

#include <maxscale/alloc.h>
#include <maxscale/debug.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/log_manager.h>
#include <maxscale/spinlock.h>

#include "maxscale/queuemanager.h"

#if defined(SS_DEBUG)
int debug_check_fail = 0;
#endif /* SS_DEBUG */

static inline int mxs_queue_count(QUEUE_CONFIG*);

/**
 * @brief Allocate a new queue
 *
 * Provides for FIFO queues, this is the first operation to be requested
 * for the use of a queue.
 *
 * @param limit         The maximum size of the queue
 * @param timeout       The maximum time for which an entry is valid
 * @return QUEUE_CONFIG A queue configuration and anchor structure
 */
QUEUE_CONFIG
*mxs_queue_alloc(int limit, int timeout)
{
    QUEUE_CONFIG *new_queue = (QUEUE_CONFIG *)MXS_CALLOC(1, sizeof(QUEUE_CONFIG));
    if (new_queue)
    {
        new_queue->queue_array = MXS_CALLOC(limit + 1, sizeof(QUEUE_ENTRY));
        if (new_queue->queue_array)
        {
            new_queue->queue_limit = limit;
            new_queue->timeout = timeout;
            spinlock_init(&new_queue->queue_lock);
#if defined(SS_DEBUG)
            new_queue->sequence_number = 0;
#endif /* SS_DEBUG */
            return new_queue;
        }
        MXS_FREE(new_queue);
    }
    return NULL;
}

/**
 * @brief Free a queue configuration
 *
 * Provides for FIFO queues, this is the last operation to be requested, when
 * there is no further use for the queue.
 *
 * @param QUEUE_CONFIG A queue configuration and anchor structure
 */
void mxs_queue_free(QUEUE_CONFIG *queue_config)
{
    if (queue_config)
    {
        MXS_FREE(queue_config->queue_array);
        MXS_FREE(queue_config);
    }
}

/**
 * @brief Add an item to a queue
 *
 * Add a new item to a FIFO queue. If the queue config is null, this function
 * will behave as if the queue is full.
 *
 * @param queue_config  The configuration and anchor structure for the queue
 * @param new_entry     The new entry, to be added
 * @return bool         Whether the enqueue succeeded
 */
bool mxs_enqueue(QUEUE_CONFIG *queue_config, void *new_entry)
{
    bool result = false;

    if (queue_config)
    {
        spinlock_acquire(&queue_config->queue_lock);
        if (mxs_queue_count(queue_config) < queue_config->queue_limit)
        {
            queue_config->queue_array[queue_config->end].queued_object = new_entry;
            queue_config->queue_array[queue_config->end].heartbeat = hkheartbeat;
#if defined(SS_DEBUG)
            queue_config->queue_array[queue_config->end].sequence_check = queue_config->sequence_number;
            queue_config->sequence_number++;
#endif /* SS_DEBUG */
            queue_config->end++;
            if (queue_config->end > queue_config->queue_limit)
            {
                queue_config->end = 0;
            }
            queue_config->has_entries = true;
            result = true;
        }
        else
        {
            result = false;
        }
        spinlock_release(&queue_config->queue_lock);
    }
    return result;
}

/**
 * @brief Remove an item from a queue
 *
 * Remove an item from a FIFO queue. If the queue config is NULL, the function
 * will behave as if for an empty queue.
 *
 * @param queue_config  The configuration and anchor structure for the queue
 * @param result        A queue entry structure that will receive the result
 * @return bool indicating whether an item was successfully dequeued
 */
bool mxs_dequeue(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result)
{
    QUEUE_ENTRY *found = NULL;

    if (queue_config && queue_config->has_entries)
    {
        spinlock_acquire(&queue_config->queue_lock);
        if (mxs_queue_count(queue_config) > 0)
        {
            found = &(queue_config->queue_array[queue_config->start]);
#if defined(SS_DEBUG)
            ss_dassert((queue_config->sequence_number) == (found->sequence_check + mxs_queue_count(queue_config)));
            if ((queue_config->sequence_number) != (found->sequence_check + mxs_queue_count(queue_config)))
            {
                debug_check_fail++;
            }
#endif /* SS_DEBUG */
            result->heartbeat = found->heartbeat;
            result->queued_object = found->queued_object;
            if (++queue_config->start > queue_config->queue_limit)
            {
                queue_config->start = 0;
            }
            queue_config->has_entries = (mxs_queue_count(queue_config) > 0);
        }
        spinlock_release(&queue_config->queue_lock);
    }
    return (found != NULL);
}

/**
 * @brief Remove an item from a queue if it has passed the timeout limit
 *
 * Remove an item from a FIFO queue if expired. If the queue config is NULL,
 * the function will behave as for an empty queue.
 *
 * @param queue_config  The configuration and anchor structure for the queue
 * @param result        A queue entry structure that will receive the result
 * @return bool indicating whether an item was successfully dequeued
 */
bool mxs_dequeue_if_expired(QUEUE_CONFIG *queue_config, QUEUE_ENTRY *result)
{
    QUEUE_ENTRY *found = NULL;

    if (queue_config && queue_config->has_entries)
    {
        spinlock_acquire(&queue_config->queue_lock);
        if (mxs_queue_count(queue_config) > 0)
        {
            found = &(queue_config->queue_array[queue_config->start]);
            if (found->heartbeat > hkheartbeat - queue_config->timeout)
            {
                found = NULL;
            }
            else
            {
#if defined(SS_DEBUG)
                ss_dassert((queue_config->sequence_number) == (found->sequence_check + mxs_queue_count(queue_config)));
                if ((queue_config->sequence_number) != (found->sequence_check + mxs_queue_count(queue_config)))
                {
                    debug_check_fail++;
                }
#endif /* SS_DEBUG */
                result->heartbeat = found->heartbeat;
                result->queued_object = found->queued_object;
                if (++queue_config->start > queue_config->queue_limit)
                {
                    queue_config->start = 0;
                }
                queue_config->has_entries = (mxs_queue_count(queue_config) > 0);
            }
        }
        spinlock_release(&queue_config->queue_lock);
    }
    return (found != NULL);
}

static inline int mxs_queue_count(QUEUE_CONFIG *queue_config)
{
    int count = queue_config->end - queue_config->start;
    return count < 0 ? (count + queue_config->queue_limit + 1) : count;
}