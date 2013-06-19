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
#include <string.h>
#include <dcb.h>
#include <buffer.h>
#include <service.h>
#include <session.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <router.h>

/**
 * @file telnetd.c - telnet daemon protocol module
 *
 * The telnetd protocol module is intended as a mechanism to allow connections
 * into the gateway for the purpsoe of accessing debugging information within
 * the gateway rather than a protocol to be used to send queries to backend
 * databases.
 *
 * In the first instance it is intended to allow a debug connection to access
 * internal data structures, however it may also be used to manage the 
 * configuration of the gateway.
 *
 * @verbatim
 * Revision History
 * Date		Who			Description
 * 17/06/2013	Mark Riddoch		Initial version
 *
 * @endverbatim
 */
#define	TELNET_IAC	255

static char *version_str = "V1.0.0";

static int telnetd_read_event(DCB* dcb, int epfd);
static int telnetd_write_event(DCB *dcb, int epfd);
static int telnetd_write(DCB *dcb, GWBUF *queue);
static int telnetd_error(DCB *dcb, int event);
static int telnetd_hangup(DCB *dcb, int event);
static int telnetd_accept(DCB *dcb, int event);
static int telnetd_close(DCB *dcb, int event);
static int telnetd_listen(DCB *dcb, int efd, char *config);

/**
 * The "module object" for the telnetd protocol module.
 */
static GWPROTOCOL MyObject = { 
	telnetd_read_event,			/**< Read - EPOLLIN handler	 */
	telnetd_write,				/**< Write - data from gateway	 */
	telnetd_write_event,			/**< WriteReady - EPOLLOUT handler */
	telnetd_error,				/**< Error - EPOLLERR handler	 */
	telnetd_hangup,				/**< HangUp - EPOLLHUP handler	 */
	telnetd_accept,				/**< Accept			 */
	NULL,					/**< Connect			 */
	telnetd_close,				/**< Close			 */
	telnetd_listen				/**< Create a listener		 */
	};

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
	fprintf(stderr, "Initialise Telnetd Protocol module.\n");
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
 * Read event for EPOLLIN on the telnetd protocol module.
 *
 * @param dcb	The descriptor control block
 * @param epfd	The epoll descriptor
 * @return
 */
static int
telnetd_read_event(DCB* dcb, int epfd)
{
int		n;
GWBUF		*head = NULL;
SESSION		*session = dcb->session;
ROUTER_OBJECT	*router = session->service->router;
ROUTER		*router_instance = session->service->router_instance;
void		*rsession = session->router_session;

	if ((n = dcb_read(dcb, &head)) != -1)
	{
		if (head)
		{
			char *ptr = GWBUF_DATA(head);
			ptr = GWBUF_DATA(head);
			if (*ptr == TELNET_IAC)
			{
				GWBUF_CONSUME(head, 2);
			}
			router->routeQuery(router_instance, rsession, head);
		}
	}

	return n;
}

/**
 * EPOLLOUT handler for the telnetd protocol module.
 *
 * @param dcb	The descriptor control block
 * @param epfd	The epoll descriptor
 * @return
 */
static int
telnetd_write_event(DCB *dcb, int epfd)
{
	return dcb_drain_writeq(dcb);
}

/**
 * Write routine for the telnetd protocol module.
 *
 * Writes the content of the buffer queue to the socket
 * observing the non-blocking principles of the gateway.
 *
 * @param dcb	Descriptor Control Block for the socket
 * @param queue	Linked list of buffes to write
 */
static int
telnetd_write(DCB *dcb, GWBUF *queue)
{
	return dcb_write(dcb, queue);
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb	The descriptor control block
 * @param event	The epoll descriptor
 */
static int
telnetd_error(DCB *dcb, int event)
{
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb	The descriptor control block
 * @param event	The epoll descriptor
 */
static int
telnetd_hangup(DCB *dcb, int event)
{
}

/**
 * Handler for the EPOLLIN event when the DCB refers to the listening
 * socket for the protocol.
 *
 * @param dcb	The descriptor control block
 * @param efd	The epoll descriptor
 */
static int
telnetd_accept(DCB *dcb, int efd)
{
int	n_connect = 0;

	while (1)
	{
		int			so;
		struct sockaddr_in	addr;
		socklen_t		addrlen;
		DCB			*client;
		struct epoll_event	ee;

		if ((so = accept(dcb->fd, (struct sockaddr *)&addr, &addrlen)) == -1)
			return n_connect;
		else
		{
			atomic_add(&dcb->stats.n_accepts, 1);
			client = alloc_dcb();
			client->fd = so;
			memcpy(&client->func, &MyObject, sizeof(GWPROTOCOL));
			client->session = session_alloc(dcb->session->service, client);

			ee.events = EPOLLIN | EPOLLOUT | EPOLLET;
			ee.data.ptr = client;
			client->state = DCB_STATE_IDLE;

			if (epoll_ctl(efd, EPOLL_CTL_ADD, so, &ee) == -1)
			{
				return n_connect;
			}
			n_connect++;

			dcb_printf(client, "Gateway> ");
		}
	}
	return n_connect;
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb	The descriptor control block
 * @param efd	The epoll descriptor
 */

static int
telnetd_close(DCB *dcb, int efd)
{
	dcb_close(dcb, efd);
}

/**
 * Telnet daemon listener entry point
 *
 * @param	efd		epoll File descriptor
 * @param	config		Configuration (ip:port)
 */
static int
telnetd_listen(DCB *listener, int efd, char *config)
{
struct sockaddr_in	addr;
char			*port;
struct epoll_event	ev;
int			one = 1;
short			pnum;

	memcpy(&listener->func, &MyObject, sizeof(GWPROTOCOL));

	port = strrchr(config, ':');
	if (port)
		port++;
	else
		port = "4442";

	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	pnum = atoi(port);
	addr.sin_port = htons(pnum);

	if ((listener->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return 0;
	}

        // socket options
	setsockopt(listener->fd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(one));
        // set NONBLOCKING mode
        setnonblocking(listener->fd);
        // bind address and port
        if (bind(listener->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
        	return 0;
	}

	listener->state = DCB_STATE_LISTENING; 
	listen(listener->fd, SOMAXCONN);

	ev.events = EPOLLIN;
        ev.data.ptr = listener;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, listener->fd, &ev) == -1)
	{
		return 0;
	}
	return 1;
}
