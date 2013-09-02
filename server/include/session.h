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
 * 01-07-2013	Massimiliano Pinto	Removed backends pointer
 *					from struct session
 * 02-09-2013	Massimiliano Pinto	Added session ref counter
 *
 * @endverbatim
 */
#include <time.h>
#include <atomic.h>
#include <spinlock.h>
#include <skygw_utils.h>

struct dcb;
struct service;

/**
 * The session statistics structure
 */
typedef struct {
	time_t		connect;	/**< Time when the session was started */
} SESSION_STATS;

typedef enum {
    SESSION_STATE_ALLOC,
    SESSION_STATE_READY,
    SESSION_STATE_LISTENER,
    SESSION_STATE_LISTENER_STOPPED
} session_state_t;

/**
 * The session status block
 *
 * A session status block is created for each user (client) connection
 * to the database, it links the descriptors, routing implementation
 * and originating service together for the client session.
 */
typedef struct session {
        skygw_chk_t     ses_chk_top;
        SPINLOCK        ses_lock;
	session_state_t state;		/**< Current descriptor state */
	struct dcb	*client;	/**< The client connection */
	struct dcb	*backends;	/**< The set of backend servers */
	void 		*data;		/**< The session data */
	void		*router_session;/**< The router instance data */
	SESSION_STATS	stats;		/**< Session statistics */
	struct service	*service;	/**< The service this session is using */
	struct session	*next;		/**< Linked list of all sessions */
	int		refcount;	/**< Reference count on the session */
        skygw_chk_t     ses_chk_tail;
} SESSION;

#define SESSION_PROTOCOL(x, type)	DCB_PROTOCOL((x)->client, type)

extern SESSION	*session_alloc(struct service *, struct dcb *);
extern void	session_free(SESSION *);
extern void	printAllSessions();
extern void	printSession(SESSION *);
extern void	dprintAllSessions(struct dcb *);
extern void	dprintSession(struct dcb *, SESSION *);
extern char	*session_state(int);
#endif
