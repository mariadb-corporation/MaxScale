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
 * @file mysql_client.c - the protcol module for clients connecting to the gateway
 *
 * MySQL Protocol module for handling the protocol between the gateway
 * and the client.
 *
 * @verbatim
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 *
 * @endverbatim
 */

static char *version_str = "V1.0.0";

extern int gw_route_read_event(DCB* dcb, int epfd);
extern int gw_handle_write_event(DCB *dcb, int epfd);
extern int MySQLWrite(DCB *dcb, GWBUF *queue);
extern int handle_event_errors(DCB *dcb, int event);

/**
 * The "module object" that defines the entry points to the protocol module
 */
static GWPROTOCOL MyObject = { 
	gw_route_read_event,			/**< Read - EPOLLIN handler	 */
	MySQLWrite,				/**< Write - data from gateway	 */
	gw_handle_write_event,			/**< WriteReady - EPOLLOUT handler */
	handle_event_errors,			/**< Error - EPOLLERR handler	 */
	NULL,					/**< HangUp - EPOLLHUP handler	 */
	NULL,					/**< Accept			 */
	NULL,					/**< Connect			 */
	NULL					/* Close			 */
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
	fprintf(stderr, "Initial MySQL Client Protcol module.\n");
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
