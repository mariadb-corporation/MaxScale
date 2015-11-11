#ifndef _ROUTER_H
#define _ROUTER_H
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2013-2014
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
 * 16/07/2013	Massimiliano Pinto	Added router commands values
 * 22/10/2013	Massimiliano Pinto	Added router errorReply entry point
 * 27/10/2015   Martin Brampton         Add RCAP_TYPE_NO_RSESSION
 *
 */
#include <service.h>
#include <session.h>
#include <buffer.h>
#include <stdint.h>

/**
 * The ROUTER handle points to module specific data, so the best we can do
 * is to make it a void * externally.
 */
typedef void *ROUTER;

typedef enum error_action {
	ERRACT_NEW_CONNECTION = 0x001,
	ERRACT_REPLY_CLIENT   = 0x002
} error_action_t;

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
 *	errorReply		Called to reply to client errors with optional closeSession or
 *				make a request for a new backend connection
 *
 * @endverbatim
 *
 * @see load_module
 */
typedef struct router_object {
	ROUTER	*(*createInstance)(SERVICE *service, char **options);
	void	*(*newSession)(ROUTER *instance, SESSION *session);
	void 	(*closeSession)(ROUTER *instance, void *router_session);
        void 	(*freeSession)(ROUTER *instance, void *router_session);
	int	(*routeQuery)(ROUTER *instance, void *router_session, GWBUF *queue);
	void	(*diagnostics)(ROUTER *instance, DCB *dcb);
	void    (*clientReply)(ROUTER* instance, void* router_session, GWBUF* queue, DCB *backend_dcb);
	void    (*handleError)(
                        ROUTER*        instance, 
                        void*          router_session, 
                        GWBUF*         errmsgbuf, 
                        DCB*           backend_dcb, 
                        error_action_t action, 
                        bool*          succp);
        int (*getCapabilities)();
} ROUTER_OBJECT;

/**
 * The router module API version. Any change that changes the router API
 * must update these versions numbers in accordance with the rules in
 * modinfo.h.
 */
#define	ROUTER_VERSION	{ 1, 0, 0 }

/**
 * Router capability type. Indicates what kind of input router accepts.
 */
typedef enum router_capability_t {
        RCAP_TYPE_UNDEFINED    = 0x00,
        RCAP_TYPE_STMT_INPUT   = 0x01,  /*< statement per buffer */
        RCAP_TYPE_PACKET_INPUT = 0x02,  /*< data as it was read from DCB */
        RCAP_TYPE_NO_RSESSION  = 0x04   /*< router does not use router sessions */
} router_capability_t;

        

#endif
