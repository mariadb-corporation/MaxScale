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
 * @file gw_utils.c - A set if utility functions useful within the context
 * of the gateway.
 *
 * @verbatim
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
 * 06-02-2014	Mark Riddoch		Added parse_bindconfig
 * 10-02-2014	Massimiliano Pinto	Added return code to setipaddress
 *
 *@endverbatim
 */

#include <gw.h>
#include <dcb.h>
#include <session.h>

#include <skygw_utils.h>
#include <log_manager.h>

SPINLOCK tmplock = SPINLOCK_INIT;

extern int lm_enabled_logfiles_bitmask;

/*
 * Set IP address in socket structure in_addr
 *
 * @param a	Pointer to a struct in_addr into which the address is written
 * @param p	The hostname to lookup
 * @return	1 on success, 0 on failure
 */
int
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
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
					"Error : getaddrinfo failed for [%s] due [%s]",
					p,
					gai_strerror(rc))));

			return 0;
		}
	} else {
		hint.ai_flags = AI_CANONNAME;
		hint.ai_family = AF_INET;

		if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0) {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
					"Error : getaddrinfo failed for [%s] due [%s]",
					p,
					gai_strerror(rc))));

			return 0;
		}
	}

        /* take the first one */
	if (ai != NULL) {
		res_addr = (struct sockaddr_in *)(ai->ai_addr);
		memcpy(a, &res_addr->sin_addr, sizeof(struct in_addr));

		freeaddrinfo(ai);

		return 1;
	}
#else
	struct hostent *h;

        spinlock_acquire(&tmplock);
        h = gethostbyname(p);
        spinlock_release(&tmplock);
        
	if (h == NULL) {
		if ((a->s_addr = inet_addr(p)) == -1) {
			LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
					"Error : gethostbyname failed for [%s]",
					p)));

			return 0;
		}
	} else {
        	/* take the first one */
		memcpy(a, h->h_addr, h->h_length);

		return 1;
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
                ss_dassert(false);
#if 0
		dcb->func.close(dcb);
#endif
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
				gwbuf_free(buffer);
				return 1;
			} else {
				gwbuf_free(buffer);
				(dcb->func).close(dcb);
				return 1;
			}
		}

		if (n == 0) {
			//  socket closed
			gwbuf_free(buffer);
#if 1
			(dcb->func).close(dcb);
#endif
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
 * Parse the bind config data. This is passed in a string as address:port.
 *
 * The address may be either a . seperated IP address or a hostname to
 * lookup. The address 0.0.0.0 is the wildcard address for SOCKADR_ANY.
 * The ':' and port may be omitted, in which case the default port is
 * used.
 *
 * @param config	The bind address and port seperated by a ':'
 * @param def_port	The default port to use
 * @param addr		The sockaddr_in in which the data is written
 * @return		0 on failure
 */
int
parse_bindconfig(char *config, unsigned short def_port, struct sockaddr_in *addr)
{
char			*port, buf[1024];
short			pnum;
struct hostent		*hp;


	strncpy(buf, config, 1024);
	port = strrchr(buf, ':');
	if (port)
	{
		*port = 0;
		port++;
		pnum = atoi(port);
	}
	else
	{
		pnum = def_port;
	}

	if (!strcmp(buf, "0.0.0.0"))
	{
		addr->sin_addr.s_addr = htonl(INADDR_ANY);
	}
	else
	{
		if (!inet_aton(buf, &addr->sin_addr))
		{
			if ((hp = gethostbyname(buf)) != NULL)
			{
				bcopy(hp->h_addr, &(addr->sin_addr.s_addr), hp->h_length);
			}
			else
			{
                		LOGIF(LE, (skygw_log_write_flush(
		                        LOGFILE_ERROR,
                       			 "Error : Failed to lookup host '%s'. ",
		                        buf)));
				return 0;
			}
		}
	}

	addr->sin_family = AF_INET;
	addr->sin_port = htons(pnum);
	return 1;
}
