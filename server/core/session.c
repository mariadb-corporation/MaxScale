/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file session.c  - A representation of the session within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date         Who                     Description
 * 17/06/13     Mark Riddoch            Initial implementation
 * 02/09/13     Massimiliano Pinto      Added session refcounter
 * 29/05/14     Mark Riddoch            Addition of filter mechanism
 * 23/08/15     Martin Brampton         Tidying; slight improvement in safety
 * 17/09/15     Martin Brampton         Keep failed session in existence - leave DCBs to close
 * 27/06/16     Martin Brampton         Amend to utilise list manager
 *
 * @endverbatim
 */
#include <maxscale/session.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/dcb.h>
#include <maxscale/housekeeper.h>
#include <maxscale/log_manager.h>
#include <maxscale/poll.h>
#include <maxscale/router.h>
#include <maxscale/service.h>
#include <maxscale/spinlock.h>

#include "maxscale/session.h"
#include "maxscale/filter.h"

/* A session with null values, used for initialization */
static MXS_SESSION session_initialized = SESSION_INIT;

/** Global session id; updated safely by use of atomic_add */
static int session_id;

static struct session session_dummy_struct;

static void session_initialize(void *session);
static int session_setup_filters(MXS_SESSION *session);
static void session_simple_free(MXS_SESSION *session, DCB *dcb);
static void session_add_to_all_list(MXS_SESSION *session);
static MXS_SESSION *session_find_free();
static void session_final_free(MXS_SESSION *session);

/**
 * @brief Initialize a session
 *
 * This routine puts initial values into the fields of the session pointed to
 * by the parameter. The parameter has to be passed as void * because the
 * function can be called by the generic list manager, which does not know
 * the actual type of the list entries it handles.
 *
 * All fields can be initialized by the assignment of the static
 * initialized session.
 *
 * @param *session    Pointer to the session to be initialized
 */
static void
session_initialize(void *session)
{
    *(MXS_SESSION *)session = session_initialized;
}

/**
 * Allocate a new session for a new client of the specified service.
 *
 * Create the link to the router session by calling the newSession
 * entry point of the router using the router instance of the
 * service this session is part of.
 *
 * @param service       The service this connection was established by
 * @param client_dcb    The client side DCB
 * @return              The newly created session or NULL if an error occured
 */
MXS_SESSION *
session_alloc(SERVICE *service, DCB *client_dcb)
{
    MXS_SESSION *session = (MXS_SESSION *)(MXS_MALLOC(sizeof(*session)));

    if (NULL == session)
    {
        return NULL;
    }
    session_initialize(session);

    /** Assign a session id and increase */
    session->ses_id = (size_t)atomic_add(&session_id, 1) + 1;
    session->ses_is_child = (bool) DCB_IS_CLONE(client_dcb);
    session->service = service;
    session->client_dcb = client_dcb;
    session->stats.connect = time(0);
    session->stmt.buffer = NULL;
    session->stmt.target = NULL;
    session->qualifies_for_pooling = false;
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
     * Only create a router session if we are not the listening
     * DCB or an internal DCB. Creating a router session may create a connection to a
     * backend server, depending upon the router module implementation
     * and should be avoided for the listener session
     *
     * Router session creation may create other DCBs that link to the
     * session, therefore it is important that the session lock is
     * relinquished before the router call.
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
        session->head.instance = service->router_instance;
        session->head.session = session->router_session;

        session->head.routeQuery = (void *)(service->router->routeQuery);

        session->tail.instance = session;
        session->tail.session = session;
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
            MXS_INFO("Started session [%lu] for %s service ",
                     session->ses_id,
                     service->name);
        }
        else
        {
            MXS_INFO("Started %s client session [%lu] for '%s' from %s",
                     service->name,
                     session->ses_id,
                     session->client_dcb->user,
                     session->client_dcb->remote);
        }
    }
    else
    {
        MXS_INFO("Start %s client session [%lu] for '%s' from %s failed, will be "
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
    return SESSION_STATE_TO_BE_FREED == session->state ? NULL : session;
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
    session->ses_is_child = false;
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

/**
 * Link a session to a DCB.
 *
 * @param session       The session to link with the dcb
 * @param dcb           The DCB to be linked
 * @return              True if the session was sucessfully linked to the DCB
 */
bool
session_link_dcb(MXS_SESSION *session, DCB *dcb)
{
    ss_info_dassert(session->state != SESSION_STATE_FREE,
                    "If session->state is SESSION_STATE_FREE then this attempt to "
                    "access freed memory block.");
    if (session->state == SESSION_STATE_FREE)
    {
        return false;
    }
    atomic_add(&session->refcount, 1);
    dcb->session = session;
    /** Move this DCB under the same thread */
    dcb->thread.id = session->client_dcb->thread.id;
    return true;
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
    if (dcb->data && !DCB_IS_CLONE(dcb))
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
    }
    /**
     * If session is not child of some other session, free router_session.
     * Otherwise let the parent free it.
     */
    if (!session->ses_is_child && session->router_session)
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

    MXS_INFO("Stopped %s client session [%lu]", session->service->name, session->ses_id);

    /** If session doesn't have parent referencing to it, it can be freed */
    if (!session->ses_is_child)
    {
        session->state = SESSION_STATE_FREE;
        session_final_free(session);
    }
}

static void
session_final_free(MXS_SESSION *session)
{
    gwbuf_free(session->stmt.buffer);
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

    dcb_printf(dcb, "Session %lu\n", print_session->ses_id);
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
        dcb_printf(out_dcb, "%-16lu | %-15s | %-14s | %s\n", session->ses_id,
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
char *
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

    if ((session->filters = MXS_CALLOC(service->n_filters,
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
 * @param       instance        The "instance" data
 * @param       session         The session
 * @param       data            The buffer chain to write
 */
int
session_reply(void *instance, void *session, GWBUF *data)
{
    MXS_SESSION *the_session = (MXS_SESSION *)session;

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
    int *id = (int*)params[1];
    bool rval = true;

    if (dcb->session->ses_id == *id)
    {
        *ses = session_get_ref(dcb->session);
        rval = false;
    }

    return rval;
}

MXS_SESSION* session_get_by_id(int id)
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
