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

int gw_daemonize(void) {
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

//////////////////////////////////////
// Generic read from filedescriptor
//////////////////////////////////////
int do_read_fd(int fd) {
	char buf[MAX_BUFFER_SIZE]="";
	int n;

	n = read(fd, buf, MAX_BUFFER_SIZE);

	if (n == 0) {
		fprintf(stderr, "Generic Socket EOF\n");
		return -1;
	}

	if (n < 0) {
		fprintf(stderr, "Read error: %i [%s]\n", errno, strerror(errno));
		if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
			return -1;
		else
			return 0;
	}

#ifdef DEBUG_GATEWAY_READ
	fprintf(stderr, "socket %i, do_read %i bytes\n", fd, n);
#endif

	return n;
}

int do_read_dcb10(DCB *dcb) {
	char buf[MAX_BUFFER_SIZE]="";
	int n;
	n = read(dcb->fd, buf, 10);

	if (n == 0) {
		fprintf(stderr, "Read DCB Socket EOF\n");
		return -1;
	}

	if (n < 0) {
		fprintf(stderr, "ReadDcb error: %i [%s]\n", errno, strerror(errno));
		if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
			return -1;
		else
			return 0;
	}

#ifdef GW_READ_DEBUG
	fprintf(stderr, "socket %i, do_read % i bytes, [%s]\n", dcb->fd, n, buf+5);
#endif

	return n;
}

int do_read_dcb(DCB *dcb) {
	char buf[MAX_BUFFER_SIZE]="";
	int n;
	n = read(dcb->fd, buf, MAX_BUFFER_SIZE);

	if (n == 0) {
		fprintf(stderr, "Read DCB Socket EOF\n");
		return -1;
	}

	if (n < 0) {
		fprintf(stderr, "ReadDcb error: %i [%s]\n", errno, strerror(errno));
		if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
			return -1;
		else
			return 0;
	}

#ifdef GW_READ_DEBUG
	fprintf(stderr, "socket %i, do_read % i bytes, [%s]\n", dcb->fd, n, buf+5);
#endif

	return n;
}


int do_read_buffer10(DCB *dcb, uint8_t *buffer) {
	int n;
	n = read(dcb->fd, buffer, 10);

	if (n == 0 & (errno==EOF)) {
		fprintf(stderr, "ReadBuffer Socket EOF\n");
		return -1;
	}

	if (n < 0) {
		fprintf(stderr, "ReadBuffer error: %i [%s]\n", errno, strerror(errno));
		if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
			return -1;
		else
			return 0;
	}

#ifdef GW_READ_DEBUG
	fprintf(stderr, "socket %i, do_read % i bytes\n", dcb->fd, n);
#endif

	return n;
}

int do_read_buffer(DCB *dcb, uint8_t *buffer) {
	int n;
	n = read(dcb->fd, buffer, MAX_BUFFER_SIZE);

	if (n == 0 & (errno==EOF)) {
		fprintf(stderr, "ReadBuffer Socket EOF\n");
		return -2;
	}

	if (n < 0) {
		fprintf(stderr, "ReadBuffer error: %i [%s]\n", errno, strerror(errno));
		if ((errno != EAGAIN) || (errno != EWOULDBLOCK))
			return -1;
		else
			return 0;
	}

#ifdef GW_READ_DEBUG
	fprintf(stderr, "socket %i, do_read % i bytes\n", dcb->fd, n);
#endif

	return n;
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
			(dcb->session->backends->func).error(dcb->session->backends, -1);
		}
		dcb->func.error(dcb, -1);
		return 1;
	}

	while (b > 0) {
		int bufsize = b < MAX_BUFFER_SIZE ? b : MAX_BUFFER_SIZE;
		if ((buffer = gwbuf_alloc(bufsize)) == NULL) {
			/* Bad news, we have run out of memory */
			/* Error handling */
			if (dcb->session->backends) {
				(dcb->session->backends->func).error(dcb->session->backends, -1);
			}
			(dcb->func).error(dcb, -1);
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
					(dcb->session->backends->func).error(dcb->session->backends, -1);
				}
				(dcb->func).error(dcb, -1);
				return 1;
			}
		}

		if (n == 0) {
			//  socket closed
			fprintf(stderr, "Client connection %i closed: %i, %s\n", dcb->fd, errno, strerror(errno));
			if (dcb->session->backends) {
				(dcb->session->backends->func).error(dcb->session->backends, -1);
			}
			(dcb->func).error(dcb, -1);
			return 1;
		}

		// append read data to the gwbuf
		*head = gwbuf_append(*head, buffer);

		// how many bytes left
		b -= n;
	}

	return 0;
}

/**
 * Create a new MySQL backend connection.
 *
 * This routine performs the MySQL connection to the backend and fills the session->backends of the callier dcb
 * with the new allocatetd dcb and adds the new socket to the epoll set
 *
 * - backend dcb allocation
 * - MySQL session data fetch
 * - backend connection using data in MySQL session
 *
 * @param client_dcb The client DCB struct
 * @param epfd The epoll set to add the new connection
 * @return 0 on Success or 1 on Failure.
 */
int create_backend_connection(DCB *client_dcb, int efd) {
	struct epoll_event ee;
	DCB *backend = NULL;
	MySQLProtocol *ptr_proto = NULL;
	MySQLProtocol *client_protocol = NULL;
	SESSION *session = NULL;
	MYSQL_session *s_data = NULL;

	backend = (DCB *) calloc(1, sizeof(DCB));
	backend->state = DCB_STATE_ALLOC;
	backend->session = NULL;
	backend->protocol = (MySQLProtocol *)gw_mysql_init(NULL);

	ptr_proto = (MySQLProtocol *)backend->protocol;
	client_protocol = (MySQLProtocol *)client_dcb->protocol;
	session = DCB_SESSION(client_dcb);
	s_data = (MYSQL_session *)session->data;

	// this is blocking until auth done
	if (gw_mysql_connect("127.0.0.1", 3306, s_data->db, s_data->user, s_data->client_sha1, backend->protocol) == 0) {
		fprintf(stderr, "Connected to backend mysql server\n");
		backend->fd = ptr_proto->fd;
		setnonblocking(backend->fd);
	} else {
		fprintf(stderr, "<<<< NOT Connected to backend mysql server!!!\n");
		backend->fd = -1;
	}

	// edge triggering flag added
	ee.events = EPOLLIN | EPOLLET | EPOLLOUT;
	ee.data.ptr = backend;

	// if connected, add it to the epoll
	if (backend->fd > 0) {
		if (epoll_ctl(efd, EPOLL_CTL_ADD, backend->fd, &ee) == -1) {
			perror("epoll_ctl: backend sock");
		} else {
			fprintf(stderr, "--> Backend conn added, bk_fd [%i], scramble [%s], is session with client_fd [%i]\n", ptr_proto->fd, ptr_proto->scramble, client_dcb->fd);
			backend->state = DCB_STATE_POLLING;
			backend->session = DCB_SESSION(client_dcb);
			(backend->func).read = gw_read_backend_event;
			(backend->func).write = MySQLWrite;
			(backend->func).write_ready = gw_write_backend_event;
			(backend->func).error = handle_event_errors_backend;

			// assume here one backend only.
			// in session.h
			// struct dcb      *backends;
			// instead of a list **backends;
			client_dcb->session->backends = backend;
		}
		return 0;
	}
	return 1;
}
//////
