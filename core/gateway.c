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
 * 27/06/13
 * 28/06/13 Vilho Raatikka      Added necessary headers, example functions and
 *                              calls to log manager and to query classifier.
 *                              Put example code behind SS_DEBUG macros.
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

#include <stdlib.h>
#include <mysql.h>

# include <skygw_utils.h>
# include <log_manager.h>

static char* server_options[] = {
    "SkySQL Gateway",
    "--datadir=/tmp/",
    "--skip-innodb",
    "--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] = {
    "embedded",
    "server",
    "server",
    "server",
    NULL
};



/* basic signal handling */
static void sighup_handler (int i) {
	fprintf(stderr, "Signal SIGHUP %i received ...\n", i);
}

static void sigterm_handler (int i) {
extern void shutdown_gateway();

	fprintf(stderr, "Signal SIGTERM %i received ...Exiting!\n", i);
	shutdown_gateway();
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

	return 0;
}

// main function
int
main(int argc, char **argv)
{
int		    daemon_mode = 1;
sigset_t	sigset;
int		    n, n_threads;
void		**threads;
char		buf[1024], *home, *cnf_file = NULL;
bool        failp;

#if defined(SS_DEBUG)
    int 	i;

	i = atexit(skygw_logmanager_exit);
    i = atexit(mysql_library_end);

	if (i != 0) {
		fprintf(stderr, "Couldn't register exit function.\n");
	}
#endif

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

	if (cnf_file == NULL) {
        skygw_log_write(
                NULL, 
                LOGFILE_ERROR,
                "Fatal : Unable to find a gateway configuration file, either "
                "install one in /etc/gateway.cnf, $GATEWAY_HOME/etc/gateway.cnf "
                "or use the -c option. Exiting.\n");
        exit(1);
	}
    
    failp = mysql_server_init(num_elements, server_options, server_groups);
    
    if (failp) {
        skygw_log_write_flush(
                NULL,
                LOGFILE_ERROR,
                "Fatal : mysql_server_init failed. It is mandatory component needed "
                "by router service and gateway can't continue without it. Exiting.\n"
                "%s : %d\n", __FILE__, __LINE__);
        exit(1);
    }
            
	if (!config_load(cnf_file))
	{
		skygw_log_write(NULL,
                        LOGFILE_ERROR,
                        "Failed to load gateway configuration file %s\n");
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
	 * Start the services that were created above
	 */
	printf("Started %d services\n", serviceStartAll());

	/*
	 * Start the polling threads, note this is one less than is configured as the
	 * main thread will also poll.
	 */
	n_threads = config_threadcount();
	threads = (void **)calloc(n_threads, sizeof(void *));
	for (n = 0; n < n_threads - 1; n++)
		threads[n] = thread_start(poll_waitevents, (void *)(n + 1));
	poll_waitevents((void *)0);
	for (n = 0; n < n_threads - 1; n++)
		thread_wait(threads[n]);

	printf("Gateway shutdown\n");

	return 0;
} // End of main

/**
 * Shutdown the gateway
 */
void
shutdown_gateway()
{
	poll_shutdown();
}
