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

#define         PROFILE_POLL    0

#if PROFILE_POLL
extern unsigned long hkheartbeat;
#endif

int number_poll_spins;
int max_poll_sleep;
static thread_local DCB* current_dcb;

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

/**
 * Control the use of mutexes for the epoll_wait call. Setting to 1 will
 * cause the epoll_wait calls to be moved under a mutex. This may be useful
 * for debugging purposes but should be avoided in general use.
 */
#define MUTEX_EPOLL     0

/** Fake epoll event struct */
typedef struct fake_event
{
    DCB               *dcb;   /*< The DCB where this event was generated */
    GWBUF             *data;  /*< Fake data, placed in the DCB's read queue */
    uint32_t           event; /*< The EPOLL event type */
    struct fake_event *tail;  /*< The last event */
    struct fake_event *next;  /*< The next event */
} fake_event_t;

thread_local int current_thread_id; /**< This thread's ID */
static int *epoll_fd;    /*< The epoll file descriptor */
static int next_epoll_fd = 0; /*< Which thread handles the next DCB */
static fake_event_t **fake_events; /*< Thread-specific fake event queue */
static SPINLOCK      *fake_event_lock;
static int do_shutdown = 0;  /*< Flag the shutdown of the poll subsystem */

/** Poll cross-thread messaging variables */
static volatile int     *poll_msg;
static void    *poll_msg_data = NULL;
static SPINLOCK poll_msg_lock = SPINLOCK_INIT;

#if MUTEX_EPOLL
static simple_mutex_t epoll_wait_mutex; /*< serializes calls to epoll_wait */
#endif
static int n_waiting = 0;    /*< No. of threads in epoll_wait */

static int process_pollq(int thread_id, struct epoll_event *event);
static void poll_add_event_to_dcb(DCB* dcb, GWBUF* buf, uint32_t ev);
static bool poll_dcb_session_check(DCB *dcb, const char *);
static void poll_check_message(void);

DCB *eventq = NULL;
SPINLOCK pollqlock = SPINLOCK_INIT;

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
 * Internal MaxScale thread states
 */
typedef enum
{
    THREAD_STOPPED,
    THREAD_IDLE,
    THREAD_POLLING,
    THREAD_PROCESSING,
    THREAD_ZPROCESSING
} THREAD_STATE;

/**
 * Thread data used to report the current state and activity related to
 * a thread
 */
typedef struct
{
    THREAD_STATE state; /*< Current thread state */
    int n_fds;          /*< No. of descriptors thread is processing */
    DCB *cur_dcb;       /*< Current DCB being processed */
    uint32_t event;     /*< Current event being processed */
    uint64_t cycle_start; /*< The time when the poll loop was started */
} THREAD_DATA;

static THREAD_DATA *thread_data = NULL;    /*< Status of each thread */

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

/**
 * The polling statistics
 */
static struct
{
    ts_stats_t *n_read;         /*< Number of read events   */
    ts_stats_t *n_write;        /*< Number of write events  */
    ts_stats_t *n_error;        /*< Number of error events  */
    ts_stats_t *n_hup;          /*< Number of hangup events */
    ts_stats_t *n_accept;       /*< Number of accept events */
    ts_stats_t *n_polls;        /*< Number of poll cycles   */
    ts_stats_t *n_pollev;       /*< Number of polls returning events */
    ts_stats_t *n_nbpollev;     /*< Number of polls returning events */
    ts_stats_t *n_nothreads;    /*< Number of times no threads are polling */
    int32_t n_fds[MAXNFDS];     /*< Number of wakeups with particular n_fds value */
    ts_stats_t *evq_length;     /*< Event queue length */
    ts_stats_t *evq_max;        /*< Maximum event queue length */
    ts_stats_t *blockingpolls;  /*< Number of epoll_waits with a timeout specified */
} pollStats;

#define N_QUEUE_TIMES   30
/**
 * The event queue statistics
 */
static struct
{
    uint32_t qtimes[N_QUEUE_TIMES + 1];
    uint32_t exectimes[N_QUEUE_TIMES + 1];
    ts_stats_t *maxqtime;
    ts_stats_t *maxexectime;
} queueStats;

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
 * Function to analyse error return from epoll_ctl
 */
static int poll_resolve_error(DCB *, int, bool);

/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
    n_threads = config_threadcount();

    if (!(epoll_fd = MXS_MALLOC(sizeof(int) * n_threads)))
    {
        return;
    }

    for (int i = 0; i < n_threads; i++)
    {
        if ((epoll_fd[i] = epoll_create(MAX_EVENTS)) == -1)
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_ERROR("FATAL: Could not create epoll instance: %s", strerror_r(errno, errbuf, sizeof(errbuf)));
            exit(-1);
        }
    }

    if ((fake_events = MXS_CALLOC(n_threads, sizeof(fake_event_t*))) == NULL)
    {
        exit(-1);
    }

    if ((fake_event_lock = MXS_CALLOC(n_threads, sizeof(SPINLOCK))) == NULL)
    {
        exit(-1);
    }

    if ((poll_msg = MXS_CALLOC(n_threads, sizeof(int))) == NULL)
    {
        exit(-1);
    }

    for (int i = 0; i < n_threads; i++)
    {
        spinlock_init(&fake_event_lock[i]);
    }

    memset(&pollStats, 0, sizeof(pollStats));
    memset(&queueStats, 0, sizeof(queueStats));
    thread_data = (THREAD_DATA *)MXS_MALLOC(n_threads * sizeof(THREAD_DATA));
    if (thread_data)
    {
        for (int i = 0; i < n_threads; i++)
        {
            thread_data[i].state = THREAD_STOPPED;
        }
    }

    if ((pollStats.n_read = ts_stats_alloc()) == NULL ||
        (pollStats.n_write = ts_stats_alloc()) == NULL ||
        (pollStats.n_error = ts_stats_alloc()) == NULL ||
        (pollStats.n_hup = ts_stats_alloc()) == NULL ||
        (pollStats.n_accept = ts_stats_alloc()) == NULL ||
        (pollStats.n_polls = ts_stats_alloc()) == NULL ||
        (pollStats.n_pollev = ts_stats_alloc()) == NULL ||
        (pollStats.n_nbpollev = ts_stats_alloc()) == NULL ||
        (pollStats.n_nothreads = ts_stats_alloc()) == NULL ||
        (pollStats.evq_length = ts_stats_alloc()) == NULL ||
        (pollStats.evq_max = ts_stats_alloc()) == NULL ||
        (queueStats.maxqtime = ts_stats_alloc()) == NULL ||
        (queueStats.maxexectime = ts_stats_alloc()) == NULL ||
        (pollStats.blockingpolls = ts_stats_alloc()) == NULL)
    {
        MXS_OOM_MESSAGE("FATAL: Could not allocate statistics data.");
        exit(-1);
    }

#if MUTEX_EPOLL
    simple_mutex_init(&epoll_wait_mutex, "epoll_wait_mutex");
#endif

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

int poll_add_dcb(DCB *dcb)
{
    int rc = -1;
    dcb_state_t old_state = dcb->state;
    dcb_state_t new_state;
    struct epoll_event ev;

    CHK_DCB(dcb);

#ifdef EPOLLRDHUP
    ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLET;
#else
    ev.events = EPOLLIN | EPOLLOUT | EPOLLHUP | EPOLLET;
#endif
    ev.data.ptr = dcb;

    /*<
     * Choose new state according to the role of dcb.
     */
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER || dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
    {
        new_state = DCB_STATE_POLLING;
    }
    else
    {
        ss_dassert(dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);
        new_state = DCB_STATE_LISTENING;
    }
    /*
     * Check DCB current state seems sensible
     */
    if (DCB_STATE_DISCONNECTED == dcb->state
        || DCB_STATE_ZOMBIE == dcb->state
        || DCB_STATE_UNDEFINED == dcb->state)
    {
        MXS_ERROR("%lu [poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this should be impossible, crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
        raise(SIGABRT);
    }
    if (DCB_STATE_POLLING == dcb->state
        || DCB_STATE_LISTENING == dcb->state)
    {
        MXS_ERROR("%lu [poll_add_dcb] Error : existing state of dcb %p "
                  "is %s, but this is probably an error, not crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    dcb->state = new_state;

    /*
     * The only possible failure that will not cause a crash is
     * running out of system resources.
     */
    int owner = 0;

    if (dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER)
    {
        owner = dcb->session->client_dcb->thread.id;
    }
    else
    {
        owner = (unsigned int)atomic_add(&next_epoll_fd, 1) % n_threads;
    }

    dcb->thread.id = owner;

    dcb_add_to_list(dcb);

    int error_num = 0;

    if (dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER)
    {
        /** Listeners are added to all epoll instances */
        int nthr = config_threadcount();

        for (int i = 0; i < nthr; i++)
        {
            if ((rc = epoll_ctl(epoll_fd[i], EPOLL_CTL_ADD, dcb->fd, &ev)))
            {
                error_num = errno;
                /** Remove the listener from the previous epoll instances */
                for (int j = 0; j < i; j++)
                {
                    epoll_ctl(epoll_fd[j], EPOLL_CTL_DEL, dcb->fd, &ev);
                }
                break;
            }
        }
    }
    else
    {
        if ((rc = epoll_ctl(epoll_fd[owner], EPOLL_CTL_ADD, dcb->fd, &ev)))
        {
            error_num = errno;
        }
    }

    if (rc)
    {
        /* Some errors are actually considered acceptable */
        rc = poll_resolve_error(dcb, error_num, true);
    }
    if (0 == rc)
    {
        MXS_DEBUG("%lu [poll_add_dcb] Added dcb %p in state %s to poll set.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    else
    {
        dcb->state = old_state;
    }
    return rc;
}

int poll_remove_dcb(DCB *dcb)
{
    int dcbfd, rc = 0;
    struct  epoll_event ev;
    CHK_DCB(dcb);

    /*< It is possible that dcb has already been removed from the set */
    if (dcb->state == DCB_STATE_NOPOLLING ||
        dcb->state == DCB_STATE_ZOMBIE)
    {
        return 0;
    }
    if (DCB_STATE_POLLING != dcb->state
        && DCB_STATE_LISTENING != dcb->state)
    {
        MXS_ERROR("%lu [poll_remove_dcb] Error : existing state of dcb %p "
                  "is %s, but this is probably an error, not crashing.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    /*<
     * Set state to NOPOLLING and remove dcb from poll set.
     */
    dcb->state = DCB_STATE_NOPOLLING;

    /**
     * Only positive fds can be removed from epoll set.
     * Cloned DCBs can have a state of DCB_STATE_POLLING but are not in
     * the epoll set and do not have a valid file descriptor.  Hence the
     * only action for them is already done - the change of state to
     * DCB_STATE_NOPOLLING.
     */
    dcbfd = dcb->fd;

    if (dcbfd > 0)
    {
        int error_num = 0;

        if (dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER)
        {
            /** Listeners are added to all epoll instances */
            int nthr = config_threadcount();

            for (int i = 0; i < nthr; i++)
            {
                int tmp_rc = epoll_ctl(epoll_fd[i], EPOLL_CTL_DEL, dcb->fd, &ev);
                if (tmp_rc && rc == 0)
                {
                    /** Even if one of the instances failed to remove it, try
                     * to remove it from all the others */
                    rc = tmp_rc;
                    error_num = errno;
                    ss_dassert(error_num);
                }
            }
        }
        else
        {
            if ((rc = epoll_ctl(epoll_fd[dcb->thread.id], EPOLL_CTL_DEL, dcbfd, &ev)))
            {
                error_num = errno;
            }
        }
        /**
         * The poll_resolve_error function will always
         * return 0 or crash.  So if it returns non-zero result,
         * things have gone wrong and we crash.
         */
        if (rc)
        {
            rc = poll_resolve_error(dcb, error_num, false);
        }
        if (rc)
        {
            raise(SIGABRT);
        }
    }
    return rc;
}

/**
 * Check error returns from epoll_ctl. Most result in a crash since they
 * are "impossible". Adding when already present is assumed non-fatal.
 * Likewise, removing when not present is assumed non-fatal.
 * It is assumed that callers to poll routines can handle the failure
 * that results from hitting system limit, although an error is written
 * here to record the problem.
 *
 * @param errornum      The errno set by epoll_ctl
 * @param adding        True for adding to poll list, false for removing
 * @return              -1 on error or 0 for possibly revised return code
 */
static int
poll_resolve_error(DCB *dcb, int errornum, bool adding)
{
    if (adding)
    {
        if (EEXIST == errornum)
        {
            MXS_ERROR("%lu [poll_resolve_error] Error : epoll_ctl could not add, "
                      "already exists for DCB %p.",
                      pthread_self(),
                      dcb);
            // Assume another thread added and no serious harm done
            return 0;
        }
        if (ENOSPC == errornum)
        {
            MXS_ERROR("%lu [poll_resolve_error] The limit imposed by "
                      "/proc/sys/fs/epoll/max_user_watches was "
                      "encountered while trying to register (EPOLL_CTL_ADD) a new "
                      "file descriptor on an epoll instance for dcb %p.",
                      pthread_self(),
                      dcb);
            /* Failure - assume handled by callers */
            return -1;
        }
    }
    else
    {
        /* Must be removing */
        if (ENOENT == errornum)
        {
            MXS_ERROR("%lu [poll_resolve_error] Error : epoll_ctl could not remove, "
                      "not found, for dcb %p.",
                      pthread_self(),
                      dcb);
            // Assume another thread removed and no serious harm done
            return 0;
        }
    }
    /* Common checks for add or remove - crash MaxScale */
    if (EBADF == errornum)
    {
        raise(SIGABRT);
    }
    if (EINVAL == errornum)
    {
        raise(SIGABRT);
    }
    if (ENOMEM == errornum)
    {
        raise(SIGABRT);
    }
    if (EPERM == errornum)
    {
        raise(SIGABRT);
    }
    /* Undocumented error number */
    raise(SIGABRT);
    /* The following statement should never be reached, but avoids compiler warning */
    return -1;
}

#define BLOCKINGPOLL 0  /*< Set BLOCKING POLL to 1 if using a single thread and to make
                         *  debugging easier.
                         */

/**
 * The main polling loop
 *
 * This routine does the polling and despatches of IO events
 * to the DCB's. It may be called either directly or as the entry point
 * of a polling thread within the gateway.
 *
 * The routine will loop as long as the variable "shutdown" is set to zero,
 * setting this to a non-zero value will cause the polling loop to return.
 *
 * There are two options for the polling, a debug option that is only useful if
 * you have a single thread. This blocks in epoll_wait until an event occurs.
 *
 * The non-debug option does an epoll with a time out. This allows the checking of
 * shutdown value to be checked in all threads. The algorithm for polling in this
 * mode is to do a poll with no-wait, if no events are detected then the poll is
 * repeated with a time out. This allows for a quick check before making the call
 * with timeout. The call with the timeout differs in that the Linux scheduler may
 * deschedule a process if a timeout is included, but will not do this if a 0 timeout
 * value is given. this improves performance when the gateway is under heavy load.
 *
 * In order to provide a fairer means of sharing the threads between the different
 * DCB's the poll mechanism has been decoupled from the processing of the events.
 * The events are now recieved via the epoll_wait call, a queue of DCB's that have
 * events pending is maintained and as new events arrive the DCB is added to the end
 * of this queue. If an eent arrives for a DCB alreayd in the queue, then the event
 * bits are added to the DCB but the DCB mantains the same point in the queue unless
 * the original events are already being processed. If they are being processed then
 * the DCB is moved to the back of the queue, this means that a DCB that is receiving
 * events at a high rate will not block the execution of events for other DCB's and
 * should result in a fairer polling strategy.
 *
 * The introduction of the ability to inject "fake" write events into the event queue meant
 * that there was a possibility to "starve" new events sicne the polling loop would
 * consume the event queue before looking for new events. If the DCB that inject
 * the fake event then injected another fake event as a result of the first it meant
 * that new events did not get added to the queue. The strategy has been updated to
 * not consume the entire event queue, but process one event before doing a non-blocking
 * call to add any new events before processing any more events. A blocking call to
 * collect events is only made if there are no pending events to be processed on the
 * event queue.
 *
 * Also introduced a "timeout bias" mechanism. This mechansim control the length of
 * of timeout passed to epoll_wait in blocking calls based on previous behaviour.
 * The initial call will block for 10% of the define timeout peroid, this will be
 * increased in increments of 10% until the full timeout value is used. If at any
 * point there is an event to be processed then the value will be reduced to 10% again
 * for the next blocking call.
 *
 * @param arg   The thread ID passed as a void * to satisfy the threading package
 */
void
poll_waitevents(void *arg)
{
    struct epoll_event events[MAX_EVENTS];
    int i, nfds, timeout_bias = 1;
    current_thread_id = (intptr_t)arg;
    int poll_spins = 0;

    int thread_id = current_thread_id;

    if (thread_data)
    {
        thread_data[thread_id].state = THREAD_IDLE;
    }

    while (1)
    {
        atomic_add(&n_waiting, 1);
#if BLOCKINGPOLL
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        atomic_add(&n_waiting, -1);
#else /* BLOCKINGPOLL */
#if MUTEX_EPOLL
        simple_mutex_lock(&epoll_wait_mutex, TRUE);
#endif
        if (thread_data)
        {
            thread_data[thread_id].state = THREAD_POLLING;
        }

        ts_stats_increment(pollStats.n_polls, thread_id);
        if ((nfds = epoll_wait(epoll_fd[thread_id], events, MAX_EVENTS, 0)) == -1)
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
            ts_stats_increment(pollStats.blockingpolls, thread_id);
            nfds = epoll_wait(epoll_fd[thread_id],
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
            ts_stats_increment(pollStats.n_nothreads, thread_id);
        }
#if MUTEX_EPOLL
        simple_mutex_unlock(&epoll_wait_mutex);
#endif
#endif /* BLOCKINGPOLL */
        if (nfds > 0)
        {
            ts_stats_set(pollStats.evq_length, nfds, thread_id);
            ts_stats_set_max(pollStats.evq_max, nfds, thread_id);

            timeout_bias = 1;
            if (poll_spins <= number_poll_spins + 1)
            {
                ts_stats_increment(pollStats.n_nbpollev, thread_id);
            }
            poll_spins = 0;
            MXS_DEBUG("%lu [poll_waitevents] epoll_wait found %d fds",
                      pthread_self(),
                      nfds);
            ts_stats_increment(pollStats.n_pollev, thread_id);
            if (thread_data)
            {
                thread_data[thread_id].n_fds = nfds;
                thread_data[thread_id].cur_dcb = NULL;
                thread_data[thread_id].event = 0;
                thread_data[thread_id].state = THREAD_PROCESSING;
            }

            pollStats.n_fds[(nfds < MAXNFDS ? (nfds - 1) : MAXNFDS - 1)]++;

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

        thread_data[thread_id].cycle_start = hkheartbeat;

        /* Process of the queue of waiting requests */
        for (int i = 0; i < nfds; i++)
        {
            process_pollq(thread_id, &events[i]);
        }

        fake_event_t *event = NULL;

        /** It is very likely that the queue is empty so to avoid hitting the
         * spinlock every time we receive events, we only do a dirty read. Currently,
         * only the monitors inject fake events from external threads. */
        if (fake_events[thread_id])
        {
            spinlock_acquire(&fake_event_lock[thread_id]);
            event = fake_events[thread_id];
            fake_events[thread_id] = NULL;
            spinlock_release(&fake_event_lock[thread_id]);
        }

        while (event)
        {
            struct epoll_event ev;
            event->dcb->dcb_fakequeue = event->data;
            ev.data.ptr = event->dcb;
            ev.events = event->event;
            process_pollq(thread_id, &ev);
            fake_event_t *tmp = event;
            event = event->next;
            MXS_FREE(tmp);
        }

        dcb_process_idle_sessions(thread_id);

        if (thread_data)
        {
            thread_data[thread_id].state = THREAD_ZPROCESSING;
        }

        /** Process closed DCBs */
        dcb_process_zombies(thread_id);

        poll_check_message();

        if (thread_data)
        {
            thread_data[thread_id].state = THREAD_IDLE;
        }

        if (do_shutdown)
        {
            /*<
             * Remove the thread from the bitmask of running
             * polling threads.
             */
            if (thread_data)
            {
                thread_data[thread_id].state = THREAD_STOPPED;
            }
            return;
        }
        if (thread_data)
        {
            thread_data[thread_id].state = THREAD_IDLE;
        }
    } /*< while(1) */
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
static int
process_pollq(int thread_id, struct epoll_event *event)
{
    uint32_t ev = event->events;
    DCB *dcb = event->data.ptr;
    ss_dassert(dcb->thread.id == thread_id || dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);
    current_dcb = dcb; // thread local

    /** Calculate event queue statistics */
    uint64_t started = hkheartbeat;
    uint64_t qtime = started - thread_data[thread_id].cycle_start;

    if (qtime > N_QUEUE_TIMES)
    {
        queueStats.qtimes[N_QUEUE_TIMES]++;
    }
    else
    {
        queueStats.qtimes[qtime]++;
    }

    ts_stats_set_max(queueStats.maxqtime, qtime, thread_id);

    CHK_DCB(dcb);
    if (thread_data)
    {
        thread_data[thread_id].state = THREAD_PROCESSING;
        thread_data[thread_id].cur_dcb = dcb;
        thread_data[thread_id].event = ev;
    }

    /* It isn't obvious that this is impossible */
    /* ss_dassert(dcb->state != DCB_STATE_DISCONNECTED); */
    if (DCB_STATE_DISCONNECTED == dcb->state)
    {
        return 0;
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
            ts_stats_increment(pollStats.n_write, thread_id);

            if (poll_dcb_session_check(dcb, "write_ready"))
            {
                dcb->func.write_ready(dcb);
            }
        }
        else
        {
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLOUT due %d, %s. "
                      "dcb %p, fd %i",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)),
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
            ts_stats_increment(pollStats.n_accept, thread_id);

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
            ts_stats_increment(pollStats.n_read, thread_id);

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
            char errbuf[MXS_STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLERR due %d, %s.",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        ts_stats_increment(pollStats.n_error, thread_id);

        if (poll_dcb_session_check(dcb, "error"))
        {
            dcb->func.error(dcb);
        }
    }

    if (ev & EPOLLHUP)
    {
        ss_debug(int eno = gw_getsockerrno(dcb->fd));
        ss_debug(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        ts_stats_increment(pollStats.n_hup, thread_id);
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
        ss_debug(char errbuf[MXS_STRERROR_BUFLEN]);
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        ts_stats_increment(pollStats.n_hup, thread_id);

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

    /** Calculate event execution statistics */
    qtime = hkheartbeat - started;

    if (qtime > N_QUEUE_TIMES)
    {
        queueStats.exectimes[N_QUEUE_TIMES]++;
    }
    else
    {
        queueStats.exectimes[qtime % N_QUEUE_TIMES]++;
    }

    ts_stats_set_max(queueStats.maxexectime, qtime, thread_id);

    current_dcb = NULL; // thread local

    return 1;
}

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
               ts_stats_get(pollStats.n_polls, TS_STATS_SUM));
    dcb_printf(dcb, "No. of epoll cycles with wait:                 %" PRId64 "\n",
               ts_stats_get(pollStats.blockingpolls, TS_STATS_SUM));
    dcb_printf(dcb, "No. of epoll calls returning events:           %" PRId64 "\n",
               ts_stats_get(pollStats.n_pollev, TS_STATS_SUM));
    dcb_printf(dcb, "No. of non-blocking calls returning events:    %" PRId64 "\n",
               ts_stats_get(pollStats.n_nbpollev, TS_STATS_SUM));
    dcb_printf(dcb, "No. of read events:                            %" PRId64 "\n",
               ts_stats_get(pollStats.n_read, TS_STATS_SUM));
    dcb_printf(dcb, "No. of write events:                           %" PRId64 "\n",
               ts_stats_get(pollStats.n_write, TS_STATS_SUM));
    dcb_printf(dcb, "No. of error events:                           %" PRId64 "\n",
               ts_stats_get(pollStats.n_error, TS_STATS_SUM));
    dcb_printf(dcb, "No. of hangup events:                          %" PRId64 "\n",
               ts_stats_get(pollStats.n_hup, TS_STATS_SUM));
    dcb_printf(dcb, "No. of accept events:                          %" PRId64 "\n",
               ts_stats_get(pollStats.n_accept, TS_STATS_SUM));
    dcb_printf(dcb, "No. of times no threads polling:               %" PRId64 "\n",
               ts_stats_get(pollStats.n_nothreads, TS_STATS_SUM));
    dcb_printf(dcb, "Total event queue length:                      %" PRId64 "\n",
               ts_stats_get(pollStats.evq_length, TS_STATS_AVG));
    dcb_printf(dcb, "Average event queue length:                    %" PRId64 "\n",
               ts_stats_get(pollStats.evq_length, TS_STATS_AVG));
    dcb_printf(dcb, "Maximum event queue length:                    %" PRId64 "\n",
               ts_stats_get(pollStats.evq_max, TS_STATS_MAX));

    dcb_printf(dcb, "No of poll completions with descriptors\n");
    dcb_printf(dcb, "\tNo. of descriptors\tNo. of poll completions.\n");
    for (i = 0; i < MAXNFDS - 1; i++)
    {
        dcb_printf(dcb, "\t%2d\t\t\t%" PRId32 "\n", i + 1, pollStats.n_fds[i]);
    }
    dcb_printf(dcb, "\t>= %d\t\t\t%" PRId32 "\n", MAXNFDS,
               pollStats.n_fds[MAXNFDS - 1]);

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

    str = MXS_MALLOC(22);       // 22 is max returned string length
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
    char *state = NULL;
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

    if (thread_data == NULL)
    {
        return;
    }
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
        else if (thread_data[i].cur_dcb == NULL)
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
                event_string = "??";
            }
            else
            {
                from_heap = true;
            }
            dcb_printf(dcb,
                       " %2d | %-10s | %6d | %-16p | <%3lu00ms | %s\n",
                       i, state, thread_data[i].n_fds,
                       thread_data[i].cur_dcb, 1 + hkheartbeat - dcb->evq.started,
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

void poll_add_epollin_event_to_dcb(DCB*   dcb,
                                   GWBUF* buf)
{
    __uint32_t ev;

    ev = EPOLLIN;

    poll_add_event_to_dcb(dcb, buf, ev);
}


static void poll_add_event_to_dcb(DCB*       dcb,
                                  GWBUF*     buf,
                                  uint32_t ev)
{
    fake_event_t *event = MXS_MALLOC(sizeof(*event));

    if (event)
    {
        event->data = buf;
        event->dcb = dcb;
        event->event = ev;
        event->next = NULL;
        event->tail = event;

        int thr = dcb->thread.id;

        /** It is possible that a housekeeper or a monitor thread inserts a fake
         * event into the thread's event queue which is why the operation needs
         * to be protected by a spinlock */
        spinlock_acquire(&fake_event_lock[thr]);

        if (fake_events[thr])
        {
            fake_events[thr]->tail->next = event;
            fake_events[thr]->tail = event;
        }
        else
        {
            fake_events[thr] = event;
        }

        spinlock_release(&fake_event_lock[thr]);
    }
}

void poll_fake_write_event(DCB *dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLOUT);
}

void poll_fake_read_event(DCB *dcb)
{
    poll_add_event_to_dcb(dcb, NULL, EPOLLIN);
}

void poll_fake_hangup_event(DCB *dcb)
{
#ifdef EPOLLRDHUP
    uint32_t ev = EPOLLRDHUP;
#else
    uint32_t ev = EPOLLHUP;
#endif
    poll_add_event_to_dcb(dcb, NULL, ev);
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
    dcb_printf(pdcb, "Maximum queue time:           %3" PRId64 "00ms\n", ts_stats_get(queueStats.maxqtime,
                                                                                      TS_STATS_MAX));
    dcb_printf(pdcb, "Maximum execution time:       %3" PRId64 "00ms\n", ts_stats_get(queueStats.maxexectime,
                                                                                      TS_STATS_MAX));
    dcb_printf(pdcb, "Maximum event queue length:   %3" PRId64 "\n", ts_stats_get(pollStats.evq_max,
                                                                                  TS_STATS_MAX));
    dcb_printf(pdcb, "Total event queue length:     %3" PRId64 "\n", ts_stats_get(pollStats.evq_length,
                                                                                  TS_STATS_SUM));
    dcb_printf(pdcb, "Average event queue length:   %3" PRId64 "\n", ts_stats_get(pollStats.evq_length,
                                                                                  TS_STATS_AVG));
    dcb_printf(pdcb, "\n");
    dcb_printf(pdcb, "               |    Number of events\n");
    dcb_printf(pdcb, "Duration       | Queued     | Executed\n");
    dcb_printf(pdcb, "---------------+------------+-----------\n");
    dcb_printf(pdcb, " < 100ms       | %-10d | %-10d\n",
               queueStats.qtimes[0], queueStats.exectimes[0]);
    for (i = 1; i < N_QUEUE_TIMES; i++)
    {
        dcb_printf(pdcb, " %2d00 - %2d00ms | %-10d | %-10d\n", i, i + 1,
                   queueStats.qtimes[i], queueStats.exectimes[i]);
    }
    dcb_printf(pdcb, " > %2d00ms      | %-10d | %-10d\n", N_QUEUE_TIMES,
               queueStats.qtimes[N_QUEUE_TIMES], queueStats.exectimes[N_QUEUE_TIMES]);
}

/**
 * Return a poll statistic from the polling subsystem
 *
 * @param stat  The required statistic
 * @return      The value of that statistic
 */
int64_t poll_get_stat(POLL_STAT stat)
{
    switch (stat)
    {
    case POLL_STAT_READ:
        return ts_stats_get(pollStats.n_read, TS_STATS_SUM);
    case POLL_STAT_WRITE:
        return ts_stats_get(pollStats.n_write, TS_STATS_SUM);
    case POLL_STAT_ERROR:
        return ts_stats_get(pollStats.n_error, TS_STATS_SUM);
    case POLL_STAT_HANGUP:
        return ts_stats_get(pollStats.n_hup, TS_STATS_SUM);
    case POLL_STAT_ACCEPT:
        return ts_stats_get(pollStats.n_accept, TS_STATS_SUM);
    case POLL_STAT_EVQ_LEN:
        return ts_stats_get(pollStats.evq_length, TS_STATS_AVG);
    case POLL_STAT_EVQ_MAX:
        return ts_stats_get(pollStats.evq_max, TS_STATS_MAX);
    case POLL_STAT_MAX_QTIME:
        return ts_stats_get(queueStats.maxqtime, TS_STATS_MAX);
    case POLL_STAT_MAX_EXECTIME:
        return ts_stats_get(queueStats.maxexectime, TS_STATS_MAX);
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
    snprintf(buf, 39, "%u", queueStats.qtimes[*rowno]);
    buf[39] = '\0';
    resultset_row_set(row, 1, buf);
    snprintf(buf, 39, "%u", queueStats.exectimes[*rowno]);
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

DCB* dcb_get_current()
{
    return current_dcb;
}
