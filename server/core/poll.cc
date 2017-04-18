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
#include "maxscale/poll.h"
#include "maxscale/worker.hh"

using maxscale::Worker;

static thread_local int current_thread_id; /*< This thread's ID */
static int next_epoll_fd = 0;              /*< Which thread handles the next DCB */
static int n_threads;                      /*< Number of threads */
static int number_poll_spins;              /*< Maximum non-block polls */
static int max_poll_sleep;                 /*< Maximum block time */


/** Poll cross-thread messaging variables */
static volatile int     *poll_msg;
static void    *poll_msg_data = NULL;
static SPINLOCK poll_msg_lock = SPINLOCK_INIT;

static void poll_check_message(void);

/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
    n_threads = config_threadcount();

    if ((poll_msg = (int*)MXS_CALLOC(n_threads, sizeof(int))) == NULL)
    {
        exit(-1);
    }

    number_poll_spins = config_nbpolls();
    max_poll_sleep = config_pollsleep();
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

    for (int i = 0; i < n_threads; i++)
    {
        Worker* worker = Worker::get(i);
        ss_dassert(worker);

        if (!worker->add_fd(fd, events, data))
        {
            /** Remove the fd from the previous epoll instances */
            for (int j = 0; j < i; j++)
            {
                Worker* worker = Worker::get(j);
                ss_dassert(worker);

                worker->remove_fd(fd);
            }
            rv = false;
            break;
        }
    }

    if (rv)
    {
        // The DCB will appear on the list of the calling thread.
        data->thread.id = current_thread_id;
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
    int rc;

    for (int i = 0; i < n_threads; ++i)
    {
        Worker* worker = Worker::get(i);
        ss_dassert(worker);
        // We don't care about the result, anything serious and the
        // function will not return and the process taken down.
        worker->remove_fd(fd);
    }

    return true;
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
 * The main polling loop
 *
 * @param epoll_fd         The epoll descriptor.
 * @param thread_id        The id of the calling thread.
 * @param poll_stats       The polling stats of the calling thread.
 * @param queue_stats      The queue stats of the calling thread.
 * @param should_shutdown  Pointer to function returning true if the polling should
 *                         be terminated.
 * @param data             Data provided to the @c should_shutdown function.
 */
void poll_waitevents(int epoll_fd,
                     int thread_id,
                     POLL_STATS* poll_stats,
                     QUEUE_STATS* queue_stats,
                     bool (*should_shutdown)(void* data),
                     void* data)
{
    current_thread_id = thread_id;

    struct epoll_event events[MAX_EVENTS];
    int i, nfds, timeout_bias = 1;
    int poll_spins = 0;

    poll_stats->thread_state = THREAD_IDLE;

    while (!should_shutdown(data))
    {
        poll_stats->thread_state = THREAD_POLLING;

        atomic_add_int64(&poll_stats->n_polls, 1);
        if ((nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 0)) == -1)
        {
            int eno = errno;
            errno = 0;
            MXS_DEBUG("%lu [poll_waitevents] epoll_wait returned "
                      "%d, errno %d",
                      pthread_self(),
                      nfds,
                      eno);
        }
        /*
         * If there are no new descriptors from the non-blocking call
         * and nothing to process on the event queue then for do a
         * blocking call to epoll_wait.
         *
         * We calculate a timeout bias to alter the length of the blocking
         * call based on the time since we last received an event to process
         */
        else if (nfds == 0 && poll_spins++ > number_poll_spins)
        {
            if (timeout_bias < 10)
            {
                timeout_bias++;
            }
            atomic_add_int64(&poll_stats->blockingpolls, 1);
            nfds = epoll_wait(epoll_fd,
                              events,
                              MAX_EVENTS,
                              (max_poll_sleep * timeout_bias) / 10);
            if (nfds == 0)
            {
                poll_spins = 0;
            }
        }

        if (nfds > 0)
        {
            poll_stats->evq_length = nfds;
            if (nfds > poll_stats->evq_max)
            {
                poll_stats->evq_max = nfds;
            }

            timeout_bias = 1;
            if (poll_spins <= number_poll_spins + 1)
            {
                atomic_add_int64(&poll_stats->n_nbpollev, 1);
            }
            poll_spins = 0;
            MXS_DEBUG("%lu [poll_waitevents] epoll_wait found %d fds",
                      pthread_self(),
                      nfds);
            atomic_add_int64(&poll_stats->n_pollev, 1);

            poll_stats->thread_state = THREAD_PROCESSING;

            poll_stats->n_fds[(nfds < MAXNFDS ? (nfds - 1) : MAXNFDS - 1)]++;
        }

        uint64_t cycle_start = hkheartbeat;

        for (int i = 0; i < nfds; i++)
        {
            /** Calculate event queue statistics */
            int64_t started = hkheartbeat;
            int64_t qtime = started - cycle_start;

            if (qtime > N_QUEUE_TIMES)
            {
                queue_stats->qtimes[N_QUEUE_TIMES]++;
            }
            else
            {
                queue_stats->qtimes[qtime]++;
            }

            queue_stats->maxqtime = MXS_MAX(queue_stats->maxqtime, qtime);

            MXS_POLL_DATA *data = (MXS_POLL_DATA*)events[i].data.ptr;

            uint32_t actions = data->handler(data, thread_id, events[i].events);

            if (actions & MXS_POLL_ACCEPT)
            {
                atomic_add_int64(&poll_stats->n_accept, 1);
            }

            if (actions & MXS_POLL_READ)
            {
                atomic_add_int64(&poll_stats->n_read, 1);
            }

            if (actions & MXS_POLL_WRITE)
            {
                atomic_add_int64(&poll_stats->n_write, 1);
            }

            if (actions & MXS_POLL_HUP)
            {
                atomic_add_int64(&poll_stats->n_hup, 1);
            }

            if (actions & MXS_POLL_ERROR)
            {
                atomic_add_int64(&poll_stats->n_error, 1);
            }

            /** Calculate event execution statistics */
            qtime = hkheartbeat - started;

            if (qtime > N_QUEUE_TIMES)
            {
                queue_stats->exectimes[N_QUEUE_TIMES]++;
            }
            else
            {
                queue_stats->exectimes[qtime % N_QUEUE_TIMES]++;
            }

            queue_stats->maxexectime = MXS_MAX(queue_stats->maxexectime, qtime);
        }

        dcb_process_idle_sessions(thread_id);

        poll_stats->thread_state = THREAD_ZPROCESSING;

        /** Process closed DCBs */
        dcb_process_zombies(thread_id);

        poll_check_message();

        poll_stats->thread_state = THREAD_IDLE;
    } /*< while(1) */

    poll_stats->thread_state = THREAD_STOPPED;
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
    number_poll_spins = nbpolls;
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
    max_poll_sleep = maxwait;
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

namespace
{

template<class T>
int64_t stats_get(T* ts, int64_t T::*what, enum ts_stats_type type)
{
    int64_t best = type == TS_STATS_MAX ? LONG_MIN : (type == TS_STATS_MIX ? LONG_MAX : 0);

    for (int i = 0; i < n_threads; ++i)
    {
        T* t = &ts[i];
        int64_t value = t->*what;

        switch (type)
        {
        case TS_STATS_MAX:
            if (value > best)
            {
                best = value;
            }
            break;

        case TS_STATS_MIX:
            if (value < best)
            {
                best = value;
            }
            break;

        case TS_STATS_AVG:
        case TS_STATS_SUM:
            best += value;
            break;
        }
    }

    return type == TS_STATS_AVG ? best / n_threads : best;
}

inline int64_t poll_stats_get(int64_t POLL_STATS::*what, enum ts_stats_type type)
{
    return stats_get(pollStats, what, type);
}

inline int64_t queue_stats_get(int64_t QUEUE_STATS::*what, enum ts_stats_type type)
{
    return stats_get(queueStats, what, type);
}

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

    dcb_printf(dcb, "\nPoll Statistics.\n\n");
    dcb_printf(dcb, "No. of epoll cycles:                           %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_polls, TS_STATS_SUM));
    dcb_printf(dcb, "No. of epoll cycles with wait:                 %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::blockingpolls, TS_STATS_SUM));
    dcb_printf(dcb, "No. of epoll calls returning events:           %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_pollev, TS_STATS_SUM));
    dcb_printf(dcb, "No. of non-blocking calls returning events:    %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_nbpollev, TS_STATS_SUM));
    dcb_printf(dcb, "No. of read events:                            %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_read, TS_STATS_SUM));
    dcb_printf(dcb, "No. of write events:                           %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_write, TS_STATS_SUM));
    dcb_printf(dcb, "No. of error events:                           %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_error, TS_STATS_SUM));
    dcb_printf(dcb, "No. of hangup events:                          %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_hup, TS_STATS_SUM));
    dcb_printf(dcb, "No. of accept events:                          %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_accept, TS_STATS_SUM));
    dcb_printf(dcb, "Total event queue length:                      %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_length, TS_STATS_AVG));
    dcb_printf(dcb, "Average event queue length:                    %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_length, TS_STATS_AVG));
    dcb_printf(dcb, "Maximum event queue length:                    %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_max, TS_STATS_MAX));

    dcb_printf(dcb, "No of poll completions with descriptors\n");
    dcb_printf(dcb, "\tNo. of descriptors\tNo. of poll completions.\n");
    for (i = 0; i < MAXNFDS - 1; i++)
    {
        int64_t v = 0;
        for (int j = 0; j < n_threads; ++j)
        {
            v += pollStats[j].n_fds[i];
        }

        dcb_printf(dcb, "\t%2d\t\t\t%" PRId64 "\n", i + 1, v);
    }
    int64_t v = 0;
    for (int j = 0; j < n_threads; ++j)
    {
        v += pollStats[j].n_fds[MAXNFDS - 1];
    }
    dcb_printf(dcb, "\t>= %d\t\t\t%" PRId64 "\n", MAXNFDS, v);

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

    dcb_printf(dcb, " ID | State      \n");
    dcb_printf(dcb, "----+------------\n");
    for (int i = 0; i < n_threads; i++)
    {
        const char *state = "Unknown";

        switch (pollStats[i].thread_state)
        {
        case THREAD_STOPPED:
            state = "Stopped";
            break;
        case THREAD_IDLE:
            state = "Idle";
            break;
        case THREAD_POLLING:
            state = "Polling";
            break;
        case THREAD_PROCESSING:
            state = "Processing";
            break;
        case THREAD_ZPROCESSING:
            state = "Collecting";
            break;

        default:
            ss_dassert(!true);
        }

        dcb_printf(dcb, " %2d | %s\n", i, state);
    }
}

namespace
{

void get_queue_times(int index, int32_t* qtimes, int32_t* exectimes)
{
    int64_t q = 0;
    int64_t e = 0;

    for (int j = 0; j < n_threads; ++j)
    {
        q += queueStats[j].qtimes[index];
        e += queueStats[j].exectimes[index];
    }

    q /= n_threads;
    e /= n_threads;

    *qtimes = q;
    *exectimes = e;
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

    dcb_printf(pdcb, "\nEvent statistics.\n");
    dcb_printf(pdcb, "Maximum queue time:           %3" PRId64 "00ms\n",
               queue_stats_get(&QUEUE_STATS::maxqtime, TS_STATS_MAX));
    dcb_printf(pdcb, "Maximum execution time:       %3" PRId64 "00ms\n",
               queue_stats_get(&QUEUE_STATS::maxexectime, TS_STATS_MAX));
    dcb_printf(pdcb, "Maximum event queue length:   %3" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_max, TS_STATS_MAX));
    dcb_printf(pdcb, "Total event queue length:     %3" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_length, TS_STATS_SUM));
    dcb_printf(pdcb, "Average event queue length:   %3" PRId64 "\n",
               poll_stats_get(&POLL_STATS::evq_length, TS_STATS_AVG));
    dcb_printf(pdcb, "\n");
    dcb_printf(pdcb, "               |    Number of events\n");
    dcb_printf(pdcb, "Duration       | Queued     | Executed\n");
    dcb_printf(pdcb, "---------------+------------+-----------\n");

    int32_t qtimes;
    int32_t exectimes;

    get_queue_times(0, &qtimes, &exectimes);
    dcb_printf(pdcb, " < 100ms       | %-10d | %-10d\n", qtimes, exectimes);

    for (i = 1; i < N_QUEUE_TIMES; i++)
    {
        get_queue_times(i, &qtimes, &exectimes);
        dcb_printf(pdcb, " %2d00 - %2d00ms | %-10d | %-10d\n", i, i + 1, qtimes, exectimes);
    }

    get_queue_times(N_QUEUE_TIMES, &qtimes, &exectimes);
    dcb_printf(pdcb, " > %2d00ms      | %-10d | %-10d\n", N_QUEUE_TIMES, qtimes, exectimes);
}

/**
 * Return a poll statistic from the polling subsystem
 *
 * @param stat  The required statistic
 * @return      The value of that statistic
 */
int
poll_get_stat(POLL_STAT stat)
{
    switch (stat)
    {
    case POLL_STAT_READ:
        return poll_stats_get(&POLL_STATS::n_read, TS_STATS_SUM);
    case POLL_STAT_WRITE:
        return poll_stats_get(&POLL_STATS::n_write, TS_STATS_SUM);
    case POLL_STAT_ERROR:
        return poll_stats_get(&POLL_STATS::n_error, TS_STATS_SUM);
    case POLL_STAT_HANGUP:
        return poll_stats_get(&POLL_STATS::n_hup, TS_STATS_SUM);
    case POLL_STAT_ACCEPT:
        return poll_stats_get(&POLL_STATS::n_accept, TS_STATS_SUM);
    case POLL_STAT_EVQ_LEN:
        return poll_stats_get(&POLL_STATS::evq_length, TS_STATS_AVG);
    case POLL_STAT_EVQ_MAX:
        return poll_stats_get(&POLL_STATS::evq_max, TS_STATS_MAX);
    case POLL_STAT_MAX_QTIME:
        return queue_stats_get(&QUEUE_STATS::maxqtime, TS_STATS_MAX);
    case POLL_STAT_MAX_EXECTIME:
        return queue_stats_get(&QUEUE_STATS::maxexectime, TS_STATS_MAX);
    default:
        ss_dassert(false);
        break;
    }
    return 0;
}

/**
 * Provide a row to the result set that defines the event queue statistics
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
eventTimesRowCallback(RESULTSET *set, void *data)
{
    int *rowno = (int *)data;
    char buf[40];
    RESULT_ROW *row;

    if (*rowno >= N_QUEUE_TIMES)
    {
        MXS_FREE(data);
        return NULL;
    }
    row = resultset_make_row(set);
    if (*rowno == 0)
    {
        resultset_row_set(row, 0, "< 100ms");
    }
    else if (*rowno == N_QUEUE_TIMES - 1)
    {
        snprintf(buf, 39, "> %2d00ms", N_QUEUE_TIMES);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }
    else
    {
        snprintf(buf, 39, "%2d00 - %2d00ms", *rowno, (*rowno) + 1);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }
    int32_t qtimes;
    int32_t exectimes;
    get_queue_times(*rowno, &qtimes, &exectimes);
    snprintf(buf, 39, "%u", qtimes);
    buf[39] = '\0';
    resultset_row_set(row, 1, buf);
    snprintf(buf, 39, "%u", exectimes);
    buf[39] = '\0';
    resultset_row_set(row, 2, buf);
    (*rowno)++;
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
    int *data;

    if ((data = (int *)MXS_MALLOC(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
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

void poll_send_message(enum poll_message msg, void *data)
{
    spinlock_acquire(&poll_msg_lock);
    int nthr = config_threadcount();
    poll_msg_data = data;

    for (int i = 0; i < nthr; i++)
    {
        poll_msg[i] |= msg;
    }

    /** Handle this thread's message */
    poll_check_message();

    for (int i = 0; i < nthr; i++)
    {
        if (i != current_thread_id)
        {
            while (poll_msg[i] & msg)
            {
                thread_millisleep(1);
            }
        }
    }

    poll_msg_data = NULL;
    spinlock_release(&poll_msg_lock);
}

static void poll_check_message()
{
    int thread_id = current_thread_id;

    if (poll_msg[thread_id] & POLL_MSG_CLEAN_PERSISTENT)
    {
        SERVER *server = (SERVER*)poll_msg_data;
        dcb_persistent_clean_count(server->persistent[thread_id], thread_id, false);
        atomic_synchronize();
        poll_msg[thread_id] &= ~POLL_MSG_CLEAN_PERSISTENT;
    }
}
