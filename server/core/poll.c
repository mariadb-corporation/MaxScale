/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */
 
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/epoll.h>
#include <errno.h>
#include <poll.h>
#include <dcb.h>
#include <atomic.h>
#include <gwbitmask.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gw.h>
#include <maxconfig.h>
#include <housekeeper.h>
#include <maxconfig.h>
#include <mysql.h>
#include <resultset.h>

#define         PROFILE_POLL    0

#if PROFILE_POLL
#include <rdtsc.h>
#include <memlog.h>

extern unsigned long hkheartbeat;
MEMLOG  *plog;
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
 *
 * @endverbatim
 */

/**
 * Control the use of mutexes for the epoll_wait call. Setting to 1 will
 * cause the epoll_wait calls to be moved under a mutex. This may be useful
 * for debugging purposes but should be avoided in general use.
 */
#define MUTEX_EPOLL     0

static int epoll_fd = -1;    /*< The epoll file descriptor */
static int do_shutdown = 0;  /*< Flag the shutdown of the poll subsystem */
static GWBITMASK poll_mask;
#if MUTEX_EPOLL
static simple_mutex_t epoll_wait_mutex; /*< serializes calls to epoll_wait */
#endif
static int n_waiting = 0;    /*< No. of threads in epoll_wait */

static int process_pollq(int thread_id);
static void poll_add_event_to_dcb(DCB* dcb, GWBUF* buf, __uint32_t ev);
static bool poll_dcb_session_check(DCB *dcb, const char *);

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
    int n_read;         /*< Number of read events   */
    int n_write;        /*< Number of write events  */
    int n_error;        /*< Number of error events  */
    int n_hup;          /*< Number of hangup events */
    int n_accept;       /*< Number of accept events */
    int n_polls;        /*< Number of poll cycles   */
    int n_pollev;       /*< Number of polls returning events */
    int n_nbpollev;     /*< Number of polls returning events */
    int n_nothreads;    /*< Number of times no threads are polling */
    int n_fds[MAXNFDS]; /*< Number of wakeups with particular n_fds value */
    int evq_length;     /*< Event queue length */
    int evq_pending;    /*< Number of pending descriptors in event queue */
    int evq_max;        /*< Maximum event queue length */
    int wake_evqpending;/*< Woken from epoll_wait with pending events in queue */
    int blockingpolls;  /*< Number of epoll_waits with a timeout specified */
} pollStats;

#define N_QUEUE_TIMES   30
/**
 * The event queue statistics
 */
static struct
{
    unsigned int qtimes[N_QUEUE_TIMES+1];
    unsigned int exectimes[N_QUEUE_TIMES+1];
    unsigned long maxqtime;
    unsigned long maxexectime;
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
    int i;

    if (epoll_fd != -1)
    {
        return;
    }
    if ((epoll_fd = epoll_create(MAX_EVENTS)) == -1)
    {
        perror("epoll_create");
        exit(-1);
    }
    memset(&pollStats, 0, sizeof(pollStats));
    memset(&queueStats, 0, sizeof(queueStats));
    bitmask_init(&poll_mask);
    n_threads = config_threadcount();
    if ((thread_data = (THREAD_DATA *)malloc(n_threads * sizeof(THREAD_DATA))) != NULL)
    {
        for (i = 0; i < n_threads; i++)
        {
            thread_data[i].state = THREAD_STOPPED;
        }
    }
#if MUTEX_EPOLL
    simple_mutex_init(&epoll_wait_mutex, "epoll_wait_mutex");
#endif

    hktask_add("Load Average", poll_loadav, NULL, POLL_LOAD_FREQ);
    n_avg_samples = 15 * 60 / POLL_LOAD_FREQ;
    avg_samples = (double *)malloc(sizeof(double) * n_avg_samples);
    for (i = 0; i < n_avg_samples; i++)
    {
        avg_samples[i] = 0.0;
    }
    evqp_samples = (int *)malloc(sizeof(int) * n_avg_samples);
    for (i = 0; i < n_avg_samples; i++)
    {
        evqp_samples[i] = 0.0;
    }

    number_poll_spins = config_nbpolls();
    max_poll_sleep = config_pollsleep();

#if PROFILE_POLL
    plog = memlog_create("EventQueueWaitTime", ML_LONG, 10000);
#endif
}

/**
 * Add a DCB to the set of descriptors within the polling
 * environment.
 *
 * @param dcb   The descriptor to add to the poll
 * @return      -1 on error or 0 on success
 */
int
poll_add_dcb(DCB *dcb)
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
    spinlock_acquire(&dcb->dcb_initlock);
    if (dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER)
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
    spinlock_release(&dcb->dcb_initlock);
    /*
     * The only possible failure that will not cause a crash is
     * running out of system resources.
     */
    rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dcb->fd, &ev);
    if (rc)
    {
        /* Some errors are actually considered acceptable */
        rc = poll_resolve_error(dcb, errno, true);
    }
    if (0 == rc)
    {
        MXS_DEBUG("%lu [poll_add_dcb] Added dcb %p in state %s to poll set.",
                  pthread_self(),
                  dcb,
                  STRDCBSTATE(dcb->state));
    }
    else dcb->state = old_state;
    return rc;
}

/**
 * Remove a descriptor from the set of descriptors within the
 * polling environment.
 *
 * @param dcb   The descriptor to remove
 * @return      -1 on error or 0 on success
 */
int
poll_remove_dcb(DCB *dcb)
{
    int dcbfd, rc = -1;
    struct  epoll_event ev;
    CHK_DCB(dcb);

    spinlock_acquire(&dcb->dcb_initlock);
    /*< It is possible that dcb has already been removed from the set */
    if (dcb->state == DCB_STATE_NOPOLLING ||
        dcb->state == DCB_STATE_ZOMBIE)
    {
        spinlock_release(&dcb->dcb_initlock);
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
    spinlock_release(&dcb->dcb_initlock);
    if (dcbfd > 0)
    {
        rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dcbfd, &ev);
        /**
         * The poll_resolve_error function will always
         * return 0 or crash.  So if it returns non-zero result,
         * things have gone wrong and we crash.
         */
        if (rc)
        {
            rc = poll_resolve_error(dcb, errno, false);
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
    intptr_t thread_id = (intptr_t)arg;
    int poll_spins = 0;

    /** Add this thread to the bitmask of running polling threads */
    bitmask_set(&poll_mask, thread_id);
    if (thread_data)
    {
        thread_data[thread_id].state = THREAD_IDLE;
    }

    /** Init mysql thread context for use with a mysql handle and a parser */
    mysql_thread_init();

    while (1)
    {
        if (pollStats.evq_pending == 0 && timeout_bias < 10)
        {
            timeout_bias++;
        }

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

        atomic_add(&pollStats.n_polls, 1);
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
        else if (nfds == 0 && pollStats.evq_pending == 0 && poll_spins++ > number_poll_spins)
        {
            atomic_add(&pollStats.blockingpolls, 1);
            nfds = epoll_wait(epoll_fd,
                              events,
                              MAX_EVENTS,
                              (max_poll_sleep * timeout_bias) / 10);
            if (nfds == 0 && pollStats.evq_pending)
            {
                atomic_add(&pollStats.wake_evqpending, 1);
                poll_spins = 0;
            }
        }
        else
        {
            atomic_add(&n_waiting, -1);
        }

        if (n_waiting == 0)
        {
            atomic_add(&pollStats.n_nothreads, 1);
        }
#if MUTEX_EPOLL
        simple_mutex_unlock(&epoll_wait_mutex);
#endif
#endif /* BLOCKINGPOLL */
        if (nfds > 0)
        {
            timeout_bias = 1;
            if (poll_spins <= number_poll_spins + 1)
            {
                atomic_add(&pollStats.n_nbpollev, 1);
            }
            poll_spins = 0;
            MXS_DEBUG("%lu [poll_waitevents] epoll_wait found %d fds",
                      pthread_self(),
                      nfds);
            atomic_add(&pollStats.n_pollev, 1);
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
            for (i = 0; i < nfds; i++)
            {
                DCB *dcb = (DCB *)events[i].data.ptr;
                __uint32_t ev = events[i].events;

                spinlock_acquire(&pollqlock);
                if (DCB_POLL_BUSY(dcb))
                {
                    if (dcb->evq.pending_events == 0)
                    {
                        pollStats.evq_pending++;
                        dcb->evq.inserted = hkheartbeat;
                    }
                    dcb->evq.pending_events |= ev;
                }
                else
                {
                    dcb->evq.pending_events = ev;
                    if (eventq)
                    {
                        dcb->evq.prev = eventq->evq.prev;
                        eventq->evq.prev->evq.next = dcb;
                        eventq->evq.prev = dcb;
                        dcb->evq.next = eventq;
                    }
                    else
                    {
                        eventq = dcb;
                        dcb->evq.prev = dcb;
                        dcb->evq.next = dcb;
                    }
                    pollStats.evq_length++;
                    pollStats.evq_pending++;
                    dcb->evq.inserted = hkheartbeat;
                    if (pollStats.evq_length > pollStats.evq_max)
                    {
                        pollStats.evq_max = pollStats.evq_length;
                    }
                }
                spinlock_release(&pollqlock);
            }
        }

        /*
         * Process of the queue of waiting requests
         * This is done without checking the evq_pending count as a
         * precautionary measure to avoid issues if the house keeping
         * of the count goes wrong.
         */
        if (process_pollq(thread_id))
        {
            timeout_bias = 1;
        }

        if (thread_data)
        {
            thread_data[thread_id].state = THREAD_ZPROCESSING;
        }
        dcb_process_zombies(thread_id);
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
            bitmask_clear(&poll_mask, thread_id);
            /** Release mysql thread context */
            mysql_thread_end();
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
process_pollq(int thread_id)
{
    DCB *dcb;
    int found = 0;
    uint32_t ev;
    unsigned long qtime;

    spinlock_acquire(&pollqlock);
    if (eventq == NULL)
    {
        /* Nothing to process */
        spinlock_release(&pollqlock);
        return 0;
    }
    dcb = eventq;
    if (dcb->evq.next == dcb->evq.prev && dcb->evq.processing == 0)
    {
        found = 1;
        dcb->evq.processing = 1;
    }
    else if (dcb->evq.next == dcb->evq.prev)
    {
        /* Only item in queue is being processed */
        spinlock_release(&pollqlock);
        return 0;
    }
    else
    {
        do
        {
            dcb = dcb->evq.next;
        }
        while (dcb != eventq && dcb->evq.processing == 1);

        if (dcb->evq.processing == 0)
        {
            /* Found DCB to process */
            dcb->evq.processing = 1;
            found = 1;
        }
    }
    if (found)
    {
        ev = dcb->evq.pending_events;
        dcb->evq.processing_events = ev;
        dcb->evq.pending_events = 0;
        pollStats.evq_pending--;
        ss_dassert(pollStats.evq_pending >= 0);
    }
    spinlock_release(&pollqlock);

    if (found == 0)
    {
        return 0;
    }

#if PROFILE_POLL
    memlog_log(plog, hkheartbeat - dcb->evq.inserted);
#endif
    qtime = hkheartbeat - dcb->evq.inserted;
    dcb->evq.started = hkheartbeat;

    if (qtime > N_QUEUE_TIMES)
    {
        queueStats.qtimes[N_QUEUE_TIMES]++;
    }
    else
    {
        queueStats.qtimes[qtime]++;
    }
    if (qtime > queueStats.maxqtime)
    {
        queueStats.maxqtime = qtime;
    }


    CHK_DCB(dcb);
    if (thread_data)
    {
        thread_data[thread_id].state = THREAD_PROCESSING;
        thread_data[thread_id].cur_dcb = dcb;
        thread_data[thread_id].event = ev;
    }

#if defined(FAKE_CODE)
    if (dcb_fake_write_ev[dcb->fd] != 0)
    {
        MXS_DEBUG("%lu [poll_waitevents] "
                  "Added fake events %d to ev %d.",
                  pthread_self(),
                  dcb_fake_write_ev[dcb->fd],
                  ev);
        ev |= dcb_fake_write_ev[dcb->fd];
        dcb_fake_write_ev[dcb->fd] = 0;
    }
#endif /* FAKE_CODE */
    ss_debug(spinlock_acquire(&dcb->dcb_initlock));
    ss_dassert(dcb->state != DCB_STATE_ALLOC);
    /* It isn't obvious that this is impossible */
    /* ss_dassert(dcb->state != DCB_STATE_DISCONNECTED); */
    if (DCB_STATE_DISCONNECTED == dcb->state)
    {
        return 0;
    }
    ss_debug(spinlock_release(&dcb->dcb_initlock));

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
            atomic_add(&pollStats.n_write, 1);
            /** Read session id to thread's local storage */
            dcb_get_ses_log_info(dcb,
                                 &mxs_log_tls.li_sesid,
                                 &mxs_log_tls.li_enabled_priorities);

            if (poll_dcb_session_check(dcb, "write_ready"))
            {
                dcb->func.write_ready(dcb);
            }
        }
        else
        {
            char errbuf[STRERROR_BUFLEN];
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
        if (dcb->state == DCB_STATE_LISTENING)
        {
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Accept in fd %d",
                      pthread_self(),
                      dcb->fd);
            atomic_add(&pollStats.n_accept, 1);
            dcb_get_ses_log_info(dcb,
                                 &mxs_log_tls.li_sesid,
                                 &mxs_log_tls.li_enabled_priorities);

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
            atomic_add(&pollStats.n_read, 1);
            /** Read session id to thread's local storage */
            dcb_get_ses_log_info(dcb,
                                 &mxs_log_tls.li_sesid,
                                 &mxs_log_tls.li_enabled_priorities);

            if (poll_dcb_session_check(dcb, "read"))
            {
                dcb->func.read(dcb);
            }
        }
    }
    if (ev & EPOLLERR)
    {
        int eno = gw_getsockerrno(dcb->fd);
#if defined(FAKE_CODE)
        if (eno == 0)
        {
            eno = dcb_fake_write_errno[dcb->fd];
            char errbuf[STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "Added fake errno %d. "
                      "%s",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        dcb_fake_write_errno[dcb->fd] = 0;
#endif /* FAKE_CODE */
        if (eno != 0)
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_DEBUG("%lu [poll_waitevents] "
                      "EPOLLERR due %d, %s.",
                      pthread_self(),
                      eno,
                      strerror_r(eno, errbuf, sizeof(errbuf)));
        }
        atomic_add(&pollStats.n_error, 1);
        /** Read session id to thread's local storage */
        dcb_get_ses_log_info(dcb,
                             &mxs_log_tls.li_sesid,
                             &mxs_log_tls.li_enabled_priorities);

        if (poll_dcb_session_check(dcb, "error"))
        {
            dcb->func.error(dcb);
        }
    }

    if (ev & EPOLLHUP)
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->fd);
        char errbuf[STRERROR_BUFLEN];
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        atomic_add(&pollStats.n_hup, 1);
        spinlock_acquire(&dcb->dcb_initlock);
        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;
            spinlock_release(&dcb->dcb_initlock);
            /** Read session id to thread's local storage */
            dcb_get_ses_log_info(dcb,
                                 &mxs_log_tls.li_sesid,
                                 &mxs_log_tls.li_enabled_priorities);

            if (poll_dcb_session_check(dcb, "hangup EPOLLHUP"))
            {
                dcb->func.hangup(dcb);
            }
        }
        else
        {
            spinlock_release(&dcb->dcb_initlock);
        }
    }

#ifdef EPOLLRDHUP
    if (ev & EPOLLRDHUP)
    {
        int eno = 0;
        eno = gw_getsockerrno(dcb->fd);
        char errbuf[STRERROR_BUFLEN];
        MXS_DEBUG("%lu [poll_waitevents] "
                  "EPOLLRDHUP on dcb %p, fd %d. "
                  "Errno %d, %s.",
                  pthread_self(),
                  dcb,
                  dcb->fd,
                  eno,
                  strerror_r(eno, errbuf, sizeof(errbuf)));
        atomic_add(&pollStats.n_hup, 1);
        spinlock_acquire(&dcb->dcb_initlock);
        if ((dcb->flags & DCBF_HUNG) == 0)
        {
            dcb->flags |= DCBF_HUNG;
            spinlock_release(&dcb->dcb_initlock);
            /** Read session id to thread's local storage */
            dcb_get_ses_log_info(dcb,
                                 &mxs_log_tls.li_sesid,
                                 &mxs_log_tls.li_enabled_priorities);

            if (poll_dcb_session_check(dcb, "hangup EPOLLRDHUP"))
            {
                dcb->func.hangup(dcb);
            }
        }
        else
        {
            spinlock_release(&dcb->dcb_initlock);
        }
    }
#endif
    qtime = hkheartbeat - dcb->evq.started;

    if (qtime > N_QUEUE_TIMES)
    {
        queueStats.exectimes[N_QUEUE_TIMES]++;
    }
    else
    {
        queueStats.exectimes[qtime % N_QUEUE_TIMES]++;
    }
    if (qtime > queueStats.maxexectime)
    {
        queueStats.maxexectime = qtime;
    }

    spinlock_acquire(&pollqlock);
    dcb->evq.processing_events = 0;

    if (dcb->evq.pending_events == 0)
    {
        /* No pending events so remove from the queue */
        if (dcb->evq.prev != dcb)
        {
            dcb->evq.prev->evq.next = dcb->evq.next;
            dcb->evq.next->evq.prev = dcb->evq.prev;
            if (eventq == dcb)
                eventq = dcb->evq.next;
        }
        else
        {
            eventq = NULL;
        }
        dcb->evq.next = NULL;
        dcb->evq.prev = NULL;
        pollStats.evq_length--;
    }
    else
    {
        /*
         * We have a pending event, move to the end of the queue
         * if there are any other DCB's in the queue.
         *
         * If we are the first item on the queue this is easy, we
         * just bump the eventq pointer.
         */
        if (dcb->evq.prev != dcb)
        {
            if (eventq == dcb)
                eventq = dcb->evq.next;
            else
            {
                dcb->evq.prev->evq.next = dcb->evq.next;
                dcb->evq.next->evq.prev = dcb->evq.prev;
                dcb->evq.prev = eventq->evq.prev;
                dcb->evq.next = eventq;
                eventq->evq.prev = dcb;
                dcb->evq.prev->evq.next = dcb;
            }
        }
    }
    dcb->evq.processing = 0;
    /** Reset session id from thread's local storage */
    mxs_log_tls.li_sesid = 0;
    spinlock_release(&pollqlock);

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
 * Return the bitmask of polling threads
 *
 * @return The bitmask of the running polling threads
 */
GWBITMASK *
poll_bitmask()
{
    return &poll_mask;
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
    dcb_printf(dcb, "No. of epoll cycles:                           %d\n",
               pollStats.n_polls);
    dcb_printf(dcb, "No. of epoll cycles with wait:                         %d\n",
               pollStats.blockingpolls);
    dcb_printf(dcb, "No. of epoll calls returning events:           %d\n",
               pollStats.n_pollev);
    dcb_printf(dcb, "No. of non-blocking calls returning events:    %d\n",
               pollStats.n_nbpollev);
    dcb_printf(dcb, "No. of read events:                            %d\n",
               pollStats.n_read);
    dcb_printf(dcb, "No. of write events:                           %d\n",
               pollStats.n_write);
    dcb_printf(dcb, "No. of error events:                           %d\n",
               pollStats.n_error);
    dcb_printf(dcb, "No. of hangup events:                          %d\n",
               pollStats.n_hup);
    dcb_printf(dcb, "No. of accept events:                          %d\n",
               pollStats.n_accept);
    dcb_printf(dcb, "No. of times no threads polling:               %d\n",
               pollStats.n_nothreads);
    dcb_printf(dcb, "Current event queue length:                    %d\n",
               pollStats.evq_length);
    dcb_printf(dcb, "Maximum event queue length:                    %d\n",
               pollStats.evq_max);
    dcb_printf(dcb, "No. of DCBs with pending events:               %d\n",
               pollStats.evq_pending);
    dcb_printf(dcb, "No. of wakeups with pending queue:             %d\n",
               pollStats.wake_evqpending);

    dcb_printf(dcb, "No of poll completions with descriptors\n");
    dcb_printf(dcb, "\tNo. of descriptors\tNo. of poll completions.\n");
    for (i = 0; i < MAXNFDS - 1; i++)
    {
        dcb_printf(dcb, "\t%2d\t\t\t%d\n", i + 1, pollStats.n_fds[i]);
    }
    dcb_printf(dcb, "\t>= %d\t\t\t%d\n", MAXNFDS,
               pollStats.n_fds[MAXNFDS-1]);

#if SPINLOCK_PROFILE
    dcb_printf(dcb, "Event queue lock statistics:\n");
    spinlock_stats(&pollqlock, spin_reporter, dcb);
#endif
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

    str = malloc(22);       // 22 is max returned string length
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
            strcat(str, "|");
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
    char *state;
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
                       " %2d | %-10s | %6d | %-16p | <%3d00ms | %s\n",
                       i, state, thread_data[i].n_fds,
                       thread_data[i].cur_dcb, 1 + hkheartbeat - dcb->evq.started,
                       event_string);

            if (from_heap)
            {
                free(event_string);
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
    evqp_samples[next_sample] = pollStats.evq_pending;
    next_sample++;
    if (next_sample >= n_avg_samples)
    {
        next_sample = 0;
    }
}

/**
 * Add given GWBUF to DCB's readqueue and add a pending EPOLLIN event for DCB.
 * The event pretends that there is something to read for the DCB. Actually
 * the incoming data is stored in the DCB's readqueue where it is read.
 *
 * @param dcb   DCB where the event and data are added
 * @param buf   GWBUF including the data
 *
 */
void poll_add_epollin_event_to_dcb(DCB*   dcb,
                                   GWBUF* buf)
{
    __uint32_t ev;

    ev = EPOLLIN;

    poll_add_event_to_dcb(dcb, buf, ev);
}


static void poll_add_event_to_dcb(DCB*       dcb,
                                  GWBUF*     buf,
                                  __uint32_t ev)
{
    /** Add buf to readqueue */
    spinlock_acquire(&dcb->authlock);
    dcb->dcb_readqueue = gwbuf_append(dcb->dcb_readqueue, buf);
    spinlock_release(&dcb->authlock);

    spinlock_acquire(&pollqlock);

    /** Set event to DCB */
    if (DCB_POLL_BUSY(dcb))
    {
        if (dcb->evq.pending_events == 0)
        {
            pollStats.evq_pending++;
        }
        dcb->evq.pending_events |= ev;
    }
    else
    {
        dcb->evq.pending_events = ev;
        /** Add DCB to eventqueue if it isn't already there */
        if (eventq)
        {
            dcb->evq.prev = eventq->evq.prev;
            eventq->evq.prev->evq.next = dcb;
            eventq->evq.prev = dcb;
            dcb->evq.next = eventq;
        }
        else
        {
            eventq = dcb;
            dcb->evq.prev = dcb;
            dcb->evq.next = dcb;
        }
        pollStats.evq_length++;
        pollStats.evq_pending++;

        if (pollStats.evq_length > pollStats.evq_max)
        {
            pollStats.evq_max = pollStats.evq_length;
        }
    }
    spinlock_release(&pollqlock);
}

/*
 * Insert a fake write completion event for a DCB into the polling
 * queue.
 *
 * This is used to trigger transmission activity on another DCB from
 * within the event processing routine of a DCB. or to allow a DCB
 * to defer some further output processing, to allow for other DCBs
 * to receive a slice of the processing time. Fake events are added
 * to the tail of the event queue, in the same way that real events
 * are, so maintain the "fairness" of processing.
 *
 * @param dcb   DCB to emulate an EPOLLOUT event for
 */
void
poll_fake_write_event(DCB *dcb)
{
    uint32_t ev = EPOLLOUT;

    spinlock_acquire(&pollqlock);
    /*
     * If the DCB is already on the queue, there are no pending events and
     * there are other events on the queue, then
     * take it off the queue. This stops the DCB hogging the threads.
     */
    if (DCB_POLL_BUSY(dcb) && dcb->evq.pending_events == 0 && dcb->evq.prev != dcb)
    {
        dcb->evq.prev->evq.next = dcb->evq.next;
        dcb->evq.next->evq.prev = dcb->evq.prev;
        if (eventq == dcb)
        {
            eventq = dcb->evq.next;
        }
        dcb->evq.next = NULL;
        dcb->evq.prev = NULL;
        pollStats.evq_length--;
    }

    if (DCB_POLL_BUSY(dcb))
    {
        if (dcb->evq.pending_events == 0)
        {
            pollStats.evq_pending++;
        }
        dcb->evq.pending_events |= ev;
    }
    else
    {
        dcb->evq.pending_events = ev;
        dcb->evq.inserted = hkheartbeat;
        if (eventq)
        {
            dcb->evq.prev = eventq->evq.prev;
            eventq->evq.prev->evq.next = dcb;
            eventq->evq.prev = dcb;
            dcb->evq.next = eventq;
        }
        else
        {
            eventq = dcb;
            dcb->evq.prev = dcb;
            dcb->evq.next = dcb;
        }
        pollStats.evq_length++;
        pollStats.evq_pending++;
        dcb->evq.inserted = hkheartbeat;
        if (pollStats.evq_length > pollStats.evq_max)
        {
            pollStats.evq_max = pollStats.evq_length;
        }
    }
    spinlock_release(&pollqlock);
}

/*
 * Insert a fake hangup event for a DCB into the polling queue.
 *
 * This is used when a monitor detects that a server is not responding.
 *
 * @param dcb   DCB to emulate an EPOLLOUT event for
 */
void
poll_fake_hangup_event(DCB *dcb)
{
#ifdef EPOLLRDHUP
    uint32_t ev = EPOLLRDHUP;
#else
    uint32_t ev = EPOLLHUP;
#endif

    spinlock_acquire(&pollqlock);
    if (DCB_POLL_BUSY(dcb))
    {
        if (dcb->evq.pending_events == 0)
        {
            pollStats.evq_pending++;
        }
        dcb->evq.pending_events |= ev;
    }
    else
    {
        dcb->evq.pending_events = ev;
        dcb->evq.inserted = hkheartbeat;
        if (eventq)
        {
            dcb->evq.prev = eventq->evq.prev;
            eventq->evq.prev->evq.next = dcb;
            eventq->evq.prev = dcb;
            dcb->evq.next = eventq;
        }
        else
        {
            eventq = dcb;
            dcb->evq.prev = dcb;
            dcb->evq.next = dcb;
        }
        pollStats.evq_length++;
        pollStats.evq_pending++;
        dcb->evq.inserted = hkheartbeat;
        if (pollStats.evq_length > pollStats.evq_max)
        {
            pollStats.evq_max = pollStats.evq_length;
        }
    }
    spinlock_release(&pollqlock);
}

/**
 * Print the event queue contents
 *
 * @param pdcb          The DCB to print the event queue to
 */
void
dShowEventQ(DCB *pdcb)
{
    DCB *dcb;
    char *tmp1, *tmp2;

    spinlock_acquire(&pollqlock);
    if (eventq == NULL)
    {
        /* Nothing to process */
        spinlock_release(&pollqlock);
        return;
    }
    dcb = eventq;
    dcb_printf(pdcb, "\nEvent Queue.\n");
    dcb_printf(pdcb, "%-16s | %-10s | %-18s | %s\n", "DCB", "Status", "Processing Events",
               "Pending Events");
    dcb_printf(pdcb, "-----------------+------------+--------------------+-------------------\n");
    do
    {
        dcb_printf(pdcb, "%-16p | %-10s | %-18s | %-18s\n", dcb,
                   dcb->evq.processing ? "Processing" : "Pending", 
                   (tmp1 = event_to_string(dcb->evq.processing_events)),
                   (tmp2 = event_to_string(dcb->evq.pending_events)));
        free(tmp1);
        free(tmp2);
        dcb = dcb->evq.next;
    }
    while (dcb != eventq);
    spinlock_release(&pollqlock);
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
    dcb_printf(pdcb, "Maximum queue time:           %3d00ms\n", queueStats.maxqtime);
    dcb_printf(pdcb, "Maximum execution time:               %3d00ms\n", queueStats.maxexectime);
    dcb_printf(pdcb, "Maximum event queue length:     %3d\n", pollStats.evq_max);
    dcb_printf(pdcb, "Current event queue length:     %3d\n", pollStats.evq_length);
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
int
poll_get_stat(POLL_STAT stat)
{
    switch (stat)
    {
    case POLL_STAT_READ:
        return pollStats.n_read;
    case POLL_STAT_WRITE:
        return pollStats.n_write;
    case POLL_STAT_ERROR:
        return pollStats.n_error;
    case POLL_STAT_HANGUP:
        return pollStats.n_hup;
    case POLL_STAT_ACCEPT:
        return pollStats.n_accept;
    case POLL_STAT_EVQ_LEN:
        return pollStats.evq_length;
    case POLL_STAT_EVQ_PENDING:
        return pollStats.evq_pending;
    case POLL_STAT_EVQ_MAX:
        return pollStats.evq_max;
    case POLL_STAT_MAX_QTIME:
        return (int)queueStats.maxqtime;
    case POLL_STAT_MAX_EXECTIME:
        return (int)queueStats.maxexectime;
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
        free(data);
        return NULL;
    }
    row = resultset_make_row(set);
    if (*rowno == 0)
    {
        resultset_row_set(row, 0, "< 100ms");
    }
    else if (*rowno == N_QUEUE_TIMES - 1)
    {
        snprintf(buf,39, "> %2d00ms", N_QUEUE_TIMES);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }
    else
    {
        snprintf(buf,39, "%2d00 - %2d00ms", *rowno, (*rowno) + 1);
        buf[39] = '\0';
        resultset_row_set(row, 0, buf);
    }
    snprintf(buf,39, "%u", queueStats.qtimes[*rowno]);
    buf[39] = '\0';
    resultset_row_set(row, 1, buf);
    snprintf(buf,39, "%u", queueStats.exectimes[*rowno]);
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

    if ((data = (int *)malloc(sizeof(int))) == NULL)
    {
        return NULL;
    }
    *data = 0;
    if ((set = resultset_create(eventTimesRowCallback, data)) == NULL)
    {
        free(data);
        return NULL;
    }
    resultset_add_column(set, "Duration", 20, COL_TYPE_VARCHAR);
    resultset_add_column(set, "No. Events Queued", 12, COL_TYPE_VARCHAR);
    resultset_add_column(set, "No. Events Executed", 12, COL_TYPE_VARCHAR);

    return set;
}
