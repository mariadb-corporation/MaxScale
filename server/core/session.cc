/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file session.c  - A representation of the session within the gateway.
 */
#include <maxscale/session.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <string>
#include <sstream>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/poll.h>
#include <maxscale/router.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>
#include <maxscale/utils.h>
#include <maxscale/json_api.h>
#include <maxscale/protocol/mysql.h>

#include "internal/dcb.h"
#include "internal/session.h"
#include "internal/filter.h"
#include "internal/worker.hh"
#include "internal/workertask.hh"

using std::string;
using std::stringstream;

/** Global session id counter. Must be updated atomically. Value 0 is reserved for
 *  dummy/unused sessions.
 */
static uint64_t next_session_id = 1;

static uint32_t retain_last_statements = 0;
static session_dump_statements_t dump_statements = SESSION_DUMP_STATEMENTS_NEVER;

static struct session session_dummy_struct;

static void session_initialize(void *session);
static int session_setup_filters(MXS_SESSION *session);
static void session_simple_free(MXS_SESSION *session, DCB *dcb);
static void session_add_to_all_list(MXS_SESSION *session);
static MXS_SESSION *session_find_free();
static void session_final_free(MXS_SESSION *session);
static MXS_SESSION* session_alloc_body(SERVICE* service, DCB* client_dcb,
                                       MXS_SESSION* session);

/**
 * The clientReply of the session.
 *
 * @param inst     In reality an MXS_SESSION*.
 * @param session  In reality an MXS_SESSION*.
 * @param data     The data to send to the client.
 */
static int session_reply(MXS_FILTER *inst, MXS_FILTER_SESSION *session, GWBUF *data);

/**
 * @brief Initialize a session
 *
 * This routine puts initial values into the fields of the session pointed to
 * by the parameter.
 *
 * @param *session    Pointer to the session to be initialized
 */
static void
session_initialize(MXS_SESSION *session)
{
    memset(session, 0, sizeof(MXS_SESSION));

    session->ses_chk_top = CHK_NUM_SESSION;
    session->state = SESSION_STATE_ALLOC;
    session->last_statements = new SessionStmtQueue;
    session->ses_chk_tail = CHK_NUM_SESSION;
}

MXS_SESSION* session_alloc(SERVICE *service, DCB *client_dcb)
{
    MXS_SESSION *session = (MXS_SESSION *)(MXS_MALLOC(sizeof(*session)));
    if (NULL == session)
    {
        return NULL;
    }

    session_initialize(session);
    session->ses_id = session_get_next_id();
    return session_alloc_body(service, client_dcb, session);
}

MXS_SESSION* session_alloc_with_id(SERVICE *service, DCB *client_dcb, uint64_t id)
{
    MXS_SESSION *session = (MXS_SESSION *)(MXS_MALLOC(sizeof(*session)));
    if (session == NULL)
    {
        return NULL;
    }

    session_initialize(session);
    session->ses_id = id;
    return session_alloc_body(service, client_dcb, session);
}

static MXS_SESSION* session_alloc_body(SERVICE* service, DCB* client_dcb,
                                       MXS_SESSION* session)
{
    session->service = service;
    session->client_dcb = client_dcb;
    session->stats.connect = time(0);
    session->stmt.buffer = NULL;
    session->stmt.target = NULL;
    session->qualifies_for_pooling = false;
    session->close_reason = SESSION_CLOSE_NONE;

    MXS_CONFIG *config = config_get_global_options();
    // If MaxScale is running in Oracle mode, then autocommit needs to
    // initially be off.
    bool autocommit = (config->qc_sql_mode == QC_SQL_MODE_ORACLE) ? false : true;
    session_set_autocommit(session, autocommit);

    /*<
     * Associate the session to the client DCB and set the reference count on
     * the session to indicate that there is a single reference to the
     * session. There is no need to protect this or use atomic add as the
     * session has not been made available to the other threads at this
     * point.
     */
    session->refcount = 1;
    /*<
     * This indicates that session is ready to be shared with backend
     * DCBs. Note that this doesn't mean that router is initialized yet!
     */
    session->state = SESSION_STATE_READY;

    session->trx_state = SESSION_TRX_INACTIVE;
    session->autocommit = true;
    /*
     * Only create a router session if we are not the listening DCB or an
     * internal DCB. Creating a router session may create a connection to
     * a backend server, depending upon the router module implementation
     * and should be avoided for a listener session.
     *
     * Router session creation may create other DCBs that link to the
     * session.
     */
    if (client_dcb->state != DCB_STATE_LISTENING &&
        client_dcb->dcb_role != DCB_ROLE_INTERNAL)
    {
        session->router_session = service->router->newSession(service->router_instance, session);
        if (session->router_session == NULL)
        {
            session->state = SESSION_STATE_TO_BE_FREED;
            MXS_ERROR("Failed to create new router session for service '%s'. "
                      "See previous errors for more details.", service->name);
        }
        /*
         * Pending filter chain being setup set the head of the chain to
         * be the router. As filters are inserted the current head will
         * be pushed to the filter and the head updated.
         *
         * NB This dictates that filters are created starting at the end
         * of the chain nearest the router working back to the client
         * protocol end of the chain.
         */
        // NOTE: Here we cast the router instance into a MXS_FILTER and
        // NOTE: the router session into a MXS_FILTER_SESSION and
        // NOTE: the router routeQuery into a filter routeQuery. That
        // NOTE: is in order to be able to treat the router as the first
        // NOTE: filter.
        session->head.instance = (MXS_FILTER*)service->router_instance;
        session->head.session = (MXS_FILTER_SESSION*)session->router_session;
        session->head.routeQuery =
            (int32_t (*)(MXS_FILTER*, MXS_FILTER_SESSION*, GWBUF*))service->router->routeQuery;

        // NOTE: Here we cast the session into a MXS_FILTER and MXS_FILTER_SESSION
        // NOTE: and session_reply into a filter clientReply. That's dubious but ok
        // NOTE: as session_reply will know what to do. In practice, the session
        // NOTE: will be called as if it would be the last filter.
        session->tail.instance = (MXS_FILTER*)session;
        session->tail.session = (MXS_FILTER_SESSION*)session;
        session->tail.clientReply = session_reply;

        if (SESSION_STATE_TO_BE_FREED != session->state
            && service->n_filters > 0
            && !session_setup_filters(session))
        {
            session->state = SESSION_STATE_TO_BE_FREED;
            MXS_ERROR("Setting up filters failed. "
                      "Terminating session %s.",
                      service->name);
        }
    }

    if (SESSION_STATE_TO_BE_FREED != session->state)
    {
        session->state = SESSION_STATE_ROUTER_READY;

        if (session->client_dcb->user == NULL)
        {
            MXS_INFO("Started session [%" PRIu64 "] for %s service ",
                     session->ses_id,
                     service->name);
        }
        else
        {
            MXS_INFO("Started %s client session [%" PRIu64 "] for '%s' from %s",
                     service->name,
                     session->ses_id,
                     session->client_dcb->user,
                     session->client_dcb->remote);
        }
    }
    else
    {
        MXS_INFO("Start %s client session [%" PRIu64 "] for '%s' from %s failed, will be "
                 "closed as soon as all related DCBs have been closed.",
                 service->name,
                 session->ses_id,
                 session->client_dcb->user,
                 session->client_dcb->remote);
    }
    atomic_add(&service->stats.n_sessions, 1);
    atomic_add(&service->stats.n_current, 1);
    CHK_SESSION(session);

    client_dcb->session = session;
    return (session->state == SESSION_STATE_TO_BE_FREED) ? NULL : session;
}

/**
 * Allocate a dummy session so that DCBs can always have sessions.
 *
 * Only one dummy session exists, it is statically declared
 *
 * @param client_dcb    The client side DCB
 * @return              The dummy created session
 */
MXS_SESSION *
session_set_dummy(DCB *client_dcb)
{
    MXS_SESSION *session;

    session = &session_dummy_struct;
    session->ses_chk_top = CHK_NUM_SESSION;
    session->ses_chk_tail = CHK_NUM_SESSION;
    session->service = NULL;
    session->client_dcb = NULL;
    session->n_filters = 0;
    memset(&session->stats, 0, sizeof(MXS_SESSION_STATS));
    session->stats.connect = 0;
    session->state = SESSION_STATE_DUMMY;
    session->refcount = 1;
    session->ses_id = 0;

    client_dcb->session = session;
    return session;
}

void session_link_backend_dcb(MXS_SESSION *session, DCB *dcb)
{
    ss_dassert(dcb->dcb_role == DCB_ROLE_BACKEND_HANDLER);

    atomic_add(&session->refcount, 1);
    dcb->session = session;
    dcb->service = session->service;
    /** Move this DCB under the same thread */
    dcb->poll.thread.id = session->client_dcb->poll.thread.id;
}

/**
 * Deallocate the specified session, minimal actions during session_alloc
 * Since changes to keep new session in existence until all related DCBs
 * have been destroyed, this function is redundant.  Just left until we are
 * sure of the direction taken.
 *
 * @param session       The session to deallocate
 */
static void
session_simple_free(MXS_SESSION *session, DCB *dcb)
{
    /* Does this possibly need a lock? */
    if (dcb->data)
    {
        void * clientdata = dcb->data;
        dcb->data = NULL;
        MXS_FREE(clientdata);
    }
    if (session)
    {
        if (SESSION_STATE_DUMMY == session->state)
        {
            return;
        }
        if (session && session->router_session)
        {
            session->service->router->freeSession(
                session->service->router_instance,
                session->router_session);
        }
        session->state = SESSION_STATE_STOPPING;
    }

    session_final_free(session);
}

void session_close(MXS_SESSION *session)
{
    if (session->router_session)
    {
        session->state = SESSION_STATE_STOPPING;

        MXS_ROUTER_OBJECT* router = session->service->router;
        MXS_ROUTER* router_instance = session->service->router_instance;

        /** Close router session and all its connections */
        router->closeSession(router_instance, session->router_session);
    }
}

/**
 * Deallocate the specified session
 *
 * @param session       The session to deallocate
 */
static void session_free(MXS_SESSION *session)
{
    CHK_SESSION(session);
    ss_dassert(session->refcount == 0);

    session->state = SESSION_STATE_TO_BE_FREED;
    atomic_add(&session->service->stats.n_current, -1);

    if (session->client_dcb)
    {
        dcb_free_all_memory(session->client_dcb);
        session->client_dcb = NULL;
    }
    /**
     * If session is not child of some other session, free router_session.
     * Otherwise let the parent free it.
     */
    if (session->router_session)
    {
        session->service->router->freeSession(session->service->router_instance,
                                              session->router_session);
    }
    if (session->n_filters)
    {
        int i;
        for (i = 0; i < session->n_filters; i++)
        {
            if (session->filters[i].filter)
            {
                session->filters[i].filter->obj->closeSession(session->filters[i].instance,
                                                              session->filters[i].session);
            }
        }
        for (i = 0; i < session->n_filters; i++)
        {
            if (session->filters[i].filter)
            {
                session->filters[i].filter->obj->freeSession(session->filters[i].instance,
                                                             session->filters[i].session);
            }
        }
        MXS_FREE(session->filters);
    }

    MXS_INFO("Stopped %s client session [%" PRIu64 "]", session->service->name, session->ses_id);

    session->state = SESSION_STATE_FREE;
    session_final_free(session);
}

static void
session_final_free(MXS_SESSION *session)
{
    if (dump_statements == SESSION_DUMP_STATEMENTS_ON_CLOSE)
    {
        session_dump_statements(session);
    }

    gwbuf_free(session->stmt.buffer);
    delete session->last_statements;
    MXS_FREE(session);
}

/**
 * Check to see if a session is valid, i.e. in the list of all sessions
 *
 * @param session       Session to check
 * @return              1 if the session is valid otherwise 0
 */
int
session_isvalid(MXS_SESSION *session)
{
    return session != NULL;
}

/**
 * Print details of an individual session
 *
 * @param session       Session to print
 */
void
printSession(MXS_SESSION *session)
{
    struct tm result;
    char timebuf[40];

    printf("Session %p\n", session);
    printf("\tState:        %s\n", session_state(session->state));
    printf("\tService:      %s (%p)\n", session->service->name, session->service);
    printf("\tClient DCB:   %p\n", session->client_dcb);
    printf("\tConnected:    %s\n",
           asctime_r(localtime_r(&session->stats.connect, &result), timebuf));
    printf("\tRouter Session: %p\n", session->router_session);
}

bool printAllSessions_cb(DCB *dcb, void *data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        printSession(dcb->session);
    }

    return true;
}

/**
 * Print all sessions
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 */
void
printAllSessions()
{
    dcb_foreach(printAllSessions_cb, NULL);
}

/** Callback for dprintAllSessions */
bool dprintAllSessions_cb(DCB *dcb, void *data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER &&
        dcb->session->state != SESSION_STATE_DUMMY)
    {
        DCB *out_dcb = (DCB*)data;
        dprintSession(out_dcb, dcb->session);
    }
    return true;
}

/**
 * Print all sessions to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 */
void
dprintAllSessions(DCB *dcb)
{
    dcb_foreach(dprintAllSessions_cb, dcb);
}

/**
 * Print a particular session to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 * @param print_session   The session to print
 */
void
dprintSession(DCB *dcb, MXS_SESSION *print_session)
{
    struct tm result;
    char buf[30];
    int i;

    dcb_printf(dcb, "Session %" PRIu64 "\n", print_session->ses_id);
    dcb_printf(dcb, "\tState:               %s\n", session_state(print_session->state));
    dcb_printf(dcb, "\tService:             %s\n", print_session->service->name);

    if (print_session->client_dcb && print_session->client_dcb->remote)
    {
        double idle = (hkheartbeat - print_session->client_dcb->last_read);
        idle = idle > 0 ? idle / 10.f : 0;
        dcb_printf(dcb, "\tClient Address:          %s%s%s\n",
                   print_session->client_dcb->user ? print_session->client_dcb->user : "",
                   print_session->client_dcb->user ? "@" : "",
                   print_session->client_dcb->remote);
        dcb_printf(dcb, "\tConnected:               %s\n",
                   asctime_r(localtime_r(&print_session->stats.connect, &result), buf));
        if (print_session->client_dcb->state == DCB_STATE_POLLING)
        {
            dcb_printf(dcb, "\tIdle:                %.0f seconds\n", idle);
        }

    }

    if (print_session->n_filters)
    {
        for (i = 0; i < print_session->n_filters; i++)
        {
            dcb_printf(dcb, "\tFilter: %s\n",
                       print_session->filters[i].filter->name);
            print_session->filters[i].filter->obj->diagnostics(print_session->filters[i].instance,
                                                               print_session->filters[i].session,
                                                               dcb);
        }
    }
}

bool dListSessions_cb(DCB *dcb, void *data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        DCB *out_dcb = (DCB*)data;
        MXS_SESSION *session = dcb->session;
        dcb_printf(out_dcb, "%-16" PRIu64 " | %-15s | %-14s | %s\n", session->ses_id,
                   session->client_dcb && session->client_dcb->remote ?
                   session->client_dcb->remote : "",
                   session->service && session->service->name ?
                   session->service->name : "",
                   session_state(session->state));
    }

    return true;
}
/**
 * List all sessions in tabular form to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb   The DCB to print to
 */
void
dListSessions(DCB *dcb)
{
    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
    dcb_printf(dcb, "Session          | Client          | Service        | State\n");
    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");

    dcb_foreach(dListSessions_cb, dcb);

    dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n\n");
}

/**
 * Convert a session state to a string representation
 *
 * @param state         The session state
 * @return A string representation of the session state
 */
const char *
session_state(mxs_session_state_t state)
{
    switch (state)
    {
    case SESSION_STATE_ALLOC:
        return "Session Allocated";
    case SESSION_STATE_DUMMY:
        return "Dummy Session";
    case SESSION_STATE_READY:
        return "Session Ready";
    case SESSION_STATE_ROUTER_READY:
        return "Session ready for routing";
    case SESSION_STATE_LISTENER:
        return "Listener Session";
    case SESSION_STATE_LISTENER_STOPPED:
        return "Stopped Listener Session";
    case SESSION_STATE_STOPPING:
        return "Stopping session";
    case SESSION_STATE_TO_BE_FREED:
        return "Session to be freed";
    case SESSION_STATE_FREE:
        return "Freed session";
    default:
        return "Invalid State";
    }
}

/**
 * Create the filter chain for this session.
 *
 * Filters must be setup in reverse order, starting with the last
 * filter in the chain and working back towards the client connection
 * Each filter is passed the current session head of the filter chain
 * this head becomes the destination for the filter. The newly created
 * filter becomes the new head of the filter chain.
 *
 * @param       session         The session that requires the chain
 * @return      0 if filter creation fails
 */
static int
session_setup_filters(MXS_SESSION *session)
{
    SERVICE *service = session->service;
    MXS_DOWNSTREAM *head;
    MXS_UPSTREAM *tail;
    int i;

    if ((session->filters = (SESSION_FILTER*)MXS_CALLOC(service->n_filters,
                                                        sizeof(SESSION_FILTER))) == NULL)
    {
        return 0;
    }
    session->n_filters = service->n_filters;
    for (i = service->n_filters - 1; i >= 0; i--)
    {
        if (service->filters[i] == NULL)
        {
            MXS_ERROR("Service '%s' contians an unresolved filter.", service->name);
            return 0;
        }
        if ((head = filter_apply(service->filters[i], session,
                                 &session->head)) == NULL)
        {
            MXS_ERROR("Failed to create filter '%s' for "
                      "service '%s'.\n",
                      service->filters[i]->name,
                      service->name);
            return 0;
        }
        session->filters[i].filter = service->filters[i];
        session->filters[i].session = head->session;
        session->filters[i].instance = head->instance;
        session->head = *head;
        MXS_FREE(head);
    }

    for (i = 0; i < service->n_filters; i++)
    {
        if ((tail = filter_upstream(service->filters[i],
                                    session->filters[i].session,
                                    &session->tail)) == NULL)
        {
            MXS_ERROR("Failed to create filter '%s' for service '%s'.",
                      service->filters[i]->name,
                      service->name);
            return 0;
        }

        /*
         * filter_upstream may simply return the 3 parameter if
         * the filter has no upstream entry point. So no need
         * to copy the contents or free tail in this case.
         */
        if (tail != &session->tail)
        {
            session->tail = *tail;
            MXS_FREE(tail);
        }
    }

    return 1;
}

/**
 * Entry point for the final element in the upstream filter, i.e. the writing
 * of the data to the client.
 *
 * Looks like a filter `clientReply`, but in this case both the instance and
 * the session argument will be a MXS_SESSION*.
 *
 * @param       instance        The "instance" data
 * @param       session         The session
 * @param       data            The buffer chain to write
 */
int
session_reply(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *data)
{
    MXS_SESSION *the_session = (MXS_SESSION*)session;

    return the_session->client_dcb->func.write(the_session->client_dcb, data);
}

/**
 * Return the client connection address or name
 *
 * @param session       The session whose client address to return
 */
const char *
session_get_remote(const MXS_SESSION *session)
{
    if (session && session->client_dcb)
    {
        return session->client_dcb->remote;
    }
    return NULL;
}

bool session_route_query(MXS_SESSION* ses, GWBUF* buf)
{
    bool succp;

    if (ses->head.routeQuery == NULL ||
        ses->head.instance == NULL ||
        ses->head.session == NULL)
    {
        succp = false;
        goto return_succp;
    }

    if (ses->head.routeQuery(ses->head.instance, ses->head.session, buf) == 1)
    {
        succp = true;
    }
    else
    {
        succp = false;
    }
return_succp:
    return succp;
}


/**
 * Return the username of the user connected to the client side of the
 * session.
 *
 * @param session               The session pointer.
 * @return      The user name or NULL if it can not be determined.
 */
const char *
session_get_user(const MXS_SESSION *session)
{
    return (session && session->client_dcb) ? session->client_dcb->user : NULL;
}

/**
 * Callback structure for the session list extraction
 */
typedef struct
{
    int index;
    int current;
    SESSIONLISTFILTER filter;
    RESULT_ROW *row;
    RESULTSET *set;
} SESSIONFILTER;

bool dcb_iter_cb(DCB *dcb, void *data)
{
    SESSIONFILTER *cbdata = (SESSIONFILTER*)data;

    if (cbdata->current < cbdata->index)
    {
        if (cbdata->filter == SESSION_LIST_ALL ||
            (cbdata->filter == SESSION_LIST_CONNECTION &&
             (dcb->session->state != SESSION_STATE_LISTENER)))
        {
            cbdata->current++;
        }
    }
    else
    {
        char buf[20];
        MXS_SESSION *list_session = dcb->session;

        cbdata->index++;
        cbdata->row = resultset_make_row(cbdata->set);
        snprintf(buf, sizeof(buf), "%p", list_session);
        resultset_row_set(cbdata->row, 0, buf);
        resultset_row_set(cbdata->row, 1, ((list_session->client_dcb && list_session->client_dcb->remote)
                                           ? list_session->client_dcb->remote : ""));
        resultset_row_set(cbdata->row, 2, (list_session->service && list_session->service->name
                                           ? list_session->service->name : ""));
        resultset_row_set(cbdata->row, 3, session_state(list_session->state));
        return false;
    }
    return true;
}

/**
 * Provide a row to the result set that defines the set of sessions
 *
 * @param set   The result set
 * @param data  The index of the row to send
 * @return The next row or NULL
 */
static RESULT_ROW *
sessionRowCallback(RESULTSET *set, void *data)
{
    SESSIONFILTER *cbdata = (SESSIONFILTER*)data;
    RESULT_ROW *row = NULL;

    cbdata->current = 0;
    dcb_foreach(dcb_iter_cb, cbdata);

    if (cbdata->row)
    {
        row = cbdata->row;
        cbdata->row = NULL;
    }
    else
    {
        MXS_FREE(cbdata);
    }

    return row;
}

/**
 * Return a resultset that has the current set of sessions in it
 *
 * @return A Result set
 */
/* Lint is not convinced that the new memory for data is always tracked
 * because it does not see what happens within the resultset_create function,
 * so we suppress the warning. In fact, the function call results in return
 * of the set structure which includes a pointer to data
 */

/*lint -e429 */
RESULTSET *
sessionGetList(SESSIONLISTFILTER filter)
{
    RESULTSET *set;
    SESSIONFILTER *data;

    if ((data = (SESSIONFILTER *)MXS_MALLOC(sizeof(SESSIONFILTER))) == NULL)
    {
        return NULL;
    }
    data->index = 0;
    data->filter = filter;
    data->current = 0;
    data->row = NULL;

    if ((set = resultset_create(sessionRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }

    data->set = set;
    resultset_add_column(set, "Session", 16, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Client", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Service", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "State", 15, COL_TYPE_VARCHAR);

    return set;
}
/*lint +e429 */

mxs_session_trx_state_t session_get_trx_state(const MXS_SESSION* ses)
{
    return ses->trx_state;
}

mxs_session_trx_state_t session_set_trx_state(MXS_SESSION* ses, mxs_session_trx_state_t new_state)
{
    mxs_session_trx_state_t prev_state = ses->trx_state;

    ses->trx_state = new_state;

    return prev_state;
}

const char* session_trx_state_to_string(mxs_session_trx_state_t state)
{
    switch (state)
    {
    case SESSION_TRX_INACTIVE:
        return "SESSION_TRX_INACTIVE";
    case SESSION_TRX_ACTIVE:
        return "SESSION_TRX_ACTIVE";
    case SESSION_TRX_READ_ONLY:
        return "SESSION_TRX_READ_ONLY";
    case SESSION_TRX_READ_WRITE:
        return "SESSION_TRX_READ_WRITE";
    case SESSION_TRX_READ_ONLY_ENDING:
        return "SESSION_TRX_READ_ONLY_ENDING";
    case SESSION_TRX_READ_WRITE_ENDING:
        return "SESSION_TRX_READ_WRITE_ENDING";
    }

    MXS_ERROR("Unknown session_trx_state_t value: %d", (int)state);
    return "UNKNOWN";
}

static bool ses_find_id(DCB *dcb, void *data)
{
    void **params = (void**)data;
    MXS_SESSION **ses = (MXS_SESSION**)params[0];
    uint64_t *id = (uint64_t*)params[1];
    bool rval = true;

    if (dcb->session->ses_id == *id)
    {
        *ses = session_get_ref(dcb->session);
        rval = false;
    }

    return rval;
}

MXS_SESSION* session_get_by_id(uint64_t id)
{
    MXS_SESSION *session = NULL;
    void *params[] = {&session, &id};

    dcb_foreach(ses_find_id, params);

    return session;
}

MXS_SESSION* session_get_ref(MXS_SESSION *session)
{
    atomic_add(&session->refcount, 1);
    return session;
}

void session_put_ref(MXS_SESSION *session)
{
    if (session && session->state != SESSION_STATE_DUMMY)
    {
        /** Remove one reference. If there are no references left, free session */
        if (atomic_add(&session->refcount, -1) == 1)
        {
            session_free(session);
        }
    }
}

bool session_store_stmt(MXS_SESSION *session, GWBUF *buf, const SERVER *server)
{
    bool rval = false;

    if (session->stmt.buffer)
    {
        /** This should not happen with proper use */
        ss_dassert(false);
        gwbuf_free(session->stmt.buffer);
    }

    if ((session->stmt.buffer = gwbuf_clone(buf)))
    {
        session->stmt.target = server;
        /** No old statements were stored and we successfully cloned the buffer */
        rval = true;
    }

    return rval;
}

bool session_take_stmt(MXS_SESSION *session, GWBUF **buffer, const SERVER **target)
{
    bool rval = false;

    if (session->stmt.buffer && session->stmt.target)
    {
        *buffer = session->stmt.buffer;
        *target = session->stmt.target;
        session->stmt.buffer = NULL;
        session->stmt.target = NULL;
        rval = true;
    }

    return rval;
}

void session_clear_stmt(MXS_SESSION *session)
{
    gwbuf_free(session->stmt.buffer);
    session->stmt.buffer = NULL;
    session->stmt.target = NULL;
}

uint64_t session_get_next_id()
{
    return atomic_add_uint64(&next_session_id, 1);
}

json_t* session_json_data(const MXS_SESSION *session, const char *host)
{
    json_t* data = json_object();

    /** ID must be a string */
    stringstream ss;
    ss << session->ses_id;

    /** ID and type */
    json_object_set_new(data, CN_ID, json_string(ss.str().c_str()));
    json_object_set_new(data, CN_TYPE, json_string(CN_SESSIONS));

    /** Relationships */
    json_t* rel = json_object();

    /** Service relationship (one-to-one) */
    json_t* services = mxs_json_relationship(host, MXS_JSON_API_SERVICES);
    mxs_json_add_relation(services, session->service->name, CN_SERVICES);
    json_object_set_new(rel, CN_SERVICES, services);

    /** Filter relationships (one-to-many) */
    if (session->n_filters)
    {
        json_t* filters = mxs_json_relationship(host, MXS_JSON_API_FILTERS);

        for (int i = 0; i < session->n_filters; i++)
        {
            mxs_json_add_relation(filters, session->filters[i].filter->name, CN_FILTERS);
        }
        json_object_set_new(rel, CN_FILTERS, filters);
    }

    json_object_set_new(data, CN_RELATIONSHIPS, rel);

    /** Session attributes */
    json_t* attr = json_object();
    json_object_set_new(attr, "state", json_string(session_state(session->state)));

    if (session->client_dcb->user)
    {
        json_object_set_new(attr, CN_USER, json_string(session->client_dcb->user));
    }

    if (session->client_dcb->remote)
    {
        json_object_set_new(attr, "remote", json_string(session->client_dcb->remote));
    }

    struct tm result;
    char buf[60];

    asctime_r(localtime_r(&session->stats.connect, &result), buf);
    trim(buf);

    json_object_set_new(attr, "connected", json_string(buf));

    if (session->client_dcb->state == DCB_STATE_POLLING)
    {
        double idle = (hkheartbeat - session->client_dcb->last_read);
        idle = idle > 0 ? idle / 10.f : 0;
        json_object_set_new(attr, "idle", json_real(idle));
    }

    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_LINKS, mxs_json_self_link(host, CN_SESSIONS, ss.str().c_str()));

    return data;
}

json_t* session_to_json(const MXS_SESSION *session, const char *host)
{
    stringstream ss;
    ss << MXS_JSON_API_SESSIONS << session->ses_id;
    return mxs_json_resource(host, ss.str().c_str(), session_json_data(session, host));
}

struct SessionListData
{
    json_t* json;
    const char* host;
};

bool seslist_cb(DCB* dcb, void* data)
{
    if (dcb->dcb_role == DCB_ROLE_CLIENT_HANDLER)
    {
        SessionListData* d = (SessionListData*)data;
        json_array_append_new(d->json, session_json_data(dcb->session, d->host));
    }

    return true;
}

json_t* session_list_to_json(const char* host)
{
    SessionListData data = {json_array(), host};
    dcb_foreach(seslist_cb, &data);
    return mxs_json_resource(host, MXS_JSON_API_SESSIONS, data.json);
}

void session_qualify_for_pool(MXS_SESSION* session)
{
    session->qualifies_for_pooling = true;
}

bool session_valid_for_pool(const MXS_SESSION* session)
{
    ss_dassert(session->state != SESSION_STATE_DUMMY);
    return session->qualifies_for_pooling;
}

MXS_SESSION* session_get_current()
{
    DCB* dcb = dcb_get_current();

    return dcb ? dcb->session : NULL;
}

uint64_t session_get_current_id()
{
    MXS_SESSION* session = session_get_current();

    return session ? session->ses_id : 0;
}

void session_set_retain_last_statements(uint32_t n)
{
    retain_last_statements = n;
}

void session_set_dump_statements(session_dump_statements_t value)
{
    dump_statements = value;
}

session_dump_statements_t session_get_dump_statements()
{
    return dump_statements;
}

void session_retain_statement(MXS_SESSION* pSession, GWBUF* pBuffer)
{
    if (retain_last_statements)
    {
        size_t len = gwbuf_length(pBuffer);

        if (len > MYSQL_HEADER_LEN)
        {
            uint8_t header[MYSQL_HEADER_LEN + 1];
            uint8_t* pHeader = NULL;

            if (GWBUF_LENGTH(pBuffer) > MYSQL_HEADER_LEN)
            {
                pHeader = GWBUF_DATA(pBuffer);
            }
            else
            {
                gwbuf_copy_data(pBuffer, 0, MYSQL_HEADER_LEN + 1, header);
                pHeader = header;
            }

            if (MYSQL_GET_COMMAND(pHeader) == MXS_COM_QUERY)
            {
                ss_dassert(pSession->last_statements->size() <= retain_last_statements);

                if (pSession->last_statements->size() == retain_last_statements)
                {
                    pSession->last_statements->pop_back();
                }

                std::vector<uint8_t> stmt(len - MYSQL_HEADER_LEN - 1);
                gwbuf_copy_data(pBuffer, MYSQL_HEADER_LEN + 1, len - (MYSQL_HEADER_LEN + 1), &stmt.front());

                pSession->last_statements->push_front(stmt);
            }
        }
    }
}

void session_dump_statements(MXS_SESSION* pSession)
{
    if (retain_last_statements)
    {
        int n = pSession->last_statements->size();

        uint64_t id = session_get_current_id();

        if ((id != 0) && (id != pSession->ses_id))
        {
            MXS_WARNING("Current session is %" PRIu64 ", yet statements are dumped for %" PRIu64 ". "
                        "The session id in the subsequent dumped statements is the wrong one.",
                        id, pSession->ses_id);
        }

        for (auto i = pSession->last_statements->rbegin(); i != pSession->last_statements->rend(); ++i)
        {
            int len = i->size();
            const char* pStmt = (char*) &i->front();

            if (id != 0)
            {
                MXS_NOTICE("Stmt %d: %.*s", n, len, pStmt);
            }
            else
            {
                // We are in a context where we do not have a current session, so we need to
                // log the session id ourselves.

                MXS_NOTICE("(%" PRIu64 ") Stmt %d: %.*s", pSession->ses_id, n, len, pStmt);
            }

            --n;
        }
    }
}

const char* session_get_close_reason(const MXS_SESSION* session)
{
    switch (session->close_reason)
    {
        case SESSION_CLOSE_NONE:
            return "";

        case SESSION_CLOSE_TIMEOUT:
            return "Timed out by MaxScale";

        case SESSION_CLOSE_HANDLEERROR_FAILED:
            return "Router could not recover from connection errors";

        case SESSION_CLOSE_ROUTING_FAILED:
            return "Router could not route query";

        case SESSION_CLOSE_KILLED:
            return "Killed by another connection";

        case SESSION_CLOSE_TOO_MANY_CONNECTIONS:
            return "Too many connections";

        default:
            ss_dassert(!true);
            return "Internal error";
    }
}
