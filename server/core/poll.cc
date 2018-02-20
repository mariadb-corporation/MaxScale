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

/**
 * @file poll.c  - Abstraction of the epoll functionality
 */

#include <maxscale/poll.h>

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <mysql.h>
#include <sys/epoll.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/config.h>
#include <maxscale/hk_heartbeat.h>
#include <maxscale/platform.h>
#include <maxscale/server.h>
#include <maxscale/statistics.h>
#include "internal/poll.h"
#include "internal/worker.hh"

using maxscale::Worker;

static int next_epoll_fd = 0;              /*< Which thread handles the next DCB */
static int n_threads;                      /*< Number of threads */

/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
    n_threads = config_threadcount();
}

static bool add_fd_to_worker(int wid, int fd, uint32_t events, MXS_POLL_DATA* data)
{
    ss_dassert((wid >= 0) && (wid <= n_threads));

    Worker* worker = Worker::get(wid);
    ss_dassert(worker);

    return worker->add_fd(fd, events, data);
}

static bool add_fd_to_workers(int fd, uint32_t events, MXS_POLL_DATA* data)
{
    bool rv = true;
    int thread_id = data->thread.id;

    rv = Worker::add_shared_fd(fd, events, data);

    if (rv)
    {
        // The DCB will appear on the list of the calling thread.
        int wid = Worker::get_current_id();

        if (wid == -1)
        {
            // TODO: Listeners are created before the workers have been started.
            // TODO: Hence the returned id will be -1. We change it to 0, which in
            // TODO: practice will mean that they will end up on the Worker running
            // TODO: in the main thread. This needs to be sorted out.
            wid = 0;
        }

        data->thread.id = wid;
    }
    else
    {
        // Restore the situation.
        data->thread.id = thread_id;
    }

    return rv;
}

bool poll_add_fd_to_worker(int wid, int fd, uint32_t events, MXS_POLL_DATA* data)
{
    bool rv;

    if (wid == MXS_WORKER_ANY)
    {
        wid = (int)atomic_add(&next_epoll_fd, 1) % n_threads;

        rv = add_fd_to_worker(wid, fd, events, data);
    }
    else if (wid == MXS_WORKER_ALL)
    {
        rv = add_fd_to_workers(fd, events, data);
    }
    else
    {
        ss_dassert((wid >= 0) && (wid < n_threads));

        rv = add_fd_to_worker(wid, fd, events, data);
    }

    return rv;
}

static bool remove_fd_from_worker(int wid, int fd)
{
    ss_dassert((wid >= 0) && (wid < n_threads));

    Worker* worker = Worker::get(wid);
    ss_dassert(worker);

    return worker->remove_fd(fd);
}

static bool remove_fd_from_workers(int fd)
{
    return Worker::remove_shared_fd(fd);
}

bool poll_remove_fd_from_worker(int wid, int fd)
{
    bool rv;

    if (wid == MXS_WORKER_ALL)
    {
        rv = remove_fd_from_workers(fd);
    }
    else
    {
        rv = remove_fd_from_worker(wid, fd);
    }

    return rv;
}

/**
 * Set the number of non-blocking poll cycles that will be done before
 * a blocking poll will take place. Whenever an event arrives on a thread
 * or the thread sees a pending event to execute it will reset it's
 * poll_spin coutn to zero and will then poll with a 0 timeout until the
 * poll_spin value is greater than the value set here.
 *
 * @param nbpolls       Number of non-block polls to perform before blocking
 */
void
poll_set_nonblocking_polls(unsigned int nbpolls)
{
    Worker::set_nonblocking_polls(nbpolls);
}

/**
 * Set the maximum amount of time, in milliseconds, the polling thread
 * will block before it will wake and check the event queue for work
 * that may have been added by another thread.
 *
 * @param maxwait       Maximum wait time in milliseconds
 */
void
poll_set_maxwait(unsigned int maxwait)
{
    Worker::set_maxwait(maxwait);
}

/**
 * Display an entry from the spinlock statistics data
 *
 * @param       dcb     The DCB to print to
 * @param       desc    Description of the statistic
 * @param       value   The statistic value
 */
static void
spin_reporter(void *dcb, char *desc, int value)
{
    dcb_printf((DCB *)dcb, "\t%-40s  %d\n", desc, value);
}

/**
 * Debug routine to print the polling statistics
 *
 * @param dcb   DCB to print to
 */
void
dprintPollStats(DCB *dcb)
{
    int i;

    Worker::STATISTICS s = Worker::get_statistics();

    dcb_printf(dcb, "\nPoll Statistics.\n\n");
    dcb_printf(dcb, "No. of epoll cycles:                           %" PRId64 "\n", s.n_polls);
    dcb_printf(dcb, "No. of epoll cycles with wait:                 %" PRId64 "\n", s.blockingpolls);
    dcb_printf(dcb, "No. of epoll calls returning events:           %" PRId64 "\n", s.n_pollev);
    dcb_printf(dcb, "No. of non-blocking calls returning events:    %" PRId64 "\n", s.n_nbpollev);
    dcb_printf(dcb, "No. of read events:                            %" PRId64 "\n", s.n_read);
    dcb_printf(dcb, "No. of write events:                           %" PRId64 "\n", s.n_write);
    dcb_printf(dcb, "No. of error events:                           %" PRId64 "\n", s.n_error);
    dcb_printf(dcb, "No. of hangup events:                          %" PRId64 "\n", s.n_hup);
    dcb_printf(dcb, "No. of accept events:                          %" PRId64 "\n", s.n_accept);
    dcb_printf(dcb, "Total event queue length:                      %" PRId64 "\n", s.evq_length);
    dcb_printf(dcb, "Average event queue length:                    %" PRId64 "\n", s.evq_length);
    dcb_printf(dcb, "Maximum event queue length:                    %" PRId64 "\n", s.evq_max);

    dcb_printf(dcb, "No of poll completions with descriptors\n");
    dcb_printf(dcb, "\tNo. of descriptors\tNo. of poll completions.\n");
    for (i = 0; i < Worker::STATISTICS::MAXNFDS - 1; i++)
    {
        dcb_printf(dcb, "\t%2d\t\t\t%" PRId64 "\n", i + 1, s.n_fds[i]);
    }

    dcb_printf(dcb, "\t>= %d\t\t\t%" PRId64 "\n",
               Worker::STATISTICS::MAXNFDS, s.n_fds[Worker::STATISTICS::MAXNFDS - 1]);

}

/**
 * Print the thread status for all the polling threads
 *
 * @param dcb   The DCB to send the thread status data
 */
void
dShowThreads(DCB *dcb)
{
    dcb_printf(dcb, "Polling Threads.\n\n");

    dcb_printf(dcb, " ID | State      | #descriptors (curr) | #descriptors (tot)  | Load (10s) | Load (1m) | Load (1h) |\n");
    dcb_printf(dcb, "----+------------+---------------------+---------------------+------------+-----------+-----------+\n");
    for (int i = 0; i < n_threads; i++)
    {
        Worker* worker = Worker::get(i);
        ss_dassert(worker);

        const char *state = "Unknown";

        switch (worker->state())
        {
        case Worker::STOPPED:
            state = "Stopped";
            break;
        case Worker::IDLE:
            state = "Idle";
            break;
        case Worker::POLLING:
            state = "Polling";
            break;
        case Worker::PROCESSING:
            state = "Processing";
            break;
        case Worker::ZPROCESSING:
            state = "Collecting";
            break;

        default:
            ss_dassert(!true);
        }

        uint32_t nCurrent;
        uint64_t nTotal;

        worker->get_descriptor_counts(&nCurrent, &nTotal);

        dcb_printf(dcb, " %2d | %10s | %19" PRIu32 " | %19" PRIu64 " | %10d | %9d | %9d |\n",
                   i, state, nCurrent, nTotal,
                   worker->load(Worker::Load::TEN_SECONDS),
                   worker->load(Worker::Load::ONE_MINUTE),
                   worker->load(Worker::Load::ONE_HOUR));
    }
}

/**
 * Print the event queue statistics
 *
 * @param pdcb          The DCB to print the event queue to
 */
void
dShowEventStats(DCB *pdcb)
{
    int i;

    Worker::STATISTICS s = Worker::get_statistics();

    dcb_printf(pdcb, "\nEvent statistics.\n");
    dcb_printf(pdcb, "Maximum queue time:           %3" PRId64 "00ms\n", s.maxqtime);
    dcb_printf(pdcb, "Maximum execution time:       %3" PRId64 "00ms\n", s.maxexectime);
    dcb_printf(pdcb, "Maximum event queue length:   %3" PRId64 "\n", s.evq_max);
    dcb_printf(pdcb, "Average event queue length:   %3" PRId64 "\n", s.evq_length);
    dcb_printf(pdcb, "\n");
    dcb_printf(pdcb, "               |    Number of events\n");
    dcb_printf(pdcb, "Duration       | Queued     | Executed\n");
    dcb_printf(pdcb, "---------------+------------+-----------\n");

    dcb_printf(pdcb, " < 100ms       | %-10d | %-10d\n", s.qtimes[0], s.exectimes[0]);

    for (i = 1; i < Worker::STATISTICS::N_QUEUE_TIMES; i++)
    {
        dcb_printf(pdcb, " %2d00 - %2d00ms | %-10d | %-10d\n", i, i + 1, s.qtimes[i], s.exectimes[i]);
    }

    dcb_printf(pdcb, " > %2d00ms      | %-10d | %-10d\n", Worker::STATISTICS::N_QUEUE_TIMES,
               s.qtimes[Worker::STATISTICS::N_QUEUE_TIMES], s.exectimes[Worker::STATISTICS::N_QUEUE_TIMES]);
}

/**
 * Return a poll statistic from the polling subsystem
 *
 * @param what  The required statistic
 * @return      The value of that statistic
 */
int64_t
poll_get_stat(POLL_STAT what)
{
    return Worker::get_one_statistic(what);
}

namespace
{

struct EVENT_TIMES_CB_DATA
{
    int rowno;
    Worker::STATISTICS* stats;
};

}

/**
 * Provide a row to the result set that defines the event queue statistics
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
eventTimesRowCallback(RESULTSET *set, void *v)
{
    EVENT_TIMES_CB_DATA* data = (EVENT_TIMES_CB_DATA*)v;

    char buf[40];
    RESULT_ROW *row;

    if (data->rowno >= Worker::STATISTICS::N_QUEUE_TIMES)
    {
        MXS_FREE(data);
        return NULL;
    }
    row = resultset_make_row(set);
    if (data->rowno == 0)
    {
        resultset_row_set(row, 0, "< 100ms");
    }
    else if (data->rowno == Worker::STATISTICS::N_QUEUE_TIMES - 1)
    {
        snprintf(buf, 39, "> %2d00ms", Worker::STATISTICS::N_QUEUE_TIMES);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }
    else
    {
        snprintf(buf, 39, "%2d00 - %2d00ms", data->rowno, data->rowno + 1);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }

    snprintf(buf, 39, "%u", data->stats->qtimes[data->rowno]);
    buf[39] = '\0';
    resultset_row_set(row, 1, buf);
    snprintf(buf, 39, "%u", data->stats->exectimes[data->rowno]);
    buf[39] = '\0';
    resultset_row_set(row, 2, buf);
    data->rowno++;
    return row;
}

/**
 * Return a result set that has the current set of services in it
 *
 * @return A Result set
 */
RESULTSET *
eventTimesGetList()
{
    RESULTSET *set;
    EVENT_TIMES_CB_DATA *data;

    if ((data = (EVENT_TIMES_CB_DATA*)MXS_MALLOC(sizeof(EVENT_TIMES_CB_DATA))) == NULL)
    {
        return NULL;
    }

    Worker::STATISTICS s = Worker::get_statistics();

    data->rowno = 0;
    data->stats = &s;

    if ((set = resultset_create(eventTimesRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Duration", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "No. Events Queued", 12, COL_TYPE_VARCHAR);
    resultset_add_column(set, "No. Events Executed", 12, COL_TYPE_VARCHAR);

    return set;
}
