/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 * Copyright SkySQL Ab 2013
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <errno.h>
#include <poll.h>
#include <dcb.h>
#include <atomic.h>
#include <gwbitmask.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <gw.h>
#include <config.h>
#include <housekeeper.h>

#define		PROFILE_POLL	1

#if PROFILE_POLL
#include <rdtsc.h>
#endif

extern int lm_enabled_logfiles_bitmask;

/**
 * @file poll.c  - Abstraction of the epoll functionality
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 19/06/13	Mark Riddoch	Initial implementation
 * 28/06/13	Mark Riddoch	Added poll mask support and DCB
 * 				zombie management
 * 29/08/14	Mark Riddoch	Addition of thread status data, load average
 *				etc.
 * 23/09/14	Mark Riddoch	Make use of RDHUP conditional to allow CentOS 5
 *				builds.
 * 24/09/14	Mark Riddoch	Introduction of the event queue for processing the
 *				incoming events rather than processing them immediately
 *				in the loop after the epoll_wait. This allows for better
 *				thread utilisaiton and fairer scheduling of the event
 *				processing.
 *
 * @endverbatim
 */

/**
 * Control the use of mutexes for the epoll_wait call. Setting to 1 will
 * cause the epoll_wait calls to be moved under a mutex. This may be useful
 * for debuggign purposes but should be avoided in general use.
 */
#define	MUTEX_EPOLL	0

static	int		epoll_fd = -1;	  /*< The epoll file descriptor */
static	int		do_shutdown = 0;  /*< Flag the shutdown of the poll subsystem */
static	GWBITMASK	poll_mask;
#if MUTEX_EPOLL
static  simple_mutex_t  epoll_wait_mutex; /*< serializes calls to epoll_wait */
#endif
static	int		n_waiting = 0;	  /*< No. of threads in epoll_wait */
static	int		process_pollq(int thread_id);


DCB		*eventq = NULL;
SPINLOCK	pollqlock = SPINLOCK_INIT;

/**
 * Thread load average, this is the average number of descriptors in each
 * poll completion, a value of 1 or less is the ideal.
 */
static double	load_average = 0.0;
static int	load_samples = 0;
static int	load_nfds = 0;
static double	current_avg = 0.0;
static double	*avg_samples = NULL;
static int	next_sample = 0;
static int	n_avg_samples;

/* Thread statistics data */
static	int		n_threads;	/*< No. of threads */

/**
 * Internal MaxScale thread states
 */
typedef enum { THREAD_STOPPED, THREAD_IDLE,
		THREAD_POLLING, THREAD_PROCESSING,
		THREAD_ZPROCESSING } THREAD_STATE;

/**
 * Thread data used to report the current state and activity related to
 * a thread
 */
typedef	struct {
	THREAD_STATE	state;	  /*< Current thread state */
	int		n_fds;	  /*< No. of descriptors thread is processing */
	DCB		*cur_dcb; /*< Current DCB being processed */
	uint32_t	event;	  /*< Current event being processed */
} THREAD_DATA;

static	THREAD_DATA	*thread_data = NULL;	/*< Status of each thread */

/**
 * The number of buckets used to gather statistics about how many
 * descriptors where processed on each epoll completion.
 *
 * An array of wakeup counts is created, with the number of descriptors used
 * to index that array. Each time a completion occurs the n_fds - 1 value is
 * used to index this array and increment the count held there.
 * If n_fds - 1 >= MAXFDS then the count at MAXFDS -1 is incremented.
 */
#define	MAXNFDS		10

/**
 * The polling statistics
 */
static struct {
	int	n_read;		/*< Number of read events   */
	int	n_write;	/*< Number of write events  */
	int	n_error;	/*< Number of error events  */
	int	n_hup;		/*< Number of hangup events */
	int	n_accept;	/*< Number of accept events */
	int	n_polls;	/*< Number of poll cycles   */
	int	n_nothreads;	/*< Number of times no threads are polling */
	int	n_fds[MAXNFDS];	/*< Number of wakeups with particular
				    n_fds value */
	int	evq_length;	/*< Event queue length */
	int	evq_max;	/*< Maximum event queue length */
} pollStats;

/**
 * How frequently to call the poll_loadav function used to monitor the load
 * average of the poll subsystem.
 */
#define	POLL_LOAD_FREQ	10
/**
 * Periodic function to collect load data for average calculations
 */
static void	poll_loadav(void *);

/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
int	i;

	if (epoll_fd != -1)
		return;
	if ((epoll_fd = epoll_create(MAX_EVENTS)) == -1)
	{
		perror("epoll_create");
		exit(-1);
	}
	memset(&pollStats, 0, sizeof(pollStats));
	bitmask_init(&poll_mask);
        n_threads = config_threadcount();
	if ((thread_data =
		(THREAD_DATA *)malloc(n_threads * sizeof(THREAD_DATA))) != NULL)
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
	avg_samples = (double *)malloc(sizeof(double *) * n_avg_samples);
	for (i = 0; i < n_avg_samples; i++)
		avg_samples[i] = 0.0;

}

/**
 * Add a DCB to the set of descriptors within the polling
 * environment.
 *
 * @param dcb	The descriptor to add to the poll
 * @return	-1 on error or 0 on success
 */
int
poll_add_dcb(DCB *dcb)
{
        int         rc = -1;
        dcb_state_t old_state = DCB_STATE_UNDEFINED;
        dcb_state_t new_state;
        struct	epoll_event	ev;

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
        if (dcb->dcb_role == DCB_ROLE_REQUEST_HANDLER) {
                new_state = DCB_STATE_POLLING;
        } else {
                ss_dassert(dcb->dcb_role == DCB_ROLE_SERVICE_LISTENER);
                new_state = DCB_STATE_LISTENING;
        }
        /*<
         * If dcb is in unexpected state, state change fails indicating that dcb
         * is not polling anymore.
         */
        if (dcb_set_state(dcb, new_state, &old_state)) {
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, dcb->fd, &ev);

                if (rc != 0) {
                        int eno = errno;
                        errno = 0;
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Adding dcb %p in state %s "
                                "to poll set failed. epoll_ctl failed due "
                                "%d, %s.",
                                dcb,
                                STRDCBSTATE(dcb->state),
                                eno,
                                strerror(eno))));
                } else {
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [poll_add_dcb] Added dcb %p in state %s to "
                                "poll set.",
                                pthread_self(),
                                dcb,
                                STRDCBSTATE(dcb->state))));
                }
                ss_dassert(rc == 0); /*< trap in debug */
        } else {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Unable to set new state for dcb %p "
                        "in state %s. Adding to poll set failed.",
                        dcb,
                        STRDCBSTATE(dcb->state))));
        }
        
	return rc; 
}

/**
 * Remove a descriptor from the set of descriptors within the
 * polling environment.
 * The state change command may fail because concurrent threads may call
 * dcb_set_state simultaneously and the conflict is prevented in dcb_set_state.
 *
 * @param dcb	The descriptor to remove
 * @return	-1 on error or 0 on success
 */
int
poll_remove_dcb(DCB *dcb)
{
        struct	epoll_event ev;
        int                 rc = -1;
        dcb_state_t         old_state = DCB_STATE_UNDEFINED;
        dcb_state_t         new_state = DCB_STATE_NOPOLLING;

        CHK_DCB(dcb);

        /*< It is possible that dcb has already been removed from the set */
        if (dcb->state != DCB_STATE_POLLING) {
                if (dcb->state == DCB_STATE_NOPOLLING ||
                    dcb->state == DCB_STATE_ZOMBIE)
                {
                        rc = 0;
                }
                goto return_rc;
        }
        
        /*<
         * Set state to NOPOLLING and remove dcb from poll set.
         */
        if (dcb_set_state(dcb, new_state, &old_state)) {
                rc = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, dcb->fd, &ev);

                if (rc != 0) {
                        int eno = errno;
                        errno = 0;
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : epoll_ctl failed due %d, %s.",
                                eno,
                                strerror(eno))));
                }
                ss_dassert(rc == 0); /*< trap in debug */
        }
        /*<
         * This call was redundant, but the end result is correct.
         */
        else if (old_state == new_state)
        {
                rc = 0;
                goto return_rc;
        }
        
        /*< Set bit for each maxscale thread */
        bitmask_copy(&dcb->memdata.bitmask, poll_bitmask()); 
        rc = 0;
return_rc:
        return rc;
}

#define	BLOCKINGPOLL	0	/*< Set BLOCKING POLL to 1 if using a single thread and to make
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
 * In order to provide a fairer means of sharign the threads between the different
 * DCB's the poll mechanism has been decoupled from the processing of the events.
 * The events are now recieved via the epoll_wait call, a queue of DCB's that have
 * events pending is maintained and as new events arrive the DCB is added to the end
 * of this queue. If an eent arrives for a DCB alreayd in the queue, then the event
 * bits are added to the DCB but the DCB mantains the same point inthe queue unless
 * the original events are already being processed. If they are being processed then
 * the DCB is moved to the back of the queue, this means that a DCB that is receiving
 * events at a high rate will not block the execution of events for other DCB's and
 * should result in a fairer polling strategy.
 *
 * @param arg	The thread ID passed as a void * to satisfy the threading package
 */
void
poll_waitevents(void *arg)
{
struct epoll_event events[MAX_EVENTS];
int		   i, nfds;
int		   thread_id = (int)arg;
DCB                *zombies = NULL;

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
		/* Process of the queue of waiting requests */
		while (process_pollq(thread_id))
		{
			if (thread_data)
				thread_data[thread_id].state = THREAD_ZPROCESSING;
			zombies = dcb_process_zombies(thread_id);
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
                
		if ((nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 0)) == -1)
		{
			atomic_add(&n_waiting, -1);
                        int eno = errno;
                        errno = 0;
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [poll_waitevents] epoll_wait returned "
                                "%d, errno %d",
                                pthread_self(),
                                nfds,
                                eno)));
		}
		else if (nfds == 0)
		{
			atomic_add(&n_waiting, 1);
			nfds = epoll_wait(epoll_fd,
                                                  events,
                                                  MAX_EVENTS,
                                                  EPOLL_TIMEOUT);
		}
		else
		{
			atomic_add(&n_waiting, -1);
		}

		if (n_waiting == 0)
			atomic_add(&pollStats.n_nothreads, 1);
#if MUTEX_EPOLL
                simple_mutex_unlock(&epoll_wait_mutex);
#endif
#endif /* BLOCKINGPOLL */
		if (nfds > 0)
		{
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [poll_waitevents] epoll_wait found %d fds",
                                pthread_self(),
                                nfds)));
			atomic_add(&pollStats.n_polls, 1);
			if (thread_data)
			{
				thread_data[thread_id].n_fds = nfds;
				thread_data[thread_id].cur_dcb = NULL;
				thread_data[thread_id].event = 0;
				thread_data[thread_id].state = THREAD_PROCESSING;
			}

			pollStats.n_fds[(nfds < MAXNFDS ? (nfds - 1) : MAXNFDS - 1)]++;

			load_average = (load_average * load_samples + nfds)
						/ (load_samples + 1);
			atomic_add(&load_samples, 1);
			atomic_add(&load_nfds, nfds);

			/*
			 * Process every DCB that has a new event and add
			 * it to the poll queue.
			 * If the DCB is currently beign processed then we
			 * or in the new eent bits to the pending event bits
			 * and leave it in the queue.
			 * If the DCB was not already in the queue then it was
			 * idle and is added to the queue to process after
			 * setting the eent bits.
			 */
			for (i = 0; i < nfds; i++)
			{
				DCB 	*dcb = (DCB *)events[i].data.ptr;
				__uint32_t	ev = events[i].events;

				spinlock_acquire(&pollqlock);
				if (DCB_POLL_BUSY(dcb))
				{
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
					if (pollStats.evq_length > pollStats.evq_max)
					{
						pollStats.evq_max = pollStats.evq_length;
					}
				}
				spinlock_release(&pollqlock);
			}
		}

		if (thread_data)
		{
			thread_data[thread_id].state = THREAD_ZPROCESSING;
		}
		zombies = dcb_process_zombies(thread_id);
                
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
 * @param thread_id	The thread ID of the calling thread
 * @return 		0 if no DCB's have been processed
 */
static int
process_pollq(int thread_id)
{
DCB		*dcb;
int		found = 0;
uint32_t	ev;

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
		do {
			dcb = dcb->evq.next;
		} while (dcb != eventq && dcb->evq.processing == 1);

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
		dcb->evq.pending_events = 0;
	}
	spinlock_release(&pollqlock);

	if (found == 0)
		return 0;


	CHK_DCB(dcb);
	if (thread_data)
	{
		thread_data[thread_id].state = THREAD_PROCESSING;
		thread_data[thread_id].cur_dcb = dcb;
		thread_data[thread_id].event = ev;
	}

#if defined(SS_DEBUG)
	if (dcb_fake_write_ev[dcb->fd] != 0) {
		LOGIF(LD, (skygw_log_write(
			LOGFILE_DEBUG,
			"%lu [poll_waitevents] "
			"Added fake events %d to ev %d.",
			pthread_self(),
			dcb_fake_write_ev[dcb->fd],
			ev)));
		ev |= dcb_fake_write_ev[dcb->fd];
		dcb_fake_write_ev[dcb->fd] = 0;
	}
#endif
	ss_debug(spinlock_acquire(&dcb->dcb_initlock);)
	ss_dassert(dcb->state != DCB_STATE_ALLOC);
	ss_dassert(dcb->state != DCB_STATE_DISCONNECTED);
	ss_dassert(dcb->state != DCB_STATE_FREED);
	ss_debug(spinlock_release(&dcb->dcb_initlock);)

	LOGIF(LD, (skygw_log_write(
		LOGFILE_DEBUG,
		"%lu [poll_waitevents] event %d dcb %p "
		"role %s",
		pthread_self(),
		ev,
		dcb,
		STRDCBROLE(dcb->dcb_role))));

	if (ev & EPOLLOUT)
	{
		int eno = 0;
		eno = gw_getsockerrno(dcb->fd);

		if (eno == 0)  {
#if MUTEX_BLOCK
			simple_mutex_lock(
				&dcb->dcb_write_lock,
				true);
			ss_info_dassert(
				!dcb->dcb_write_active,
				"Write already active");
			dcb->dcb_write_active = TRUE;
			atomic_add(
			&pollStats.n_write,
				1);
			dcb->func.write_ready(dcb);
			dcb->dcb_write_active = FALSE;
			simple_mutex_unlock(
				&dcb->dcb_write_lock);
#else
			atomic_add(&pollStats.n_write,
						1);
			dcb->func.write_ready(dcb);
#endif
		} else {
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [poll_waitevents] "
				"EPOLLOUT due %d, %s. "
				"dcb %p, fd %i",
				pthread_self(),
				eno,
				strerror(eno),
				dcb,
				dcb->fd)));
		}
	}
	if (ev & EPOLLIN)
	{
#if MUTEX_BLOCK
		simple_mutex_lock(&dcb->dcb_read_lock,
				  true);
		ss_info_dassert(!dcb->dcb_read_active,
				"Read already active");
		dcb->dcb_read_active = TRUE;
#endif
		
		if (dcb->state == DCB_STATE_LISTENING)
		{
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [poll_waitevents] "
				"Accept in fd %d",
				pthread_self(),
				dcb->fd)));
			atomic_add(
				&pollStats.n_accept, 1);
			dcb->func.accept(dcb);
		}
		else
		{
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [poll_waitevents] "
				"Read in dcb %p fd %d",
				pthread_self(),
				dcb,
				dcb->fd)));
			atomic_add(&pollStats.n_read, 1);
			dcb->func.read(dcb);
		}
#if MUTEX_BLOCK
		dcb->dcb_read_active = FALSE;
		simple_mutex_unlock(
			&dcb->dcb_read_lock);
#endif
	}
	if (ev & EPOLLERR)
	{
		int eno = gw_getsockerrno(dcb->fd);
#if defined(SS_DEBUG)
		if (eno == 0) {
			eno = dcb_fake_write_errno[dcb->fd];
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [poll_waitevents] "
				"Added fake errno %d. "
				"%s",
				pthread_self(),
				eno,
				strerror(eno))));
		}
		dcb_fake_write_errno[dcb->fd] = 0;
#endif
		if (eno != 0) {
			LOGIF(LD, (skygw_log_write(
				LOGFILE_DEBUG,
				"%lu [poll_waitevents] "
				"EPOLLERR due %d, %s.",
				pthread_self(),
				eno,
				strerror(eno))));
		}
		atomic_add(&pollStats.n_error, 1);
		dcb->func.error(dcb);
	}

	if (ev & EPOLLHUP)
	{
		int eno = 0;
		eno = gw_getsockerrno(dcb->fd);
		
		LOGIF(LD, (skygw_log_write(
			LOGFILE_DEBUG,
			"%lu [poll_waitevents] "
			"EPOLLHUP on dcb %p, fd %d. "
			"Errno %d, %s.",
			pthread_self(),
			dcb,
			dcb->fd,
			eno,
			strerror(eno))));
		atomic_add(&pollStats.n_hup, 1);
		spinlock_acquire(&dcb->dcb_initlock);
		if ((dcb->flags & DCBF_HUNG) == 0)
		{
			dcb->flags |= DCBF_HUNG;
			spinlock_release(&dcb->dcb_initlock);
			dcb->func.hangup(dcb);
		}
		else
			spinlock_release(&dcb->dcb_initlock);
	}

#ifdef EPOLLRDHUP
	if (ev & EPOLLRDHUP)
	{
		int eno = 0;
		eno = gw_getsockerrno(dcb->fd);
		
		LOGIF(LD, (skygw_log_write(
			LOGFILE_DEBUG,
			"%lu [poll_waitevents] "
			"EPOLLRDHUP on dcb %p, fd %d. "
			"Errno %d, %s.",
			pthread_self(),
			dcb,
			dcb->fd,
			eno,
			strerror(eno))));
		atomic_add(&pollStats.n_hup, 1);
		spinlock_acquire(&dcb->dcb_initlock);
		if ((dcb->flags & DCBF_HUNG) == 0)
		{
			dcb->flags |= DCBF_HUNG;
			spinlock_release(&dcb->dcb_initlock);
			dcb->func.hangup(dcb);
		}
		else
			spinlock_release(&dcb->dcb_initlock);
	}
#endif

	spinlock_acquire(&pollqlock);
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
	spinlock_release(&pollqlock);
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
 * Debug routine to print the polling statistics
 *
 * @param dcb	DCB to print to
 */
void
dprintPollStats(DCB *dcb)
{
int	i;

	dcb_printf(dcb, "Number of epoll cycles: 		%d\n",
							pollStats.n_polls);
	dcb_printf(dcb, "Number of read events:   		%d\n",
							pollStats.n_read);
	dcb_printf(dcb, "Number of write events: 		%d\n",
							pollStats.n_write);
	dcb_printf(dcb, "Number of error events: 		%d\n",
							pollStats.n_error);
	dcb_printf(dcb, "Number of hangup events:		%d\n",
							pollStats.n_hup);
	dcb_printf(dcb, "Number of accept events:		%d\n",
							pollStats.n_accept);
	dcb_printf(dcb, "Number of times no threads polling:	%d\n",
							pollStats.n_nothreads);
	dcb_printf(dcb, "Current event queue length:		%d\n",
							pollStats.evq_length);
	dcb_printf(dcb, "Maximum event queue length:		%d\n",
							pollStats.evq_max);

	dcb_printf(dcb, "No of poll completions with descriptors\n");
	dcb_printf(dcb, "\tNo. of descriptors\tNo. of poll completions.\n");
	for (i = 0; i < MAXNFDS - 1; i++)
	{
		dcb_printf(dcb, "\t%2d\t\t\t%d\n", i + 1, pollStats.n_fds[i]);
	}
	dcb_printf(dcb, "\t>= %d\t\t\t%d\n", MAXNFDS,
					pollStats.n_fds[MAXNFDS-1]);
}

/**
 * Convert an EPOLL event mask into a printable string
 *
 * @param	event	The event mask
 * @return	A string representation, the caller must free the string
 */
static char *
event_to_string(uint32_t event)
{
char	*str;

	str = malloc(22);	// 22 is max returned string length
	if (str == NULL)
		return NULL;
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
			strcat(str, "|");
		strcat(str, "ERR");
	}
	if (event & EPOLLHUP)
	{
		if (*str)
			strcat(str, "|");
		strcat(str, "HUP");
	}
#ifdef EPOLLRDHUP
	if (event & EPOLLRDHUP)
	{
		if (*str)
			strcat(str, "|");
		strcat(str, "RDHUP");
	}
#endif

	return str;
}

/**
 * Print the thread status for all the polling threads
 *
 * @param dcb	The DCB to send the thread status data
 */
void
dShowThreads(DCB *dcb)
{
int	i, j, n;
char	*state;
double	avg1 = 0.0, avg5 = 0.0, avg15 = 0.0;


	dcb_printf(dcb, "Polling Threads.\n\n");
	dcb_printf(dcb, "Historic Thread Load Average: %.2f.\n", load_average);
	dcb_printf(dcb, "Current Thread Load Average: %.2f.\n", current_avg);

	/* Average all the samples to get the 15 minute average */
	for (i = 0; i < n_avg_samples; i++)
		avg15 += avg_samples[i];
	avg15 = avg15 / n_avg_samples;

	/* Average the last third of the samples to get the 5 minute average */
	n = 5 * 60 / POLL_LOAD_FREQ;
	i = next_sample - (n + 1);
	if (i < 0)
		i += n_avg_samples;
	for (j = i; j < i + n; j++)
		avg5 += avg_samples[j % n_avg_samples];
	avg5 = (3 * avg5) / (n_avg_samples);

	/* Average the last 15th of the samples to get the 1 minute average */
	n =  60 / POLL_LOAD_FREQ;
	i = next_sample - (n + 1);
	if (i < 0)
		i += n_avg_samples;
	for (j = i; j < i + n; j++)
		avg1 += avg_samples[j % n_avg_samples];
	avg1 = (15 * avg1) / (n_avg_samples);

	dcb_printf(dcb, "15 Minute Average: %.2f, 5 Minute Average: %.2f, "
			"1 Minute Average: %.2f\n\n", avg15, avg5, avg1);

	if (thread_data == NULL)
		return;
	dcb_printf(dcb, " ID | State      | # fds  | Descriptor       | Event\n");
	dcb_printf(dcb, "----+------------+--------+------------------+---------------\n");
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
			dcb_printf(dcb,
				" %2d | %-10s |        |                  |\n",
				i, state);
		else if (thread_data[i].cur_dcb == NULL)
			dcb_printf(dcb,
				" %2d | %-10s | %6d |                  |\n",
				i, state, thread_data[i].n_fds);
		else
		{
			char *event_string
				= event_to_string(thread_data[i].event);
			if (event_string == NULL)
				event_string = "??";
			dcb_printf(dcb,
				" %2d | %-10s | %6d | %-16p | %s\n",
				i, state, thread_data[i].n_fds,
				thread_data[i].cur_dcb, event_string);
			free(event_string);
		}
	}
}

/**
 * The function used to calculate time based load data. This is called by the
 * housekeeper every POLL_LOAD_FREQ seconds.
 *
 * @param data		Argument required by the housekeeper but not used here
 */
static void
poll_loadav(void *data)
{
static	int	last_samples = 0, last_nfds = 0;
int		new_samples, new_nfds;

	new_samples = load_samples - last_samples;
	new_nfds = load_nfds - last_nfds;
	last_samples = load_samples;
	last_nfds = load_nfds;

	/* POLL_LOAD_FREQ average is... */
	if (new_samples)
		current_avg = new_nfds / new_samples;
	else
		current_avg = 0.0;
	avg_samples[next_sample] = current_avg;
	next_sample++;
	if (next_sample >= n_avg_samples)
		next_sample = 0;
}
