/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/cdefs.h>
#include <maxbase/poll.h>

MXS_BEGIN_DECLS

typedef enum mxb_worker_msg_id_t
{
    /**
     * Shutdown message.
     *
     * arg1: 0
     * arg2: NULL
     */
    MXB_WORKER_MSG_SHUTDOWN,

    /**
     * Function call message.
     *
     * arg1: Pointer to function with the prototype: void (*)(MXB_WORKER*, void* arg2);
     * arg2: Second argument for the function passed in arg1.
     */
    MXB_WORKER_MSG_CALL
} mxb_worker_msg_id_t;

/**
 * Return the current worker.
 *
 * @return A worker, or NULL if there is no current worker.
 */
MXB_WORKER* mxb_worker_get_current();

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
bool mxb_worker_post_message(MXB_WORKER* worker, uint32_t msg_id, intptr_t arg1, intptr_t arg2);

MXB_END_DECLS
