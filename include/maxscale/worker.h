#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cdefs.h>
#include <maxscale/poll.h>
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
     * arg1: Pointer to function with the prototype: void (*)(MXS_WORKER*, void* arg2);
     * arg2: Second argument for the function passed in arg1.
     */
    MXS_WORKER_MSG_CALL
};

/**
 * Return the current worker.
 *
 * @return A worker, or NULL if there is no current worker.
 */
MXS_WORKER* mxs_worker_get_current();

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

MXS_END_DECLS
