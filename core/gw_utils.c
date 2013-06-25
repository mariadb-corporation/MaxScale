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
 *
 * gw_utils.c	- A set if utility functions useful within the context
 * of the gateway.
 *
 * Revision History
 *
 * Date		Who			Description
 * 03-06-2013	Massimiliano Pinto	gateway utils
 * 12-06-2013	Massimiliano Pinto	gw_read_gwbuff
 *					with error detection
 *					and its handling
 *
 */

#include <gw.h>
#include <dcb.h>
#include <session.h>

///
// set ip address in sockect struct
///
void setipaddress(struct in_addr *a, char *p) {
	struct hostent *h = gethostbyname(p);
	if (h == NULL) {
		if ((a->s_addr = inet_addr(p)) == -1) {
			error("unknown or invalid address [%s]\n", p);
		}
	} else {
		memcpy(a, h->h_addr, h->h_length);
	}
}

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
		fprintf(stderr, "||| read_gwbuff called with 0 bytes, closing\n");
		if (dcb->session->backends) {
			(dcb->session->backends->func).error(dcb->session->backends);
		}
		dcb->func.error(dcb);
		return 1;
	}

	while (b > 0) {
		int bufsize = b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE;
		if ((buffer = gwbuf_alloc(bufsize)) == NULL) {
			/* Bad news, we have run out of memory */
			/* Error handling */
			if (dcb->session->backends) {
				(dcb->session->backends->func).error(dcb->session->backends);
			}
			(dcb->func).error(dcb);
			return 1;
		}

		GW_NOINTR_CALL(n = read(dcb->fd, GWBUF_DATA(buffer), bufsize); dcb->stats.n_reads++);

		if (n < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				fprintf(stderr, "Client connection %i: continue for %i, %s\n", dcb->fd, errno, strerror(errno));
				return 1;
			} else {
				fprintf(stderr, "Client connection %i error: %i, %s\n", dcb->fd, errno, strerror(errno));;
				if (dcb->session->backends) {
					(dcb->session->backends->func).error(dcb->session->backends);
				}
				(dcb->func).error(dcb);
				return 1;
			}
		}

		if (n == 0) {
			//  socket closed
			fprintf(stderr, "Client connection %i closed: %i, %s\n", dcb->fd, errno, strerror(errno));
			if (dcb->session->backends) {
				(dcb->session->backends->func).error(dcb->session->backends);
			}
			(dcb->func).error(dcb);
			return 1;
		}

		// append read data to the gwbuf
		*head = gwbuf_append(*head, buffer);

		// how many bytes left
		b -= n;
	}

	return 0;
}

