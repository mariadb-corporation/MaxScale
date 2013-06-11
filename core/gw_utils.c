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
 * 03-06-2013	Massimiliano Pinto	gateway utils
 *
 */

#include <gw.h>
#include <dcb.h>

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

const char *gw_dcb_state2string (int state) {
	switch(state) {
		case DCB_STATE_ALLOC:
			return "DCB Allocated";
		case DCB_STATE_IDLE:
			return "DCB not yet in polling";
		case DCB_STATE_POLLING:
			return "DCB in the EPOLL";
		case DCB_STATE_PROCESSING:
			return "DCB processing event";
		case DCB_STATE_LISTENING:
			return "DCB for listening socket";
		case DCB_STATE_DISCONNECTED:
			return "DCB socket closed";
		default:
			return "DCB (unknown)";
	}
}
