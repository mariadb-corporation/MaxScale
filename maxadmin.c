/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file maxadmin.c  - The MaxScale administration client
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 13/06/14	Mark Riddoch	Initial implementation
 * 15/06/14	Mark Riddoch	Addition of source command
 * 26/06/14	Mark Riddoch	Fix issue with final OK split across
 *				multiple reads
 *
 * @endverbatim
 */

#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>
#include <locale.h>
#include <errno.h>
#include <getopt.h>

//#include <version.h>


static int connectMaxScale(char *hostname, char *port);
static int setipaddress(struct in_addr *a, char *p);
static int authMaxScale(int so, char *user, char *password);
static int sendCommand(int so, char *cmd, char *buf);


/**
 * The main for the maxadmin client
 *
 * @param argc	Number of arguments
 * @param argv	The command line arguments
 */
int
main(int argc, char **argv)
{

    char		buf[1024];

    char		*hostname = "192.168.122.105";
    char		*port = "6603";
    char		*user = "admin";
    char		*passwd = "skysql";
    int		so;

    if ((so = connectMaxScale(hostname, port)) == -1)
        exit(1);
    if (!authMaxScale(so, user, passwd))
    {
        fprintf(stderr, "Failed to connect to MaxScale. "
                "Incorrect username or password.\n");
        exit(1);
    }

    sendCommand(so, "show server server2", buf);

    printf("%s", buf);

    close(so);
    return 0;
}

/**
 * Connect to the MaxScale server
 *
 * @param hostname	The hostname to connect to
 * @param port		The port to use for the connection
 * @return		The connected socket or -1 on error
 */
static int
connectMaxScale(char *hostname, char *port)
{
struct sockaddr_in	addr;
int			so;
int			keepalive = 1;

	if ((so = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		fprintf(stderr, "Unable to create socket: %s\n",
				strerror(errno));
		return -1;
	}
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	setipaddress(&addr.sin_addr, hostname);
	addr.sin_port = htons(atoi(port));
	if (connect(so, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		fprintf(stderr, "Unable to connect to MaxScale at %s, %s: %s\n",
				hostname, port, strerror(errno));
		return -1;
	}
	if (setsockopt(so, SOL_SOCKET,
			SO_KEEPALIVE, &keepalive , sizeof(keepalive )))
		perror("setsockopt");

	return so;
}


/**
 * Set IP address in socket structure in_addr
 *
 * @param a	Pointer to a struct in_addr into which the address is written
 * @param p	The hostname to lookup
 * @return	1 on success, 0 on failure
 */
static int
setipaddress(struct in_addr *a, char *p)
{
#ifdef __USE_POSIX
	struct addrinfo *ai = NULL, hint;
	int    rc;
	struct sockaddr_in * res_addr;
	memset(&hint, 0, sizeof (hint));

	hint.ai_socktype = SOCK_STREAM;
	hint.ai_flags = AI_CANONNAME;
	hint.ai_family = AF_INET;

	if ((rc = getaddrinfo(p, NULL, &hint, &ai)) != 0) {
		return 0;
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
			return 0;
		}
	} else {
        	/* take the first one */
		memcpy(a, h->h_addr, h->h_length);

		return 1;
	}
#endif
	return 0;
}

/**
 * Perform authentication using the maxscaled protocol conventions
 *
 * @param so		The socket connected to MaxScale
 * @param user		The username to authenticate
 * @param password	The password to authenticate with
 * @return		Non-zero of succesful authentication
 */
static int
authMaxScale(int so, char *user, char *password)
{
char	buf[20];

	if (read(so, buf, 4) != 4)
		return 0;
	write(so, user, strlen(user));
	if (read(so, buf, 8) != 8)
		return 0;
	write(so, password, strlen(password));
	if (read(so, buf, 6) != 6)
		return 0;

	return strncmp(buf, "FAILED", 6);
}

/**
 * Send a comamnd using the MaxScaled protocol, display the return data
 * on standard output.
 *
 * Input terminates with a lien containing just the text OK
 *
 * @param so	The socket connect to MaxScale
 * @param cmd	The command to send
 * @return	0 if the connection was closed
 */
static int
sendCommand(int so, char *cmd, char *buf)
{
char	buf1[80];
int	i, j, newline = 1;
int k=0;

	if (write(so, cmd, strlen(cmd)) == -1)
		return 0;
	while (1)
	{
        if ((i = read(so, buf1, 80)) <= 0)
			return 0;
		for (j = 0; j < i; j++)
		{
            if (newline == 1 && buf1[j] == 'O')
				newline = 2;
            else if (newline == 2 && buf1[j] == 'K' && j == i - 1)
			{
				return 1;
			}
			else if (newline == 2)
			{
                buf[k] = 'O'; k++;
                buf[k] = buf1[j]; k++;
				newline = 0;
			}
            else if (buf1[j] == '\n' || buf1[j] == '\r')
			{
                buf[k] = buf1[j]; k++;
				newline = 1;
			}
			else
			{
                buf[k] = buf1[j]; k++;
				newline = 0;
			}
		}
	}
	return 1;
}

