#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <maxscale/poll.h>
#include <maxscale/thread.h>
#include <maxscale/jansson.h>

MXS_BEGIN_DECLS

typedef struct mxs_worker
{
} MXS_WORKER;

enum mxs_worker_msg_id
{
    /**
     * Ping message.
     *
     * arg1: 0
     * arg2: NULL or pointer to dynamically allocated NULL-terminated string,
     *       to be freed by worker.
     */
    MXS_WORKER_MSG_PING,

    /**
     * Shutdown message.
     *
     * arg1: 0
     * arg2: NULL
     */
    MXS_WORKER_MSG_SHUTDOWN,

    /**
     * Function call message.
     *
     * arg1: Pointer to function with the prototype: void (*)(int thread_id, void* arg2);
     * arg2: Second argument for the function passed in arg1.
     */
    MXS_WORKER_MSG_CALL
};

/**
 * Return the worker associated with the provided worker id.
 *
 * @param worker_id  A worker id.
 *
 * @return The corresponding worker instance, or NULL if the id does
 *         not correspond to a worker.
 */
MXS_WORKER* mxs_worker_get(int worker_id);

/**
 * Return the id of the worker.
 *
 * @param worker  A worker.
 *
 * @return The id of the worker.
 */
int mxs_worker_id(MXS_WORKER* pWorker);

/**
 * Return the current worker.
 *
 * @return A worker, or NULL if there is no current worker.
 */
MXS_WORKER* mxs_worker_get_current();

/**
 * Return the id of the worker.
 *
 * @return The id of the worker, or -1 if there is no current worker.
 */
int mxs_worker_get_current_id();

/**
 * Post a message to a worker.
 *
 * @param worker  The worker to whom the message should be sent.
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 *
 * @return True if the message could be sent, false otherwise. If the message
 *         posting fails, errno is set appropriately.
 *
 * @attention The return value tells *only* whether the message could be sent,
 *            *not* that it has reached the worker.
 *
 * @attention This function is signal safe.
 */
bool mxs_worker_post_message(MXS_WORKER* worker, uint32_t msg_id, intptr_t arg1, intptr_t arg2);

/**
 * Broadcast a message to all worker.
 *
 * @param msg_id  The message id.
 * @param arg1    Message specific first argument.
 * @param arg2    Message specific second argument.
 *
 * @return The number of messages posted; if less that ne number of workers
 *         then some postings failed.
 *
 * @attention The return value tells *only* whether message could be posted,
 *            *not* that it has reached the worker.
 *
 * @attentsion Exactly the same arguments are passed to all workers. Take that
 *             into account if the passed data must be freed.
 *
 * @attention This function is signal safe.
 */
size_t mxs_worker_broadcast_message(uint32_t msg_id, intptr_t arg1, intptr_t arg2);

/**
 * @brief Convert a worker to JSON format
 *
 * @param host Hostname of this server
 * @param id   ID of the worker
 *
 * @return JSON resource representing the worker
 */
json_t* mxs_worker_to_json(const char* host, int id);

/**
 * Convert workers into JSON format
 *
 * @param host Hostname of this server
 *
 * @return A JSON resource collection of workers
 *
 * @see mxs_json_resource()
 */
json_t* mxs_worker_list_to_json(const char* host);

MXS_END_DECLS
