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
 * @file poll_basic.h  The Descriptor Control Block
 */

#include <maxscale/cdefs.h>

typedef struct mxs_poll_data
{
    /** Pointer to function that knows how to handle events for this particular
     *  'struct mxs_poll_data' structure.
     *
     * @param data    The `mxs_poll_data` instance that contained this pointer.
     * @param wid     The worker thread id.
     * @param events  The epoll events.
     */
    void (*handler)(struct mxs_poll_data *data, int wid, uint32_t events);

    struct
    {
        /**
         * The id of the worker thread
         */
        int id;
    } thread;
} MXS_POLL_DATA;

/**
 * A file descriptor should be added to the poll set of all workers.
 */
#define MXS_WORKER_ALL -1

/**
 * A file descriptor should be added to the poll set of some worker.
 */
#define MXS_WORKER_ANY -2

/**
 * Add a file descriptor with associated data to the poll set.
 *
 * @param wid      `MXS_WORKER_ALL` if the file descriptor should be added to the
 *                 poll set of all workers, `MXS_WORKER_ANY` if the file descriptor
 *                 should be added to some worker, otherwise the id of a worker.
 * @param fd       The file descriptor to be added.
 * @param events   Mask of epoll event types.
 * @param data     The structure containing the file descriptor to be
 *                 added.
 *
 *                 data->handler  : Handler that knows how to deal with events
 *                                  for this particular type of 'struct mxs_poll_data'.
 *                 data->thread.id: Will be updated by `poll_add_fd_to_worker`.
 *
 * @attention If the descriptor should be added to all workers, then the worker
 * thread id will be 0.
 *
 * @return 0 on success, non-zero on failure.
 */
int poll_add_fd_to_worker(int wid, int fd, uint32_t events, MXS_POLL_DATA* data);


/**
 * Remove a file descriptor from a poll set.
 *
 * @param wid      `MXS_WORKER_ALL` if the file descriptor should be removed from
 *                 the poll set of all workers; otherwise the id of a worker.
 * @param fd       The file descriptor to be removed.
 *
 * @return 0 on success, non-zero on failure.
 */
int poll_remove_fd_from_worker(int wid, int fd);
