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
#include <maxbase/atomic.h>
#include <maxscale/config.h>
#include <maxscale/clock.h>
#include <maxscale/platform.h>
#include <maxscale/server.h>
#include <maxscale/statistics.h>
#include "internal/poll.hh"
#include "internal/routingworker.hh"

using maxscale::Worker;
using maxscale::RoutingWorker;

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
    RoutingWorker::set_nonblocking_polls(nbpolls);
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
    RoutingWorker::set_maxwait(maxwait);
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

    Worker::STATISTICS s = RoutingWorker::get_statistics();

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
    dcb_printf(dcb, "Average event queue length:                    %" PRId64 "\n", s.evq_avg);
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

    dcb_printf(dcb, " ID | State      | #descriptors (curr) | #descriptors (tot)  | Load (1s) | Load (1m) | Load (1h) |\n");
    dcb_printf(dcb, "----+------------+---------------------+---------------------+-----------+-----------+-----------+\n");
    for (int i = 0; i < n_threads; i++)
    {
        Worker* worker = RoutingWorker::get(i);
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

        dcb_printf(dcb, " %2d | %10s | %19" PRIu32 " | %19" PRIu64 " | %9d | %9d | %9d |\n",
                   i, state, nCurrent, nTotal,
                   worker->load(Worker::Load::ONE_SECOND),
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

    Worker::STATISTICS s = RoutingWorker::get_statistics();

    dcb_printf(pdcb, "\nEvent statistics.\n");
    dcb_printf(pdcb, "Maximum queue time:           %3" PRId64 "00ms\n", s.maxqtime);
    dcb_printf(pdcb, "Maximum execution time:       %3" PRId64 "00ms\n", s.maxexectime);
    dcb_printf(pdcb, "Maximum event queue length:   %3" PRId64 "\n", s.evq_max);
    dcb_printf(pdcb, "Average event queue length:   %3" PRId64 "\n", s.evq_avg);
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
    return RoutingWorker::get_one_statistic(what);
}

/**
 * Return a result set that has the current set of services in it
 *
 * @return A Result set
 */
std::unique_ptr<ResultSet> eventTimesGetList()
{
    std::unique_ptr<ResultSet> set = ResultSet::create({"Duration", "No. Events Queued", "No. Events Executed"});
    char buf[40];
    Worker::STATISTICS stats = RoutingWorker::get_statistics();

    set->add_row({"< 100ms", std::to_string(stats.qtimes[0]), std::to_string(stats.exectimes[0])});

    for (int i = 1; i < Worker::STATISTICS::N_QUEUE_TIMES - 1; i++)
    {
        snprintf(buf, sizeof(buf), "%2d00 - %2d00ms", i, i + 1);
        set->add_row({buf, std::to_string(stats.qtimes[i]), std::to_string(stats.exectimes[i])});
    }

    int idx = Worker::STATISTICS::N_QUEUE_TIMES - 1;
    snprintf(buf, sizeof(buf), "> %2d00ms", Worker::STATISTICS::N_QUEUE_TIMES);
    set->add_row({buf, std::to_string(stats.qtimes[idx]), std::to_string(stats.exectimes[idx])});

    return set;
}
