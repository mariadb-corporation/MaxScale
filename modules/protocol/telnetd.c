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
#include <dcb.h>
#include <buffer.h>

/**
 * @file telnetd - telnet daemon protocol module
 *
 * @verbatim
 * Revision History
 * Date		Who			Description
 * 17/06/2013	Mark Riddoch		Initial version
 *
 * @endverbatim
 */

static char *version_str = "V1.0.0";

static int telnetd_read_event(DCB* dcb, int epfd);
static int telnetd_write_event(DCB *dcb, int epfd);
static int telnetd_write(DCB *dcb, GWBUF *queue);
static int telnetd_error(DCB *dcb, int event);
static int telnetd_hangup(DCB *dcb, int event);
static int telnetd_accept(DCB *dcb, int event);
static int telnetd_close(DCB *dcb, int event);

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
	telnetd_close				/**< Close			 */
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
	fprintf(stderr, "Initialise Telnetd Protcol module.\n");
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
}

/**
 * Handler for the EPOLLERR event.
 *
 * @param dcb	The descriptor control block
 * @param epfd	The epoll descriptor
 */
static int
telnetd_error(DCB *dcb, int event)
{
}

/**
 * Handler for the EPOLLHUP event.
 *
 * @param dcb	The descriptor control block
 * @param epfd	The epoll descriptor
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
 * @param epfd	The epoll descriptor
 */
static int
telnetd_accept(DCB *dcb, int event)
{
}

/**
 * The close handler for the descriptor. Called by the gateway to
 * explicitly close a connection.
 *
 * @param dcb	The descriptor control block
 * @param epfd	The epoll descriptor
 */

static int
telnetd_close(DCB *dcb, int event)
{
}


