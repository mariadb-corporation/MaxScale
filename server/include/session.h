#ifndef _SESSION_H
#define _SESSION_H
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
 * @file session.h
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 01-06-2013   Mark Riddoch            Initial implementation
 * 14-06-2013   Massimiliano Pinto      Added void *data to session
 *                                      for session specific data
 * 01-07-2013   Massimiliano Pinto      Removed backends pointer
 *                                      from struct session
 * 02-09-2013   Massimiliano Pinto      Added session ref counter
 * 29-05-2014   Mark Riddoch            Support for filter mechanism
 *                                      added
 * 20-02-2015   Markus Mäkelä           Added session timeouts
 *
 * @endverbatim
 */
#include <time.h>
#include <atomic.h>
#include <buffer.h>
#include <spinlock.h>
#include <resultset.h>
#include <skygw_utils.h>
#include <log_manager.h>

struct dcb;
struct service;
struct filter_def;

/**
 * The session statistics structure
 */
typedef struct
{
    time_t          connect;        /**< Time when the session was started */
} SESSION_STATS;

typedef enum
{
    SESSION_STATE_ALLOC,            /*< for all sessions */
    SESSION_STATE_READY,            /*< for router session */
    SESSION_STATE_ROUTER_READY,     /*< for router session */
    SESSION_STATE_STOPPING,         /*< session and router are being closed */
    SESSION_STATE_LISTENER,         /*< for listener session */
    SESSION_STATE_LISTENER_STOPPED, /*< for listener session */
    SESSION_STATE_TO_BE_FREED,      /*< ready to be freed as soon as there are no references */
    SESSION_STATE_FREE,             /*< for all sessions */
    SESSION_STATE_DUMMY             /*< dummy session for consistency */
} session_state_t;

/**
 * The downstream element in the filter chain. This may refer to
 * another filter or to a router.
 */
typedef struct
{
    void *instance;
    void *session;
    int (*routeQuery)(void *instance, void *session, GWBUF *request);
} DOWNSTREAM;

/**
 * The upstream element in the filter chain. This may refer to
 * another filter or to the protocol implementation.
 */
typedef struct
{
    void *instance;
    void *session;
    int (*clientReply)(void *instance, void *session, GWBUF *response);
    int (*error)(void *instance, void *session, void *);
} UPSTREAM;

/**
 * Structure used to track the filter instances and sessions of the filters
 * that are in use within a session.
 */
typedef struct
{
    struct filter_def *filter;
    void *instance;
    void *session;
} SESSION_FILTER;

/**
 * Filter type for the sessionGetList call
 */
typedef enum
{
    SESSION_LIST_ALL,
    SESSION_LIST_CONNECTION
} SESSIONLISTFILTER;

/**
 * The session status block
 *
 * A session status block is created for each user (client) connection
 * to the database, it links the descriptors, routing implementation
 * and originating service together for the client session.
 */
typedef struct session
{
#if defined(SS_DEBUG)
    skygw_chk_t     ses_chk_top;
#endif
    SPINLOCK        ses_lock;
    session_state_t state;            /*< Current descriptor state */
    size_t          ses_id;           /*< Unique session identifier */
    int             enabled_log_priorities; /*< Bitfield of enabled syslog priorities */
    struct dcb      *client;          /*< The client connection */
    void            *data;            /*< The session data */
    void            *router_session;  /*< The router instance data */
    SESSION_STATS   stats;            /*< Session statistics */
    struct service  *service;         /*< The service this session is using */
    int             n_filters;        /*< Number of filter sessions */
    SESSION_FILTER  *filters;         /*< The filters in use within this session */
    DOWNSTREAM      head;             /*< Head of the filter chain */
    UPSTREAM        tail;             /*< The tail of the filter chain */
    struct session  *next;            /*< Linked list of all sessions */
    int             refcount;         /*< Reference count on the session */
    bool            ses_is_child;     /*< this is a child session */
#if defined(SS_DEBUG)
    skygw_chk_t     ses_chk_tail;
#endif
} SESSION;

#define SESSION_PROTOCOL(x, type)       DCB_PROTOCOL((x)->client, type)

/**
 * A convenience macro that can be used by the protocol modules to route
 * the incoming data to the first element in the pipeline of filters and
 * routers.
 */
#define SESSION_ROUTE_QUERY(sess, buf)                          \
    ((sess)->head.routeQuery)((sess)->head.instance,            \
                              (sess)->head.session, (buf))
/**
 * A convenience macro that can be used by the router modules to route
 * the replies to the first element in the pipeline of filters and
 * the protocol.
 */
#define SESSION_ROUTE_REPLY(sess, buf)                          \
    ((sess)->tail.clientReply)((sess)->tail.instance,           \
                               (sess)->tail.session, (buf))

SESSION *get_all_sessions();
SESSION *session_alloc(struct service *, struct dcb *);
SESSION *session_set_dummy(struct dcb *);
bool session_free(SESSION *);
int session_isvalid(SESSION *);
int session_reply(void *inst, void *session, GWBUF *data);
char *session_get_remote(SESSION *);
char *session_getUser(SESSION *);
void printAllSessions();
void printSession(SESSION *);
void dprintAllSessions(struct dcb *);
void dprintSession(struct dcb *, SESSION *);
void dListSessions(struct dcb *);
char *session_state(int);
bool session_link_dcb(SESSION *, struct dcb *);
int session_unlink_dcb(SESSION*, DCB*);
SESSION* get_session_by_router_ses(void* rses);
void session_enable_log_priority(SESSION* ses, int priority);
void session_disable_log_priority(SESSION* ses, int priority);
void session_close_timeouts(void* data);
RESULTSET *sessionGetList(SESSIONLISTFILTER);

#endif
