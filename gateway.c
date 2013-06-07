/*
This file is distributed as part of the SkySQL Gateway. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Copyright SkySQL Ab

*/


/*
23-05-2013
epoll loop test
Massimiliano Pinto
*/

#include "gw.h"
#include "dcb.h"
#include "session.h"

// epoll fd, global!
static int epollfd;

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
	GW_LOOPED_CALL(err = sigaction(sig, &sigact, NULL));
	if (err < 0) {
		fprintf(stderr,"sigaction() error %s\n", strerror(errno));
		exit(1);
	}
}

int handle_event_errors(DCB *dcb, int event) {
	struct epoll_event  ed;

	fprintf(stderr, "#### Handle error function\n");
#ifdef GW_EVENT_DEBUG
	if (event != -1) {
		fprintf(stderr, ">>>>>> DCB state %i, Protocol State %i: event %i, %i\n", dcb->state, dcb->proto_state, event & EPOLLERR, event & EPOLLHUP);
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
		fprintf(stderr, "closing fd [%i], from events check and backend too [%i]\n", dcb->fd, &dcb->session->backends->fd);
#endif
		if (dcb->fd) {
			close (dcb->fd);

			if(dcb) {
				if (dcb->session) {
					if (dcb->session->backends) {
						gw_mysql_close((MySQLProtocol **)&dcb->session->backends->protocol);
					}
				}
				free(dcb);
			}
		}
	}
}

int handle_event_errors_backend(DCB *dcb, int event) {
	struct epoll_event  ed;

	fprintf(stderr, "#### Handle Backend error function\n");

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
		fprintf(stderr, "Backend closing fd [%i], from events check and backend too [%i]\n", dcb->fd);
#endif
		if (dcb->fd) {
			close (dcb->fd);

			if(dcb) {
				free(dcb);
			}
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
	MySQLListener(epollfd, NULL);

	// event loop for all the descriptors added via epoll_ctl
	while (1) {
		nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
		//nfds = epoll_wait(epollfd, events, MAX_EVENTS, 1000);
		if (nfds == -1 && (errno != EINTR)) {
			perror("GW: epoll_pwait ERROR");
			exit(EXIT_FAILURE);
		}

		fprintf(stderr, "wake from epoll_wait, n. %i events\n", nfds);

		for (n = 0; n < nfds; ++n) {
			DCB *dcb = (DCB *) (events[n].data.ptr);


			fprintf(stderr, "New event %i for socket %i is %i\n", n, dcb->fd, events[n].events);
			if (events[n].events & EPOLLIN)
				fprintf(stderr, "New event %i for socket %i is EPOLLIN\n", n, dcb->fd);
			if (events[n].events & EPOLLOUT)
				fprintf(stderr, "New event %i for socket %i is EPOLLOUT\n", n, dcb->fd);
			if (events[n].events & EPOLLPRI)
				fprintf(stderr, "New event %i for socket %i is EPOLLPRI\n", n, dcb->fd);
	
			if ((events[n].events & EPOLLIN) || (events[n].events & EPOLLPRI)) {
				// now checking the listening socket
				if (dcb->state == DCB_STATE_LISTENING) {
					(dcb->func).accept(dcb, epollfd);

				} else {
					// all the other filedesc here: clients and backends too!
					//protcocol based read and write operations
					(dcb->func).read(dcb, epollfd);
				}
				
			}


			if (events[n].events & EPOLLOUT) {
				if (dcb->state != DCB_STATE_LISTENING) {
					(dcb->func).write(dcb, epollfd);
				}
			}

			if (events[n].events & (EPOLLERR | EPOLLHUP)) {
				(dcb->func).error(dcb, events[n].events);
			}

		} // filedesc loop
	} // infinite loop
	
	// End of while (1)
} // End of main
