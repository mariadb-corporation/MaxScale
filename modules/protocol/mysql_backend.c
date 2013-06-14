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

/*
 * MySQL Protocol module for handling the protocol between the gateway
 * and the backend MySQL database.
 *
 * Revision History
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial version
 */

static char *version_str = "V1.0.0";

extern int gw_read_backend_event(DCB* dcb, int epfd);
extern int gw_write_backend_event(DCB *dcb, int epfd);
extern int MySQLWrite(DCB *dcb, GWBUF *queue);
extern int handle_event_errors_backend(DCB *dcb, int event);

static GWPROTOCOL MyObject = { 
	gw_read_backend_event,			/* Read - EPOLLIN handler	 */
	MySQLWrite,				/* Write - data from gateway	 */
	gw_write_backend_event,			/* WriteReady - EPOLLOUT handler */
	handle_event_errors_backend,		/* Error - EPOLLERR handler	 */
	NULL,					/* HangUp - EPOLLHUP handler	 */
	NULL,					/* Accept			 */
	NULL,					/* Connect			 */
	NULL					/* Close			 */
	};

/*
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/*
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	fprintf(stderr, "Initial MySQL Client Protcol module.\n");
}

/*
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
