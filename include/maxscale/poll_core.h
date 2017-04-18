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
#include <sys/epoll.h>

MXS_BEGIN_DECLS

typedef enum mxs_poll_action
{
    MXS_POLL_NOP    = 0x00,
    MXS_POLL_ACCEPT = 0x01,
    MXS_POLL_READ   = 0x02,
    MXS_POLL_WRITE  = 0x04,
    MXS_POLL_HUP    = 0x08,
    MXS_POLL_ERROR  = 0x10,
} mxs_poll_action_t;

struct mxs_poll_data;
/** Pointer to function that knows how to handle events for a particular
 *  'struct mxs_poll_data' structure.
 *
 * @param data    The `mxs_poll_data` instance that contained this pointer.
 * @param wid     The worker thread id.
 * @param events  The epoll events.
 *
 * @return A combination of mxs_poll_action_t enumeration values.
 */
typedef uint32_t (*mxs_poll_handler_t)(struct mxs_poll_data* data, int wid, uint32_t events);

typedef struct mxs_poll_data
{
    mxs_poll_handler_t handler; /*< Handler for this particular kind of mxs_poll_data. */
    struct
    {
        int id;                 /*< The id of the worker thread. */
    } thread;
} MXS_POLL_DATA;

// TODO: Temporarily moved here.
/**
 * The number of buckets used to gather statistics about how many
 * descriptors where processed on each epoll completion.
 *
 * An array of wakeup counts is created, with the number of descriptors used
 * to index that array. Each time a completion occurs the n_fds - 1 value is
 * used to index this array and increment the count held there.
 * If n_fds - 1 >= MAXFDS then the count at MAXFDS -1 is incremented.
 */
#define MAXNFDS 10

// TODO: Temporarily moved here.
typedef struct
{
    int64_t n_read;         /*< Number of read events   */
    int64_t n_write;        /*< Number of write events  */
    int64_t n_error;        /*< Number of error events  */
    int64_t n_hup;          /*< Number of hangup events */
    int64_t n_accept;       /*< Number of accept events */
    int64_t n_polls;        /*< Number of poll cycles   */
    int64_t n_pollev;       /*< Number of polls returning events */
    int64_t n_nbpollev;     /*< Number of polls returning events */
    int32_t n_fds[MAXNFDS]; /*< Number of wakeups with particular n_fds value */
    int64_t evq_length;     /*< Event queue length */
    int64_t evq_max;        /*< Maximum event queue length */
    int64_t blockingpolls;  /*< Number of epoll_waits with a timeout specified */
} POLL_STATS;

// TODO: Temporarily moved here.
extern POLL_STATS* pollStats;

// TODO: Temporarily moved here.
#define N_QUEUE_TIMES   30

// TODO: Temporarily moved here.
/**
 * The event queue statistics
 */
typedef struct
{
    uint32_t qtimes[N_QUEUE_TIMES + 1];
    uint32_t exectimes[N_QUEUE_TIMES + 1];
    int64_t  maxqtime;
    int64_t  maxexectime;
} QUEUE_STATS;

// TODO: Temporarily moved here.
extern QUEUE_STATS* queueStats;

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
 *            thread id will be 0.
 *
 * @attention The provided file descriptor *must* be non-blocking.
 *
 * @return True on success, false on failure.
 */
bool poll_add_fd_to_worker(int wid, int fd, uint32_t events, MXS_POLL_DATA* data);


/**
 * Remove a file descriptor from a poll set.
 *
 * @param wid      `MXS_WORKER_ALL` if the file descriptor should be removed from
 *                 the poll set of all workers; otherwise the id of a worker.
 * @param fd       The file descriptor to be removed.
 *
 * @return True on success, false on failure.
 */
bool poll_remove_fd_from_worker(int wid, int fd);

/**
 * Check whether there are cross-thread messages for current thread.
 *
 * @attention Only to be called by the system.
 */
void poll_check_message(void);

MXS_END_DECLS
