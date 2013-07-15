#ifndef _ROUTER_H
#define _ROUTER_H
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

/**
 * @file router.h -  The query router interface mechanisms
 *
 * Revision History
 *
 * Date		Who			Description
 * 14/06/2013	Mark Riddoch		Initial implementation
 * 26/06/2013	Mark Riddoch		Addition of router options
 * 					and the diagnostic entry point
 * 15/07/2013	Massimiliano Pinto	Added clientReply entry point
 *
 */
#include <service.h>
#include <session.h>
#include <buffer.h>

/**
 * The ROUTER handle points to module specific data, so the best we can do
 * is to make it a void * externally.
 */
typedef void *ROUTER;


/**
 * @verbatim
 * The "module object" structure for a query router module
 *
 * The entry points are:
 * 	createInstance		Called by the service to create a new
 * 				instance of the query router
 * 	newSession		Called to create a new user session
 * 				within the query router
 * 	closeSession		Called when a session is closed
 * 	routeQuery		Called on each query that requires
 * 				routing
 * 	diagnostics		Called to force the router to print
 * 				diagnostic output
 *	clientReply		Called to reply to client the data from one or all backends
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct router_object {
	ROUTER	*(*createInstance)(SERVICE *service, char **options);
	void	*(*newSession)(ROUTER *instance, SESSION *session);
	void 	(*closeSession)(ROUTER *instance, void *router_session);
	int	(*routeQuery)(ROUTER *instance, void *router_session, GWBUF *queue);
	void	(*diagnostics)(ROUTER *instance, DCB *dcb);
	void    (*clientReply)(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb);
} ROUTER_OBJECT;
#endif
