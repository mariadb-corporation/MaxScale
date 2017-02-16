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

#include <maxscale/cdefs.h>
#include <maxscale/poll.h>
#include <maxscale/thread.h>

MXS_BEGIN_DECLS

typedef struct mxs_worker
{
    MXS_POLL_DATA poll;     /*< The poll data used by the polling mechanism. */
    int           id;       /*< The id of the worker. */
    int           read_fd;  /*< The file descriptor the worked reads from. */
    int           write_fd; /*< The file descriptor used for sending data to the worker. */
    THREAD        thread;   /*< The thread handle of the worker. */
    bool          started;  /*< Whether the thread has been started or not. */
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
    MXS_WORKER_MSG_PING
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
static inline int mxs_worker_id(MXS_WORKER* worker)
{
    return worker->id;
}

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
 */
bool mxs_worker_post_message(MXS_WORKER* worker, int msg_id, int64_t arg1, void* arg2);

MXS_END_DECLS
