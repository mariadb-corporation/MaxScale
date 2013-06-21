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

/**
 * @file gateway.c - The gateway entry point.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 23-05-2013	Massimiliano Pinto	epoll loop test
 * 12-06-2013	Mark Riddoch		Add the -p option to set the
 * 					listening port
 *					and bind addr is 0.0.0.0
 * 19/06/13	Mark Riddoch		Extract the epoll functionality 
 * 21/06/13	Mark Riddoch		Added initial config support
 *
 * @endverbatim
 */

#include <gw.h>
#include <unistd.h>
#include <service.h>
#include <server.h>
#include <dcb.h>
#include <session.h>
#include <modules.h>
#include <config.h>
#include <poll.h>

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

int handle_event_errors(DCB *dcb) {
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
		if (poll_remove_dcb(dcb) == -1) {
				fprintf(stderr, "poll_remove_dcb: from events check failed to delete %i, [%i]:[%s]\n", dcb->fd, errno, strerror(errno));
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

int handle_event_errors_backend(DCB *dcb) {
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
		if (poll_remove_dcb(dcb) == -1) {
				fprintf(stderr, "Backend poll_remove_dcb: from events check failed to delete %i, [%i]:[%s]\n", dcb->fd, errno, strerror(errno));
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
int
main(int argc, char **argv)
{
int			daemon_mode = 1;
sigset_t		sigset;
int			n;
char			buf[1024], *home, *cnf_file = NULL;

	if ((home = getenv("GATEWAY_HOME")) != NULL)
	{
		sprintf(buf, "%s/etc/gateway.cnf", home);
		if (access(buf, R_OK) == 0)
			cnf_file = buf;
	}
	if (cnf_file == NULL && access("/etc/gateway.cnf", R_OK) == 0)
		cnf_file = "/etc/gateway.cnf";



	for (n = 0; n < argc; n++)
	{
		if (strcmp(argv[n], "-d") == 0)
		{
			// Debug mode
			daemon_mode = 0;
		}
		if (strncmp(argv[n], "-c", 2) == 0)
		{
			cnf_file = &argv[n][2];
		}
	}

	if (cnf_file == NULL)
	{
		fprintf(stderr, "Unable to find a gateway configuration file, either install one in\n");
		fprintf(stderr, "/etc/gateway.cnf, $GATEWAY_HOME/etc/gateway.cnf or use the -c option.\n");
		exit(1);
	}

	if (!load_config(cnf_file))
	{
		fprintf(stderr, "Failed to load gateway configuration file %s\n", cnf_file);
		exit(1);
	}

	fprintf(stderr, "SkySQL Gateway (C) SkySQL Ab 2013\n"); 

	if (daemon_mode == 1)
	{
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

		gw_daemonize();
	}

	fprintf(stderr, "GATEWAY is starting, PID %i\n\n", getpid());

	poll_init();

	/*
	 * Start the service that was created above
	 */
	printf("Started %d services\n", serviceStartAll());

	while (1)
	{
		poll();
	}
} // End of main
