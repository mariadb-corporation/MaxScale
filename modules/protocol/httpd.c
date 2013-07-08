/*
 * This file is distributed as part of the SkySQL Gateway.  It is free
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
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dcb.h>
#include <buffer.h>
#include <service.h>
#include <session.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <router.h>
#include <poll.h>
#include <atomic.h>
#include <gw.h>

/**
 * @file httpd.c - HTTP daemon protocol module
 *
 * The httpd protocol module is intended as a mechanism to allow connections
 * into the gateway for the purpose of accessing information within
 * the gateway with a REST interface
 * databases.
 *
 * In the first instance it is intended to allow a debug connection to access
 * internal data structures, however it may also be used to manage the 
 * configuration of the gateway via REST interface.
 *
 * @verbatim
 * Revision History
 * Date		Who			Description
 * 08/07/2013	Massimiliano Pinto	Initial version
 *
 * @endverbatim
 */

#define ISspace(x) isspace((int)(x))
#define HTTP_SERVER_STRING "Gateway(c) v.1.0.0\r\n"
static char *version_str = "V1.0.0";

static int httpd_read_event(DCB* dcb);
static int httpd_write_event(DCB *dcb);
static int httpd_write(DCB *dcb, GWBUF *queue);
static int httpd_error(DCB *dcb);
static int httpd_hangup(DCB *dcb);
static int httpd_accept(DCB *dcb);
static int httpd_close(DCB *dcb);
static int httpd_listen(DCB *dcb, char *config);
static int httpd_get_line(int sock, char *buf, int size);
static void httpd_send_headers(int client, const char *filename);

/**
 * The "module object" for the httpd protocol module.
 */
static GWPROTOCOL MyObject = { 
	httpd_read_event,			/**< Read - EPOLLIN handler	 */
	httpd_write,				/**< Write - data from gateway	 */
	httpd_write_event,			/**< WriteReady - EPOLLOUT handler */
	httpd_error,				/**< Error - EPOLLERR handler	 */
	httpd_hangup,				/**< HangUp - EPOLLHUP handler	 */
	httpd_accept,				/**< Accept			 */
	NULL,					/**< Connect			 */
	httpd_close,				/**< Close			 */
	httpd_listen				/**< Create a listener		 */
	};

static void
httpd_command(DCB *, char *cmd);

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	fprintf(stderr, "Initialise HTTPD Protocol module.\n");
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
GWPROTOCOL *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Read event for EPOLLIN on the httpd protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
httpd_read_event(DCB* dcb)
{
int		n = -1;
GWBUF		*head = NULL;
SESSION		*session = dcb->session;
ROUTER_OBJECT	*router = session->service->router;
ROUTER		*router_instance = session->service->router_instance;
void		*rsession = session->router_session;

int numchars = 1;
char buf[1024];
char *query_string = NULL;
char *path_info = NULL;
char method[255];
char url[255];
char path[512];
int cgi = 0;
size_t i, j;
GWBUF *buffer=NULL;

	dcb->state = DCB_STATE_PROCESSING;

	numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));
	i = 0; j = 0;
	while (!ISspace(buf[j]) && (i < sizeof(method) - 1)) {
		method[i] = buf[j];
		i++; j++;
	}
	method[i] = '\0';

	if (strcasecmp(method, "GET") && strcasecmp(method, "POST")) {
		//httpd_unimplemented(dcb->fd);
		return 0;
	}

	if ((buffer = gwbuf_alloc(1024)) == NULL) {
		//httpd_error(dcb->fd);
		return 0;
	}

	if (strcasecmp(method, "POST") == 0)
		cgi = 1;

	i = 0;
	while (ISspace(buf[j]) && (j < sizeof(buf)))
		j++;
	while (!ISspace(buf[j]) && (i < sizeof(url) - 1) && (j < sizeof(buf))) {
		url[i] = buf[j];
		i++; j++;
	}
	url[i] = '\0';

	if (strcasecmp(method, "GET") == 0) {
		query_string = url;
		while ((*query_string != '?') && (*query_string != '\0'))
			query_string++;
		if (*query_string == '?') {
			cgi = 1;
			*query_string = '\0';
			query_string++;
		}
	}

	while ((numchars > 0) && strcmp("\n", buf))  /* read & discard headers */
		numchars = httpd_get_line(dcb->fd, buf, sizeof(buf));

	httpd_send_headers(dcb->fd, NULL);

	strcpy(GWBUF_DATA(buffer), "Welcome to HTTPD Gateway (c)\n");

	dcb->func.write(dcb, buffer);

	dcb_close(dcb);	

	dcb->state = DCB_STATE_POLLING;

	return n;
}

/**
 * EPOLLOUT handler for the HTTPD protocol module.
 *
 * @param dcb	The descriptor control block
 * @return
 */
static int
httpd_write_event(DCB *dcb)
{
	return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the HTTPD protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb	Descriptor Control Block for the socket
 * @param queue	Linked list of buffes to write
 */
static int
httpd_write(DCB *dcb, GWBUF *queue)
{
	return dcb_write(dcb, queue);
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_error(DCB *dcb)
{
	return 0;
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_hangup(DCB *dcb)
{
	return 0;
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb	The descriptor control block
 */
static int
httpd_accept(DCB *dcb)
{
int	n_connect = 0;

	while (1)
	{
		int			so;
		struct sockaddr_in	addr;
		socklen_t		addrlen;
		DCB			*client;

		if ((so = accept(dcb->fd, (struct sockaddr *)&addr, &addrlen)) == -1)
			return n_connect;
		else
		{
			atomic_add(&dcb->stats.n_accepts, 1);
			client = dcb_alloc();
			client->fd = so;
			client->remote = strdup(inet_ntoa(addr.sin_addr));
			memcpy(&client->func, &MyObject, sizeof(GWPROTOCOL));
			client->session = session_alloc(dcb->session->service, client);

			client->state = DCB_STATE_IDLE;

			if (poll_add_dcb(client) == -1)
			{
				return n_connect;
			}
			n_connect++;

			client->state = DCB_STATE_POLLING;
		}
	}
	return n_connect;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb	The descriptor control block
 */

static int
httpd_close(DCB *dcb)
{
	dcb_close(dcb);
	return 0;
}

/**
 * HTTTP daemon listener entry point
 *
 * @param	listener	The Listener DCB
 * @param	config		Configuration (ip:port)
 */
static int
httpd_listen(DCB *listener, char *config)
{
struct sockaddr_in	addr;
char			*port;
int			one = 1;
short			pnum;

	memcpy(&listener->func, &MyObject, sizeof(GWPROTOCOL));

	port = strrchr(config, ':');
	if (port)
		port++;
	else
		port = "6442";

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	pnum = atoi(port);
	addr.sin_port = htons(pnum);

	if ((listener->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return 0;
	}

        /* socket options */
	setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));

        /* set NONBLOCKING mode */
        setnonblocking(listener->fd);

        /* bind address and port */
        if (bind(listener->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
        	return 0;
	}

	listener->state = DCB_STATE_LISTENING; 
	listen(listener->fd, SOMAXCONN);

        if (poll_add_dcb(listener) == -1)
	{
		return 0;
	}
	return 1;
}

/**
 * HTTPD command implementation
 *
 * Called for each command in the HTTP stream.
 *
 * Currently we do no command execution
 *
 * @param	dcb	The client DCB
 * @param	cmd	The command stream
 */
static void
httpd_command(DCB *dcb, char *cmd)
{
}

static int httpd_get_line(int sock, char *buf, int size) {
 int i = 0;
 char c = '\0';
 int n;

 while ((i < size - 1) && (c != '\n'))
 {
  n = recv(sock, &c, 1, 0);
  /* DEBUG printf("%02X\n", c); */
  if (n > 0)
  {
   if (c == '\r')
   {
    n = recv(sock, &c, 1, MSG_PEEK);
    /* DEBUG printf("%02X\n", c); */
    if ((n > 0) && (c == '\n'))
     recv(sock, &c, 1, 0);
    else
     c = '\n';
   }
   buf[i] = c;
   i++;
  }
  else
   c = '\n';
 }
 buf[i] = '\0';

 return(i);
}

static void httpd_send_headers(int client, const char *filename)
{
 char buf[1024];
 (void)filename;  /* could use filename to determine file type */

 strcpy(buf, "HTTP/1.0 200 OK\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, HTTP_SERVER_STRING);
 send(client, buf, strlen(buf), 0);
 sprintf(buf, "Content-Type: text/html\r\n");
 send(client, buf, strlen(buf), 0);
 strcpy(buf, "\r\n");
 send(client, buf, strlen(buf), 0);
}
///
