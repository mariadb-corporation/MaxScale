/*
 * This file is distributed as part of the SkySQL Gateway. It is free
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
 * 
 */

/*
 * Revision History
 *
 * Date		Who			Description
 * 23-05-2013	Massimiliano Pinto	epoll loop test
 * 12-06-2013	Mark Riddoch		Add the -p option to set the
 * 					listening port
 *					and bind addr is 0.0.0.0
 *
 */

#include <gw.h>
#include <dcb.h>
#include <session.h>

// epoll fd, global!
static int epollfd;

void myfree(void** pp) { free(*pp); *pp = NULL; }

/* basic signal handling */
static void sighup_handler (int i) {
	fprintf(stderr, "Signal SIGHUP %i received ...\n", i);
}

static void sigterm_handler (int i) {
	fprintf(stderr, "Signal SIGTERM %i received ...Exiting!\n", i);
	
	exit(0);
}

/* wrapper for sigaction */
static void signal_set (int sig, void (*handler)(int)) {
	static struct sigaction sigact;
	static int err;

	memset(&sigact, 0, sizeof(struct sigaction));
	sigact.sa_handler = handler;
	GW_NOINTR_CALL(err = sigaction(sig, &sigact, NULL));
	if (err < 0) {
		fprintf(stderr,"sigaction() error %s\n", strerror(errno));
		exit(1);
	}
}

int handle_event_errors(DCB *dcb, int event) {
	struct epoll_event  ed;
	MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);

	fprintf(stderr, "#### Handle error function for [%i] is [%s]\n", dcb->state, gw_dcb_state2string(dcb->state));

	if (dcb->state == DCB_STATE_DISCONNECTED) {
		fprintf(stderr, "#### Handle error function, session is %p\n", dcb->session);
		return 1;
	}

#ifdef GW_EVENT_DEBUG
	if (event != -1) {
		fprintf(stderr, ">>>>>> DCB state %i, Protocol State %i: event %i, %i\n", dcb->state, protocol->state, event & EPOLLERR, event & EPOLLHUP);
		if(event & EPOLLHUP)
			fprintf(stderr, "EPOLLHUP\n");

		if(event & EPOLLERR)
			fprintf(stderr, "EPOLLERR\n");

		if(event & EPOLLPRI)
			fprintf(stderr, "EPOLLPRI\n");
	}
#endif

	if (dcb->state != DCB_STATE_LISTENING) {
		if (epoll_ctl(epollfd, EPOLL_CTL_DEL, dcb->fd, &ed) == -1) {
				fprintf(stderr, "epoll_ctl_del: from events check failed to delete %i, [%i]:[%s]\n", dcb->fd, errno, strerror(errno));
		}

#ifdef GW_EVENT_DEBUG
		fprintf(stderr, "closing fd [%i]=[%i], from events\n", dcb->fd, protocol->fd);
#endif
		if (dcb->fd) {
			//fprintf(stderr, "Client protocol dcb->protocol %p\n", dcb->protocol);

			gw_mysql_close((MySQLProtocol **)&dcb->protocol);
			fprintf(stderr, "Client protocol dcb->protocol %p\n", dcb->protocol);

			dcb->state = DCB_STATE_DISCONNECTED;

/*			
			if (dcb->session->backends->protocol != NULL) {
				fprintf(stderr, "!!!!!! Backend still open! dcb %p\n", dcb->session->backends->protocol);
				gw_mysql_close((MySQLProtocol **)&dcb->session->backends->protocol);
			}
*/
		}
	}

	fprintf(stderr, "Return from error handling, dcb is %p\n", dcb);
	//free(dcb->session);
	dcb->state = DCB_STATE_FREED;

	fprintf(stderr, "#### Handle error function RETURN for [%i] is [%s]\n", dcb->state, gw_dcb_state2string(dcb->state));
	//free(dcb);

	return 1;
}

int handle_event_errors_backend(DCB *dcb, int event) {
	struct epoll_event ed;
	MySQLProtocol *protocol = DCB_PROTOCOL(dcb, MySQLProtocol);

	fprintf(stderr, "#### Handle Backend error function for %i\n", dcb->fd);

#ifdef GW_EVENT_DEBUG
	if (event != -1) {
		fprintf(stderr, ">>>>>> Backend DCB state %i, Protocol State %i: event %i, %i\n", dcb->state, dcb->proto_state, event & EPOLLERR, event & EPOLLHUP);
		if(event & EPOLLHUP)
			fprintf(stderr, "EPOLLHUP\n");

		if(event & EPOLLERR)
			fprintf(stderr, "EPOLLERR\n");

		if(event & EPOLLPRI)
			fprintf(stderr, "EPOLLPRI\n");
	}
#endif

	if (dcb->state != DCB_STATE_LISTENING) {
		if (epoll_ctl(epollfd, EPOLL_CTL_DEL, dcb->fd, &ed) == -1) {
				fprintf(stderr, "Backend epoll_ctl_del: from events check failed to delete %i, [%i]:[%s]\n", dcb->fd, errno, strerror(errno));
		}

#ifdef GW_EVENT_DEBUG
		fprintf(stderr, "Backend closing fd [%i]=%i, from events check\n", dcb->fd, protocol->fd);
#endif
		if (dcb->fd) {
			dcb->state = DCB_STATE_DISCONNECTED;
			fprintf(stderr, "Freeing backend MySQL conn %p, %p\n", dcb->protocol, &dcb->protocol);
			gw_mysql_close((MySQLProtocol **)&dcb->protocol);
			fprintf(stderr, "Freeing backend MySQL conn %p, %p\n", dcb->protocol, &dcb->protocol);
		}
	}
}

// main function
int main(int argc, char **argv) {
	int daemon_mode = 1;
	sigset_t sigset;
	struct epoll_event events[MAX_EVENTS];
	struct epoll_event ev;
	int nfds;
	int n;
	char *port = NULL;

	for (n = 0; n < argc; n++)
	{
		if (strncmp(argv[n], "-p", 2) == 0)
		{
			port = &argv[n][2];
		}
	}

	fprintf(stderr, "(C) SkySQL Ab 2013\n"); 

	if (sigfillset(&sigset) != 0) {
		fprintf(stderr, "sigfillset() error %s\n", strerror(errno));
		return 1;
	}

	if (sigdelset(&sigset, SIGHUP) != 0) {
		fprintf(stderr, "sigdelset(SIGHUP) error %s\n", strerror(errno));
	}

	if (sigdelset(&sigset, SIGTERM) != 0) {
		fprintf(stderr, "sigdelset(SIGTERM) error %s\n", strerror(errno));
	}

	if (sigprocmask(SIG_SETMASK, &sigset, NULL) != 0) {
		fprintf(stderr, "sigprocmask() error %s\n", strerror(errno));
	}
	
	signal_set(SIGHUP, sighup_handler);
	signal_set(SIGTERM, sigterm_handler);

	if (daemon_mode == 1) {
		gw_daemonize();
	}

	fprintf(stderr, "GATEWAY is starting, PID %i\n\n", getpid());

	fprintf(stderr, ">> GATEWAY log is /dev/stderr\n");

	epollfd = epoll_create(MAX_EVENTS);

	if (epollfd == -1) {
		perror("epoll_create");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, ">> GATEWAY epoll maxevents is %i\n", MAX_EVENTS);

	// listen to MySQL protocol
	/*
	1. create socket
	2. set reuse 
	3. set nonblock
	4. listen
	5. bind
	6. epoll add event
	*/
	MySQLListener(epollfd, port);

	// event loop for all the descriptors added via epoll_ctl
	while (1) {
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		//nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1000);
		if (nfds == -1 && (errno != EINTR)) {
			perror("GW: epoll_pwait ERROR");
			exit(EXIT_FAILURE);
		}

#ifdef GW_EVENT_DEBUG
		fprintf(stderr, "wake from epoll_wait, n. %i events\n", nfds);
#endif

		for (n = 0; n < nfds; ++n) {
			DCB *dcb = (DCB *) (events[n].data.ptr);


#ifdef GW_EVENT_DEBUG
			fprintf(stderr, "New event %i for socket %i is %i\n", n, dcb->fd, events[n].events);
			if (events[n].events & EPOLLIN)
				fprintf(stderr, "New event %i for socket %i is EPOLLIN\n", n, dcb->fd);
			if (events[n].events & EPOLLOUT)
				fprintf(stderr, "New event %i for socket %i is EPOLLOUT\n", n, dcb->fd);
			if (events[n].events & EPOLLPRI)
				fprintf(stderr, "New event %i for socket %i is EPOLLPRI\n", n, dcb->fd);
	
#endif
			if (events[n].events & (EPOLLERR | EPOLLHUP)) {
				//fprintf(stderr, "CALL the ERROR pointer\n");
				(dcb->func).error(dcb, events[n].events);
				//fprintf(stderr, "CALLED the ERROR pointer\n");

				// go to next event
				continue;
			}

			if (events[n].events & EPOLLIN) {
				// now checking the listening socket
				if (dcb->state == DCB_STATE_LISTENING) {
					(dcb->func).accept(dcb, epollfd);

				} else {
					//fprintf(stderr, "CALL the READ pointer\n");
					(dcb->func).read(dcb, epollfd);
					//fprintf(stderr, "CALLED the READ pointer\n");
				}
			}


			if (events[n].events & EPOLLOUT) {
				if (dcb->state != DCB_STATE_LISTENING) {
					//fprintf(stderr, "CALL the WRITE pointer\n");
					(dcb->func).write_ready(dcb, epollfd);
					//fprintf(stderr, ">>> CALLED the WRITE pointer\n");
				}
			}

		} // filedesc loop
	} // infinite loop
	
	// End of while (1)
} // End of main
