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
 *
 * @endverbatim
 */

static	int		epoll_fd = -1;	  /*< The epoll file descriptor */
static	int		do_shutdown = 0;	  /*< Flag the shutdown of the poll subsystem */
static	GWBITMASK	poll_mask;
static  simple_mutex_t  epoll_wait_mutex; /*< serializes calls to epoll_wait */

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
} pollStats;


/**
 * Initialise the polling system we are using for the gateway.
 *
 * In this case we are using the Linux epoll mechanism
 */
void
poll_init()
{
	if (epoll_fd != -1)
		return;
	if ((epoll_fd = epoll_create(MAX_EVENTS)) == -1)
	{
		perror("epoll_create");
		exit(-1);
	}
	memset(&pollStats, 0, sizeof(pollStats));
	bitmask_init(&poll_mask);
        simple_mutex_init(&epoll_wait_mutex, "epoll_wait_mutex");        
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
        
	ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
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
 * @param arg	The thread ID passed as a void * to satisfy the threading package
 */
void
poll_waitevents(void *arg)
{
        struct epoll_event events[MAX_EVENTS];
        int		   i, nfds;
        int		   thread_id = (int)arg;
        bool               no_op = false;
        static bool        process_zombies_only = false; /*< flag for all threads */
        DCB                *zombies = NULL;

	/** Add this thread to the bitmask of running polling threads */
	bitmask_set(&poll_mask, thread_id);
	/** Init mysql thread context for use with a mysql handle and a parser */
	mysql_thread_init();
	
	while (1)
	{
#if BLOCKINGPOLL
		nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
#else /* BLOCKINGPOLL */
                if (!no_op) {
                        LOGIF(LD, (skygw_log_write(
                                           LOGFILE_DEBUG,
                                           "%lu [poll_waitevents] MaxScale thread "
                                           "%d > epoll_wait <",
                                           pthread_self(),
                                           thread_id)));                        
                        no_op = TRUE;
                }
#if 0
                simple_mutex_lock(&epoll_wait_mutex, TRUE);
#endif
                
		if ((nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 0)) == -1)
		{
                        int eno = errno;
                        errno = 0;
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [poll_waitevents] epoll_wait returned "
                                "%d, errno %d",
                                pthread_self(),
                                nfds,
                                eno)));
                        no_op = FALSE;
		}
		else if (nfds == 0)
		{
                        if (process_zombies_only) {
#if 0
                                simple_mutex_unlock(&epoll_wait_mutex);
#endif
                                goto process_zombies;
                        } else {
                                nfds = epoll_wait(epoll_fd,
                                                  events,
                                                  MAX_EVENTS,
                                                  EPOLL_TIMEOUT);
                                /*<
                                 * When there are zombies to be cleaned up but
                                 * no client requests, allow all threads to call
                                 * dcb_process_zombies without having to wait
                                 * for the timeout.
                                 */
                                if (nfds == 0 && dcb_get_zombies() != NULL)
                                {
                                        process_zombies_only = true;
                                }
                        }
		}
#if 0
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

			for (i = 0; i < nfds; i++)
			{
				DCB 		*dcb = (DCB *)events[i].data.ptr;
				__uint32_t	ev = events[i].events;

                                CHK_DCB(dcb);

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
                                        simple_mutex_lock(&dcb->dcb_read_lock,
                                                          true);
                                        ss_info_dassert(!dcb->dcb_read_active,
                                                        "Read already active");
                                        dcb->dcb_read_active = TRUE;
                                        
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
                                        dcb->dcb_read_active = FALSE;
                                        simple_mutex_unlock(
                                                &dcb->dcb_read_lock);
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
					dcb->func.hangup(dcb);
				}
			} /*< for */
                        no_op = FALSE;
		}
        process_zombies:
		zombies = dcb_process_zombies(thread_id);
                
                if (zombies == NULL) {
                        process_zombies_only = false;
                }

		if (do_shutdown)
		{
                        /*<
                         * Remove the thread from the bitmask of running
                         * polling threads.
                         */
			bitmask_clear(&poll_mask, thread_id);
			return;
		}
	} /*< while(1) */
	/** Release mysql thread context */
	mysql_thread_end();
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
	dcb_printf(dcb, "Number of epoll cycles: 	%d\n", pollStats.n_polls);
	dcb_printf(dcb, "Number of read events:   	%d\n", pollStats.n_read);
	dcb_printf(dcb, "Number of write events: 	%d\n", pollStats.n_write);
	dcb_printf(dcb, "Number of error events: 	%d\n", pollStats.n_error);
	dcb_printf(dcb, "Number of hangup events:	%d\n", pollStats.n_hup);
	dcb_printf(dcb, "Number of accept events:	%d\n", pollStats.n_accept);
}
