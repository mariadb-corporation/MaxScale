#pragma once
#ifndef _MAXSCALE_SESSION_H
#define _MAXSCALE_SESSION_H
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
 * 27/06/2016   Martin Brampton         Modify session struct for list manager
 *
 * @endverbatim
 */

#include <maxscale/cdefs.h>
#include <time.h>
#include <maxscale/atomic.h>
#include <maxscale/buffer.h>
#include <maxscale/listmanager.h>
#include <maxscale/spinlock.h>
#include <maxscale/resultset.h>
#include <maxscale/skygw_utils.h>
#include <maxscale/log_manager.h>

MXS_BEGIN_DECLS

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

#define SESSION_STATS_INIT {0}

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

#define DOWNSTREAM_INIT {0}

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

#define UPSTREAM_INIT {0}

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

#define SESSION_FILTER_INIT {0}

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
 * 
 * Note that the first few fields (up to and including "entry_is_ready") must
 * precisely match the LIST_ENTRY structure defined in the list manager.
 */
typedef struct session
{
    LIST_ENTRY_FIELDS
    skygw_chk_t     ses_chk_top;
    SPINLOCK        ses_lock;
    session_state_t state;            /*< Current descriptor state */
    size_t          ses_id;           /*< Unique session identifier */
    int             enabled_log_priorities; /*< Bitfield of enabled syslog priorities */
    struct dcb      *client_dcb;      /*< The client connection */
    void            *router_session;  /*< The router instance data */
    SESSION_STATS   stats;            /*< Session statistics */
    struct service  *service;         /*< The service this session is using */
    int             n_filters;        /*< Number of filter sessions */
    SESSION_FILTER  *filters;         /*< The filters in use within this session */
    DOWNSTREAM      head;             /*< Head of the filter chain */
    UPSTREAM        tail;             /*< The tail of the filter chain */
    int             refcount;         /*< Reference count on the session */
    bool            ses_is_child;     /*< this is a child session */
    skygw_chk_t     ses_chk_tail;
} SESSION;

#define SESSION_INIT {.ses_chk_top = CHK_NUM_SESSION, .ses_lock = SPINLOCK_INIT, \
    .stats = SESSION_STATS_INIT, .head = DOWNSTREAM_INIT, .tail = UPSTREAM_INIT, \
    .state = SESSION_STATE_ALLOC, .ses_chk_tail = CHK_NUM_SESSION}

/** Whether to do session timeout checks */
extern bool check_timeouts;

/** When the next timeout check is done. This is compared to hkheartbeat in
 * hk_heartbeat.h */
extern long next_timeout_check;

#define SESSION_PROTOCOL(x, type)       DCB_PROTOCOL((x)->client_dcb, type)

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

SESSION *session_alloc(struct service *, struct dcb *);
bool session_pre_alloc(int number);
SESSION *session_set_dummy(struct dcb *);
bool session_free(SESSION *);
int session_isvalid(SESSION *);
int session_reply(void *inst, void *session, GWBUF *data);
char *session_get_remote(SESSION *);
char *session_getUser(SESSION *);
void printAllSessions();
void printSession(SESSION *);
void dprintSessionList(DCB *pdcb);
void dprintAllSessions(struct dcb *);
void dprintSession(struct dcb *, SESSION *);
void dListSessions(struct dcb *);
char *session_state(session_state_t);
bool session_link_dcb(SESSION *, struct dcb *);
SESSION* get_session_by_router_ses(void* rses);
void session_enable_log_priority(SESSION* ses, int priority);
void session_disable_log_priority(SESSION* ses, int priority);
RESULTSET *sessionGetList(SESSIONLISTFILTER);
void process_idle_sessions();
void enable_session_timeouts();

MXS_END_DECLS

#endif
