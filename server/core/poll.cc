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
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/platform.h>
#include <maxscale/query_classifier.h>
#include <maxscale/resultset.h>
#include <maxscale/server.h>
#include <maxscale/session.h>
#include <maxscale/statistics.h>
#include <maxscale/thread.h>
#include <maxscale/utils.h>

#include "maxscale/poll.h"
#include "maxscale/worker.hh"

#define         PROFILE_POLL    0

using maxscale::Worker;

#if PROFILE_POLL
extern unsigned long hkheartbeat;
#endif

int number_poll_spins;
int max_poll_sleep;

/**
 * @file poll.c  - Abstraction of the epoll functionality
 *
 * @verbatim
 * Revision History
 *
 * Date         Who             Description
 * 19/06/13     Mark Riddoch    Initial implementation
 * 28/06/13     Mark Riddoch    Added poll mask support and DCB
 *                              zombie management
 * 29/08/14     Mark Riddoch    Addition of thread status data, load average
 *                              etc.
 * 23/09/14     Mark Riddoch    Make use of RDHUP conditional to allow CentOS 5
 *                              builds.
 * 24/09/14     Mark Riddoch    Introduction of the event queue for processing the
 *                              incoming events rather than processing them immediately
 *                              in the loop after the epoll_wait. This allows for better
 *                              thread utilisation and fairer scheduling of the event
 *                              processing.
 * 07/07/15     Martin Brampton Simplified add and remove DCB, improve error handling.
 * 23/08/15     Martin Brampton Added test so only DCB with a session link can be added to the poll list
 * 07/02/16     Martin Brampton Added a small piece of SSL logic to EPOLLIN
 * 15/06/16     Martin Brampton Changed ts_stats_add to inline ts_stats_increment
 *
 * @endverbatim
 */

thread_local int current_thread_id; /**< This thread's ID */
static int next_epoll_fd = 0; /*< Which thread handles the next DCB */
static int do_shutdown = 0;  /*< Flag the shutdown of the poll subsystem */

/** Poll cross-thread messaging variables */
static volatile int     *poll_msg;
static void    *poll_msg_data = NULL;
static SPINLOCK poll_msg_lock = SPINLOCK_INIT;

static int n_waiting = 0;    /*< No. of threads in epoll_wait */

static void poll_check_message(void);
static bool poll_dcb_session_check(DCB *dcb, const char *function);

/**
 * Thread load average, this is the average number of descriptors in each
 * poll completion, a value of 1 or less is the ideal.
 */
static double load_average = 0.0;
static int load_samples = 0;
static int load_nfds = 0;
static double current_avg = 0.0;
static double *avg_samples = NULL;
static int *evqp_samples = NULL;
static int next_sample = 0;
static int n_avg_samples;

/* Thread statistics data */
static int n_threads;      /*< No. of threads */

/**
 * How frequently to call the poll_loadav function used to monitor the load
 * average of the poll subsystem.
 */
#define POLL_LOAD_FREQ 10
/**
 * Periodic function to collect load data for average calculations
 */
static void poll_loadav(void *);

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

    hktask_add("Load Average", poll_loadav, NULL, POLL_LOAD_FREQ);
    n_avg_samples = 15 * 60 / POLL_LOAD_FREQ;
    avg_samples = (double *)MXS_MALLOC(sizeof(double) * n_avg_samples);
    MXS_ABORT_IF_NULL(avg_samples);
    for (int i = 0; i < n_avg_samples; i++)
    {
        avg_samples[i] = 0.0;
    }
    evqp_samples = (int *)MXS_MALLOC(sizeof(int) * n_avg_samples);
    MXS_ABORT_IF_NULL(evqp_samples);
    for (int i = 0; i < n_avg_samples; i++)
    {
        evqp_samples[i] = 0.0;
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
 * @param thread_data      The thread data of the calling thread.
 * @param poll_stats       The polling stats of the calling thread.
 * @param queue_stats      The queue stats of the calling thread.
 * @param should_shutdown  Pointer to function returning true if the polling should
 *                         be terminated.
 * @param data             Data provided to the @c should_shutdown function.
 */
void poll_waitevents(int epoll_fd,
                     int thread_id,
                     THREAD_DATA* thread_data,
                     POLL_STATS* poll_stats,
                     QUEUE_STATS* queue_stats,
                     bool (*should_shutdown)(void* data),
                     void* data)
{
    current_thread_id = thread_id;

    struct epoll_event events[MAX_EVENTS];
    int i, nfds, timeout_bias = 1;
    int poll_spins = 0;

    thread_data->state = THREAD_IDLE;

    while (!should_shutdown(data))
    {
        atomic_add(&n_waiting, 1);
        thread_data->state = THREAD_POLLING;

        atomic_add_int64(&poll_stats->n_polls, 1);
        if ((nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 0)) == -1)
        {
            atomic_add(&n_waiting, -1);
            int eno = errno;
            errno = 0;
            MXS_DEBUG("%lu [poll_waitevents] epoll_wait returned "
                      "%d, errno %d",
                      pthread_self(),
                      nfds,
                      eno);
            atomic_add(&n_waiting, -1);
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
        else
        {
            atomic_add(&n_waiting, -1);
        }

        if (n_waiting == 0)
        {
            atomic_add_int64(&poll_stats->n_nothreads, 1);
        }

        if (nfds > 0)
        {
            poll_stats->evq_length = nfds;
            poll_stats->evq_max = nfds;

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

            thread_data->n_fds = nfds;
            thread_data->cur_data = NULL;
            thread_data->event = 0;
            thread_data->state = THREAD_PROCESSING;

            poll_stats->n_fds[(nfds < MAXNFDS ? (nfds - 1) : MAXNFDS - 1)]++;

            load_average = (load_average * load_samples + nfds) / (load_samples + 1);
            atomic_add(&load_samples, 1);
            atomic_add(&load_nfds, nfds);

            /*
             * Process every DCB that has a new event and add
             * it to the poll queue.
             * If the DCB is currently being processed then we
             * or in the new eent bits to the pending event bits
             * and leave it in the queue.
             * If the DCB was not already in the queue then it was
             * idle and is added to the queue to process after
             * setting the event bits.
             */
        }

        thread_data->cycle_start = hkheartbeat;

        /* Process of the queue of waiting requests */
        for (int i = 0; i < nfds; i++)
        {
            /** Calculate event queue statistics */
            int64_t started = hkheartbeat;
            int64_t qtime = started - thread_data->cycle_start;

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
            thread_data->cur_data = data;

            thread_data->event = events[i].events;
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

        thread_data->state = THREAD_ZPROCESSING;

        /** Process closed DCBs */
        dcb_process_zombies(thread_id);

        poll_check_message();

        thread_data->state = THREAD_IDLE;
    } /*< while(1) */

    thread_data->state = THREAD_STOPPED;
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
 * Process of the queue of DCB's that have outstanding events
 *
 * The first event on the queue will be chosen to be executed by this thread,
 * all other events will be left on the queue and may be picked up by other
 * threads. When the processing is complete the thread will take the DCB off the
 * queue if there are no pending events that have arrived since the thread started
 * to process the DCB. If there are pending events the DCB will be moved to the
 * back of the queue so that other DCB's will have a share of the threads to
 * execute events for them.
 *
 * Including session id to log entries depends on this function. Assumption is
 * that when maxscale thread starts processing of an event it processes one
 * and only one session until it returns from this function. Session id is
 * read to thread's local storage if LOG_MAY_BE_ENABLED(LOGFILE_TRACE) returns true
 * reset back to zero just before returning in LOG_IS_ENABLED(LOGFILE_TRACE) returns true.
 * Thread local storage (tls_log_info_t) follows thread and is accessed every
 * time log is written to particular log.
 *
 * @param thread_id     The thread ID of the calling thread
 * @return              0 if no DCB's have been processed
 */
static uint32_t
process_pollq_dcb(DCB *dcb, int thread_id, uint32_t ev)
{
    ss_dassert(dcb->poll.thread.id == thread_id || dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);

    CHK_DCB(dcb);

    uint32_t rc = MXS_POLL_NOP;

    /* It isn't obvious that this is impossible */
    /* ss_dassert(dcb->state != DCB_STATE_DISCONNECTED); */
    if (DCB_STATE_DISCONNECTED == dcb->state)
    {
        return rc;
    }

    MXS_DEBUG("%lu [poll_waitevents] event %d dcb %p "
              "role %s",
              pthread_self(),
              ev,
              dcb,
              STRDCBROLE(dcb->dcb_role));

    if (ev & EPOLLOUT)
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->fd);

        if (eno == 0)
        {
            rc |= MXS_POLL_WRITE;

            if (poll_dcb_session_check(dcb, "write_ready"))
            {
                dcb->func.write_ready(dcb);
            }
        }
        else
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLOUT due %d, %s. "
                      "dcb %p, fd %i",
                      pthread_self(),
                      eno,
                      mxs_strerror(eno),
                      dcb,
                      dcb->fd);
        }
    }
    if (ev & EPOLLIN)
    {
        if (dcb->state == DCB_STATE_LISTENING || dcb->state == DCB_STATE_WAITING)
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Accept in fd %d",
                      pthread_self(),
                      dcb->fd);
            rc |= MXS_POLL_ACCEPT;

            if (poll_dcb_session_check(dcb, "accept"))
            {
                dcb->func.accept(dcb);
            }
        }
        else
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Read in dcb %p fd %d",
                      pthread_self(),
                      dcb,
                      dcb->fd);
            rc |= MXS_POLL_READ;

            if (poll_dcb_session_check(dcb, "read"))
            {
                int return_code = 1;
                /** SSL authentication is still going on, we need to call dcb_accept_SSL
                 * until it return 1 for success or -1 for error */
                if (dcb->ssl_state == SSL_HANDSHAKE_REQUIRED)
                {
                    return_code = (DCB_ROLE_CLIENT_HANDLER == dcb->dcb_role) ?
                                  dcb_accept_SSL(dcb) :
                                  dcb_connect_SSL(dcb);
                }
                if (1 == return_code)
                {
                    dcb->func.read(dcb);
                }
            }
        }
    }
    if (ev & EPOLLERR)
    {
        int eno = gw_getsockerrno(dcb->fd);
        if (eno != 0)
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLERR due %d, %s.",
                      pthread_self(),
                      eno,
                      mxs_strerror(eno));
        }
        rc |= MXS_POLL_ERROR;

        if (poll_dcb_session_check(dcb, "error"))
        {
            dcb->func.error(dcb);
        }
    }

    if (ev & EPOLLHUP)
    {
        ss_debug(int eno = gw_getsockerrno(dcb->fd));
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  mxs_strerror(eno));

        rc |= MXS_POLL_HUP;

        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;

            if (poll_dcb_session_check(dcb, "hangup EPOLLHUP"))
            {
                dcb->func.hangup(dcb);
            }
        }
    }

#ifdef EPOLLRDHUP
    if (ev & EPOLLRDHUP)
    {
        ss_debug(int eno = gw_getsockerrno(dcb->fd));
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  mxs_strerror(eno));

        rc |= MXS_POLL_HUP;

        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;

            if (poll_dcb_session_check(dcb, "hangup EPOLLRDHUP"))
            {
                dcb->func.hangup(dcb);
            }
        }
    }
#endif
    return rc;
}
#ifdef CRAP
static uint32_t dcb_poll_handler(MXS_POLL_DATA *data, int wid, uint32_t events)
{
    uint32_t rc = process_pollq_dcb((DCB*)data, wid, events);

    // Since this loop is now here, it will be processed once per extracted epoll
    // event and not once per extraction of events, but as this is temporary code
    // that's ok. Once it'll be possible to send cross-thread messages, the need
    // for the fake event list will disappear.

    fake_event_t *event = NULL;

    /** It is very likely that the queue is empty so to avoid hitting the
     * spinlock every time we receive events, we only do a dirty read. Currently,
     * only the monitors inject fake events from external threads. */
    if (fake_events[wid])
    {
        spinlock_acquire(&fake_event_lock[wid]);
        event = fake_events[wid];
        fake_events[wid] = NULL;
        spinlock_release(&fake_event_lock[wid]);
    }

    while (event)
    {
        event->dcb->dcb_fakequeue = event->data;
        process_pollq_dcb(event->dcb, wid, event->event);
        fake_event_t *tmp = event;
        event = event->next;
        MXS_FREE(tmp);
    }

    return rc;
}
#endif
/**
 *
 * Check that the DCB has a session link before processing.
 * If not, log an error.  Processing will be bypassed
 *
 * @param   dcb         The DCB to check
 * @param   function    The name of the function about to be called
 * @return  bool        Does the DCB have a non-null session link
 */
static bool
poll_dcb_session_check(DCB *dcb, const char *function)
{
    if (dcb->session)
    {
        return true;
    }
    else
    {
        MXS_ERROR("%lu [%s] The dcb %p that was about to be processed by %s does not "
                  "have a non-null session pointer ",
                  pthread_self(),
                  __func__,
                  dcb,
                  function);
        return false;
    }
}

/**
 * Shutdown the polling loop
 */
void
poll_shutdown()
{
    do_shutdown = 1;
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
    dcb_printf(dcb, "No. of times no threads polling:               %" PRId64 "\n",
               poll_stats_get(&POLL_STATS::n_nothreads, TS_STATS_SUM));
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
 * Convert an EPOLL event mask into a printable string
 *
 * @param       event   The event mask
 * @return      A string representation, the caller must free the string
 */
static char *
event_to_string(uint32_t event)
{
    char *str;

    str = (char*)MXS_MALLOC(22);       // 22 is max returned string length
    if (str == NULL)
    {
        return NULL;
    }
    *str = 0;
    if (event & EPOLLIN)
    {
        strcat(str, "IN");
    }
    if (event & EPOLLOUT)
    {
        if (*str)
        {
            strcat(str, "|");
        }
        strcat(str, "OUT");
    }
    if (event & EPOLLERR)
    {
        if (*str)
        {
            strcat(str, "|");
        }
        strcat(str, "ERR");
    }
    if (event & EPOLLHUP)
    {
        if (*str)
        {
            strcat(str, "|");
        }
        strcat(str, "HUP");
    }
#ifdef EPOLLRDHUP
    if (event & EPOLLRDHUP)
    {
        if (*str)
        {
            strcat(str, "|");
        }
        strcat(str, "RDHUP");
    }
#endif

    return str;
}

/**
 * Print the thread status for all the polling threads
 *
 * @param dcb   The DCB to send the thread status data
 */
void
dShowThreads(DCB *dcb)
{
    int i, j, n;
    const char *state;
    double avg1 = 0.0, avg5 = 0.0, avg15 = 0.0;
    double qavg1 = 0.0, qavg5 = 0.0, qavg15 = 0.0;

    dcb_printf(dcb, "Polling Threads.\n\n");
    dcb_printf(dcb, "Historic Thread Load Average: %.2f.\n", load_average);
    dcb_printf(dcb, "Current Thread Load Average: %.2f.\n", current_avg);

    /* Average all the samples to get the 15 minute average */
    for (i = 0; i < n_avg_samples; i++)
    {
        avg15 += avg_samples[i];
        qavg15 += evqp_samples[i];
    }
    avg15 = avg15 / n_avg_samples;
    qavg15 = qavg15 / n_avg_samples;

    /* Average the last third of the samples to get the 5 minute average */
    n = 5 * 60 / POLL_LOAD_FREQ;
    i = next_sample - (n + 1);
    if (i < 0)
    {
        i += n_avg_samples;
    }
    for (j = i; j < i + n; j++)
    {
        avg5 += avg_samples[j % n_avg_samples];
        qavg5 += evqp_samples[j % n_avg_samples];
    }
    avg5 = (3 * avg5) / (n_avg_samples);
    qavg5 = (3 * qavg5) / (n_avg_samples);

    /* Average the last 15th of the samples to get the 1 minute average */
    n =  60 / POLL_LOAD_FREQ;
    i = next_sample - (n + 1);
    if (i < 0)
    {
        i += n_avg_samples;
    }
    for (j = i; j < i + n; j++)
    {
        avg1 += avg_samples[j % n_avg_samples];
        qavg1 += evqp_samples[j % n_avg_samples];
    }
    avg1 = (15 * avg1) / (n_avg_samples);
    qavg1 = (15 * qavg1) / (n_avg_samples);

    dcb_printf(dcb, "15 Minute Average: %.2f, 5 Minute Average: %.2f, "
               "1 Minute Average: %.2f\n\n", avg15, avg5, avg1);
    dcb_printf(dcb, "Pending event queue length averages:\n");
    dcb_printf(dcb, "15 Minute Average: %.2f, 5 Minute Average: %.2f, "
               "1 Minute Average: %.2f\n\n", qavg15, qavg5, qavg1);

    dcb_printf(dcb, " ID | State      | # fds  | Descriptor       | Running  | Event\n");
    dcb_printf(dcb, "----+------------+--------+------------------+----------+---------------\n");
    for (i = 0; i < n_threads; i++)
    {
        switch (thread_data[i].state)
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
        }
        if (thread_data[i].state != THREAD_PROCESSING)
        {
            dcb_printf(dcb,
                       " %2d | %-10s |        |                  |          |\n",
                       i, state);
        }
        else if (thread_data[i].cur_data == NULL)
        {
            dcb_printf(dcb,
                       " %2d | %-10s | %6d |                  |          |\n",
                       i, state, thread_data[i].n_fds);
        }
        else
        {
            char *event_string = event_to_string(thread_data[i].event);
            bool from_heap;

            if (event_string == NULL)
            {
                from_heap = false;
                event_string = (char*)"??";
            }
            else
            {
                from_heap = true;
            }
            dcb_printf(dcb,
                       " %2d | %-10s | %6d | %-16p | <%3lu00ms | %s\n",
                       i, state, thread_data[i].n_fds,
                       thread_data[i].cur_data, 1 + hkheartbeat - dcb->evq.started,
                       event_string);

            if (from_heap)
            {
                MXS_FREE(event_string);
            }
        }
    }
}

/**
 * The function used to calculate time based load data. This is called by the
 * housekeeper every POLL_LOAD_FREQ seconds.
 *
 * @param data          Argument required by the housekeeper but not used here
 */
static void
poll_loadav(void *data)
{
    static  int last_samples = 0, last_nfds = 0;
    int new_samples, new_nfds;

    new_samples = load_samples - last_samples;
    new_nfds = load_nfds - last_nfds;
    last_samples = load_samples;
    last_nfds = load_nfds;

    /* POLL_LOAD_FREQ average is... */
    if (new_samples)
    {
        current_avg = new_nfds / new_samples;
    }
    else
    {
        current_avg = 0.0;
    }
    avg_samples[next_sample] = current_avg;
    next_sample++;
    if (next_sample >= n_avg_samples)
    {
        next_sample = 0;
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
