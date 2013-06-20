#ifndef _SESSION_H
#define _SESSION_H
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
 * @file session.h
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 01-06-2013	Mark Riddoch		Initial implementation
 * 14-06-2013	Massimiliano Pinto	Added void *data to session
 *					for session specific data
 * @endverbatim
 */
#include <time.h>

struct dcb;
struct service;

/**
 * The session statistics structure
 */
typedef struct {
	time_t		connect;	/**< Time when the session was started */
} SESSION_STATS;

/**
 * The session status block
 *
 * A session status block is created for each user (client) connection
 * to the database, it links the descriptors, routing implementation
 * and originating service together for the client session.
 */
typedef struct session {
	int 		state;		/**< Current descriptor state */
	struct dcb	*client;	/**< The client connection */
	struct dcb	*backends;	/**< The set of backend servers */
	void 		*data;		/**< The session data */
	void		*router_session;/**< The router instance data */
	SESSION_STATS	stats;		/**< Session statistics */
	struct service	*service;	/**< The service this session is using */
	struct session	*next;		/**< Linked list of all sessions */
} SESSION;

#define SESSION_STATE_ALLOC		0
#define SESSION_STATE_READY		1
#define SESSION_STATE_LISTENER		2

#define SESSION_PROTOCOL(x, type)	DCB_PROTOCOL((x)->client, type)

extern SESSION	*session_alloc(struct service *, struct dcb *);
extern void	session_free(SESSION *);
extern void	printAllSessions();
extern void	printSession(SESSION *);
extern void	dprintAllSessions(struct dcb *);
extern void	dprintSession(struct dcb *, SESSION *);
extern char	*session_state(int);
#endif
