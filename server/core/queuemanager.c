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
#include <stdlib.h>
#include <queuemanager.h>
#include <spinlock.h>
#include <log_manager.h>
#include <hk_heartbeat.h>

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
    QUEUE_CONFIG *new_queue = NULL;
    if (limit > CONNECTION_QUEUE_LIMIT)
    {
        MXS_ERROR("Limit configured for connection queue exceeds system maximum");
        limit = CONNECTION_QUEUE_LIMIT;
    }
    new_queue = (QUEUE_CONFIG *)calloc(1, sizeof(QUEUE_CONFIG));
    if (new_queue)
    {
        new_queue->queue_size = CONNECTION_QUEUE_LIMIT;
        new_queue->queue_limit = limit;
        new_queue->timeout = timeout;
        spinlock_init(&new_queue->queue_lock);
    }
    else
    {
        MXS_ERROR("Failed to allocate memory for new queue in mxs_queue_alloc");
    }
    return new_queue;
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
    free(queue_config);
}

/**
 * @brief Add an item to a queue
 *
 * Add a new item to a FIFO queue
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
        if (queue_config->queue_limit < queue_config->queue_size
            && mxs_queue_count(queue_config) < queue_config->queue_size)
        {
            queue_config->queue_array[queue_config->end].queued_object = new_entry;
            queue_config->queue_array[queue_config->end].heartbeat = hkheartbeat;
            queue_config->end++;
            if (queue_config->end >= queue_config->queue_size)
            {
                queue_config->end = 0;
            }
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
 * Remove an item from a FIFO queue
 *
 * @param queue_config  The configuration and anchor structure for the queue
 * @return QUEUE_ENTRY  A structure defining the removed entry
 */
QUEUE_ENTRY *mxs_dequeue(QUEUE_CONFIG *queue_config)
{
    int count;
    QUEUE_ENTRY *result = NULL;

    spinlock_acquire(&queue_config->queue_lock);
    if (NULL == result && (count = mxs_queue_count(queue_config)) > 0)
    {
        result = &(queue_config->queue_array[queue_config->start++]);
        if (queue_config->start >= queue_config->queue_size)
        {
            queue_config->start = 0;
        }
    }
    spinlock_release(&queue_config->queue_lock);
    return result;
}
