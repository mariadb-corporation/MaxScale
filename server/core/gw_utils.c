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
 * @file gw_utils.c	- A set if utility functions useful within the context
 * of the gateway.
 *
 * Revision History
 *
 * Date		Who			Description
 * 03-06-2013	Massimiliano Pinto	gateway utils
 * 12-06-2013	Massimiliano Pinto	gw_read_gwbuff
 *					with error detection
 *					and its handling
 * 01-07-2013	Massimiliano Pinto	Removed session->backends
 *					from gw_read_gwbuff()
 * 25-09-2013	Massimiliano Pinto	setipaddress uses getaddrinfo
 *
 */

#include <gw.h>
#include <dcb.h>
#include <session.h>

SPINLOCK tmplock = SPINLOCK_INIT;

/**
 * Set IP address in socket structure in_addr
 *
 * @param a	Pointer to a struct in_addr into which the address is written
 * @param p	The hostname to lookup
 */
void
setipaddress(struct in_addr *a, char *p) {
#ifdef __USE_POSIX
	struct addrinfo *ai = NULL, hint;
	int    rc;
	struct sockaddr_in * res_addr;
	memset(&hint, 0, sizeof (hint));

	hint.ai_socktype = SOCK_STREAM;

	/*
	* This is for the listening socket, matching INADDR_ANY only for now.
	* For future specific addresses bind, a dedicated routine woulbd be better
	*/

	if (strcmp(p, "0.0.0.0") == 0) {
		hint.ai_flags = AI_PASSIVE;
		hint.ai_family = AF_UNSPEC;
		if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0) {
			fprintf(stderr, "getaddrinfo(%s) failed with %s", p, gai_strerror(rc));
		}
	} else {
		hint.ai_flags = AI_CANONNAME;
		hint.ai_family = AF_INET;

		if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0) {
			fprintf(stderr, "getaddrinfo(%s) failed with %s", p, gai_strerror(rc));
		}
	}

        /* take the first one */
	if (ai != NULL) {
		res_addr = (struct sockaddr_in *)(ai->ai_addr);
		memcpy(a, &res_addr->sin_addr, sizeof(struct in_addr));
	}

	freeaddrinfo(ai);
#else
	struct hostent *h;

        spinlock_acquire(&tmplock);
        h = gethostbyname(p);
        spinlock_release(&tmplock);
        
	if (h == NULL) {
		if ((a->s_addr = inet_addr(p)) == -1) {
			fprintf(stderr, "unknown or invalid address [%s]\n", p);
		}
	} else {
        	/* take the first one */
		memcpy(a, h->h_addr, h->h_length);
	}
#endif
}

/**
 * Daemonize the process by forking and putting the process into the
 * background.
 */
void gw_daemonize(void) {
	pid_t pid;

	pid = fork();

	if (pid < 0) {
		fprintf(stderr, "fork() error %s\n", strerror(errno));
		exit(1);
	}

	if (pid != 0) {
		// exit from main
		exit(0);
	}

	if (setsid() < 0) {
		fprintf(stderr, "setsid() error %s\n", strerror(errno));
		exit(1);
	}
}

/////////////////////////////////////////////////
// Read data from dcb and store it in the gwbuf
/////////////////////////////////////////////////
int gw_read_gwbuff(DCB *dcb, GWBUF **head, int b) {
	GWBUF *buffer = NULL;
	int n = -1;

	if (b <= 0) {
		//fprintf(stderr, "||| read_gwbuff called with 0 bytes for %i, closing\n", dcb->fd);
		dcb->func.close(dcb);
		return 1;
	}

	while (b > 0) {
		int bufsize = b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE;
		if ((buffer = gwbuf_alloc(bufsize)) == NULL) {
			/* Bad news, we have run out of memory */
			/* Error handling */
			(dcb->func).close(dcb);
			return 1;
		}

		GW_NOINTR_CALL(n = read(dcb->fd, GWBUF_DATA(buffer), bufsize); dcb->stats.n_reads++);

		if (n < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				fprintf(stderr, "Client connection %i: continue for %i, %s\n", dcb->fd, errno, strerror(errno));
				gwbuf_free(buffer);
				return 1;
			} else {
				fprintf(stderr, "Client connection %i error: %i, %s\n", dcb->fd, errno, strerror(errno));;
				gwbuf_free(buffer);
				(dcb->func).close(dcb);
				return 1;
			}
		}

		if (n == 0) {
			//  socket closed
			fprintf(stderr, "Client connection %i closed: %i, %s\n", dcb->fd, errno, strerror(errno));
			gwbuf_free(buffer);
			(dcb->func).close(dcb);
			return 1;
		}

		// append read data to the gwbuf
		*head = gwbuf_append(*head, buffer);

		// how many bytes left
		b -= n;
	}

	return 0;
}

