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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <maxscale/alloc.h>
#include <maxscale/session.h>
#include <maxscale/listmanager.h>
#include <maxscale/service.h>
#include <maxscale/router.h>
#include <maxscale/dcb.h>
#include <maxscale/spinlock.h>
#include <maxscale/atomic.h>
#include <maxscale/log_manager.h>
#include <maxscale/housekeeper.h>
#include <maxscale/poll.h>

/* This list of all sessions */
LIST_CONFIG SESSIONlist =
{LIST_TYPE_RECYCLABLE, sizeof(SESSION), SPINLOCK_INIT};

/* A session with null values, used for initialization */
static SESSION session_initialized = SESSION_INIT;

/** Global session id; updated safely by use of atomic_add */
static int session_id;

static struct session session_dummy_struct;

/**
 * These two are declared in session.h
 */
bool check_timeouts = false;
long next_timeout_check = 0;

static SPINLOCK timeout_lock = SPINLOCK_INIT;

static void session_initialize(void *session);
static int session_setup_filters(SESSION *session);
static void session_simple_free(SESSION *session, DCB *dcb);
static void session_add_to_all_list(SESSION *session);
static SESSION *session_find_free();
static void session_final_free(SESSION *session);
static list_entry_t *skip_maybe_to_next_non_listener(list_entry_t *current, SESSIONLISTFILTER filter);

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
    *(SESSION *)session = session_initialized;
}

/*
 * @brief Pre-allocate memory for a number of sessions
 *
 * @param   The number of sessions to be pre-allocated
 */
bool
session_pre_alloc(int number)
{
    return list_pre_alloc(&SESSIONlist, number, session_initialize);
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
SESSION *
session_alloc(SERVICE *service, DCB *client_dcb)
{
    SESSION *session;

    session = (SESSION *)list_find_free(&SESSIONlist, session_initialize);
    ss_info_dassert(session != NULL, "Allocating memory for session failed.");
    if (NULL == session)
    {
        MXS_OOM();
        return NULL;
    }
    /** Assign a session id and increase */
    session->ses_id = (size_t)atomic_add(&session_id, 1) + 1;
    session->ses_is_child = (bool) DCB_IS_CLONE(client_dcb);
    session->service = service;
    session->client_dcb = client_dcb;
    session->stats.connect = time(0);
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
    session->entry_is_ready = true;
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
SESSION *
session_set_dummy(DCB *client_dcb)
{
    SESSION *session;

    session = &session_dummy_struct;
    session->list_entry_chk_top = CHK_NUM_MANAGED_LIST;
    session->list_entry_chk_tail = CHK_NUM_MANAGED_LIST;
    session->ses_chk_top = CHK_NUM_SESSION;
    session->ses_chk_tail = CHK_NUM_SESSION;
    session->ses_is_child = false;
    spinlock_init(&session->ses_lock);
    session->service = NULL;
    session->client_dcb = NULL;
    session->n_filters = 0;
    memset(&session->stats, 0, sizeof(SESSION_STATS));
    session->stats.connect = 0;
    session->state = SESSION_STATE_DUMMY;
    session->refcount = 1;
    session->ses_id = 0;
    session->next = NULL;

    client_dcb->session = session;
    return session;
}

/**
 * Enable specified log priority for the current session and increase logger
 * counter.
 * Generic logging setting has precedence over session-specific setting.
 *
 * @param session      session
 * @param priority syslog priority
 */
void session_enable_log_priority(SESSION* session, int priority)
{
    session->enabled_log_priorities |= (1 << priority);
    atomic_add((int *)&mxs_log_session_count[priority], 1);
}

/**
 * Disable specified log priority for the current session and decrease logger
 * counter.
 * Generic logging setting has precedence over session-specific setting.
 *
 * @param session   session
 * @param priority syslog priority
 */
void session_disable_log_priority(SESSION* session, int priority)
{
    if (session->enabled_log_priorities & (1 << priority))
    {
        session->enabled_log_priorities &= ~(1 << priority);
        atomic_add((int *)&mxs_log_session_count[priority], -1);
    }
}

/**
 * Link a session to a DCB.
 *
 * @param session       The session to link with the dcb
 * @param dcb           The DCB to be linked
 * @return              True if the session was sucessfully linked to the DCB
 */
bool
session_link_dcb(SESSION *session, DCB *dcb)
{
    spinlock_acquire(&session->ses_lock);
    ss_info_dassert(session->state != SESSION_STATE_FREE,
                    "If session->state is SESSION_STATE_FREE then this attempt to "
                    "access freed memory block.");
    if (session->state == SESSION_STATE_FREE)
    {
        spinlock_release(&session->ses_lock);
        return false;
    }
    atomic_add(&session->refcount, 1);
    dcb->session = session;
    spinlock_release(&session->ses_lock);
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
session_simple_free(SESSION *session, DCB *dcb)
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
bool
session_free(SESSION *session)
{
    if (NULL == session || SESSION_STATE_DUMMY == session->state)
    {
        return true;
    }
    CHK_SESSION(session);

    /*
     * Remove one reference. If there are no references left,
     * free session.
     */
    if (atomic_add(&session->refcount, -1) > 1)
    {
        /* Must be one or more references left */
        return false;
    }
    session->state = SESSION_STATE_TO_BE_FREED;

    atomic_add(&session->service->stats.n_current, -1);

    /***
     *
     */
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

    MXS_INFO("Stopped %s client session [%lu]",
             session->service->name,
             session->ses_id);

    /** Disable trace and decrease trace logger counter */
    session_disable_log_priority(session, LOG_INFO);

    /** If session doesn't have parent referencing to it, it can be freed */
    if (!session->ses_is_child)
    {
        session->state = SESSION_STATE_FREE;
        session_final_free(session);
    }
    return true;
}

static void
session_final_free(SESSION *session)
{
    /* We never free the actual session, it is available for reuse*/
    list_free_entry(&SESSIONlist, (list_entry_t *)session);
}

/**
 * Check to see if a session is valid, i.e. in the list of all sessions
 *
 * @param session       Session to check
 * @return              1 if the session is valid otherwise 0
 */
int
session_isvalid(SESSION *session)
{
    int rval = 0;
    list_entry_t *current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        if ((SESSION *)current == session)
        {
            rval = 1;
            list_terminate_iteration_early(&SESSIONlist, current);
            break;
        }
        current = list_iterate(&SESSIONlist, current);
    }

    return rval;
}

/**
 * Print details of an individual session
 *
 * @param session       Session to print
 */
void
printSession(SESSION *session)
{
    struct tm result;
    char timebuf[40];

    printf("Session %p\n", session);
    printf("\tState:        %s\n", session_state(session->state));
    printf("\tService:      %s (%p)\n", session->service->name, session->service);
    printf("\tClient DCB:   %p\n", session->client_dcb);
    printf("\tConnected:    %s",
           asctime_r(localtime_r(&session->stats.connect, &result), timebuf));
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
    list_entry_t *current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        printSession((SESSION *)current);
        current = list_iterate(&SESSIONlist, current);
    }
}


/**
 * Check sessions
 *
 * Designed to be called within a debugger session in order
 * to display information regarding "interesting" sessions
 */
void
CheckSessions()
{
    list_entry_t *current;
    int noclients = 0;
    int norouter = 0;

    current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        SESSION *list_session = (SESSION *)current;
        if (list_session->state != SESSION_STATE_LISTENER ||
            list_session->state != SESSION_STATE_LISTENER_STOPPED)
        {
            if (list_session->client_dcb == NULL && list_session->refcount)
            {
                if (noclients == 0)
                {
                    printf("Sessions without a client DCB.\n");
                    printf("==============================\n");
                }
                printSession(list_session);
                noclients++;
            }
        }
        current = list_iterate(&SESSIONlist, current);
    }
    if (noclients)
    {
        printf("%d Sessions have no clients\n", noclients);
    }
    current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        SESSION *list_session = (SESSION *)current;
        if (list_session->state != SESSION_STATE_LISTENER ||
            list_session->state != SESSION_STATE_LISTENER_STOPPED)
        {
            if (list_session->router_session == NULL && list_session->refcount)
            {
                if (norouter == 0)
                {
                    printf("Sessions without a router session.\n");
                    printf("==================================\n");
                }
                printSession(list_session);
                norouter++;
            }
        }
        current = list_iterate(&SESSIONlist, current);
    }
    if (norouter)
    {
        printf("%d Sessions have no router session\n", norouter);
    }
}

/*
 * @brief Print session list statistics
 *
 * @param       pdcb    DCB to print results to
 */
void
dprintSessionList(DCB *pdcb)
{
    dprintListStats(pdcb, &SESSIONlist, "All Sessions");
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

    list_entry_t *current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        dprintSession(dcb, (SESSION *)current);
        current = list_iterate(&SESSIONlist, current);
    }
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
dprintSession(DCB *dcb, SESSION *print_session)
{
    struct tm result;
    char buf[30];
    int i;

    dcb_printf(dcb, "Session %lu (%p)\n", print_session->ses_id, print_session);
    dcb_printf(dcb, "\tState:               %s\n", session_state(print_session->state));
    dcb_printf(dcb, "\tService:             %s (%p)\n", print_session->service->name, print_session->service);
    dcb_printf(dcb, "\tClient DCB:          %p\n", print_session->client_dcb);

    if (print_session->client_dcb && print_session->client_dcb->remote)
    {
        double idle = (hkheartbeat - print_session->client_dcb->last_read);
        idle = idle > 0 ? idle/10.f : 0;
        dcb_printf(dcb, "\tClient Address:          %s%s%s\n",
                   print_session->client_dcb->user?print_session->client_dcb->user:"",
                   print_session->client_dcb->user?"@":"",
                   print_session->client_dcb->remote);
        dcb_printf(dcb, "\tConnected:               %s\n",
                   asctime_r(localtime_r(&print_session->stats.connect, &result), buf));
        if (print_session->client_dcb->state == DCB_STATE_POLLING)
        {
            dcb_printf(dcb, "\tIdle:                %.0f seconds\n",idle);
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
    bool written_heading = false;
    list_entry_t *current = list_start_iteration(&SESSIONlist);
    if (current)
    {
        dcb_printf(dcb, "Sessions.\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
        dcb_printf(dcb, "Session          | Client          | Service        | State\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
        written_heading = true;
    }
    while (current)
    {
        SESSION *list_session = (SESSION *)current;
        dcb_printf(dcb, "%-16p | %-15s | %-14s | %s\n", list_session,
                ((list_session->client_dcb && list_session->client_dcb->remote)
                ? list_session->client_dcb->remote : ""),
                (list_session->service && list_session->service->name ? list_session->service->name
                : ""),
                session_state(list_session->state));
        current = list_iterate(&SESSIONlist, current);
    }
    if (written_heading)
    {
        dcb_printf(dcb,
                   "-----------------+-----------------+----------------+--------------------------\n\n");
    }
}

/**
 * Convert a session state to a string representation
 *
 * @param state         The session state
 * @return A string representation of the session state
 */
char *
session_state(session_state_t state)
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

/*
 * @brief Find the session that relates to a given router session
 *
 * @param rses      A router session
 * @return      The related session, or NULL if none
 */
SESSION* get_session_by_router_ses(void* rses)
{
    list_entry_t *current = list_start_iteration(&SESSIONlist);
    while (current)
    {
        if (((SESSION *)current)->router_session == rses)
        {
            list_terminate_iteration_early(&SESSIONlist, current);
            return (SESSION *)current;
        }
        current = list_iterate(&SESSIONlist, current);
    }
    return NULL;
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
session_setup_filters(SESSION *session)
{
    SERVICE *service = session->service;
    DOWNSTREAM *head;
    UPSTREAM *tail;
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
        if ((head = filterApply(service->filters[i], session,
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
        if ((tail = filterUpstream(service->filters[i],
                                   session->filters[i].session,
                                   &session->tail)) == NULL)
        {
            MXS_ERROR("Failed to create filter '%s' for service '%s'.",
                      service->filters[i]->name,
                      service->name);
            return 0;
        }

        /*
         * filterUpstream may simply return the 3 parameter if
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
 * Entry point for the final element int he upstream filter, i.e. the writing
 * of the data to the client.
 *
 * @param       instance        The "instance" data
 * @param       session         The session
 * @param       data            The buffer chain to write
 */
int
session_reply(void *instance, void *session, GWBUF *data)
{
    SESSION *the_session = (SESSION *)session;

    return the_session->client_dcb->func.write(the_session->client_dcb, data);
}

/**
 * Return the client connection address or name
 *
 * @param session       The session whose client address to return
 */
char *
session_get_remote(SESSION *session)
{
    if (session && session->client_dcb)
    {
        return session->client_dcb->remote;
    }
    return NULL;
}

bool session_route_query(SESSION* ses, GWBUF* buf)
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
char *
session_getUser(SESSION *session)
{
    return (session && session->client_dcb) ? session->client_dcb->user : NULL;
}

/**
 * Enable the timing out of idle connections.
 *
 * This will prevent unnecessary acquisitions of the session spinlock if no
 * service is configured with a session idle timeout.
 */
void enable_session_timeouts()
{
    check_timeouts = true;
}

/**
 * Close sessions that have been idle for too long.
 *
 * If the time since a session last sent data is greater than the set value in the
 * service, it is disconnected. The connection timeout is disabled by default.
 */
void process_idle_sessions()
{
    if (spinlock_acquire_nowait(&timeout_lock))
    {
        if (hkheartbeat >= next_timeout_check)
        {
            list_entry_t *current = list_start_iteration(&SESSIONlist);
            /** Because the resolution of the timeout is one second, we only need to
             * check for it once per second. One heartbeat is 100 milliseconds. */
            next_timeout_check = hkheartbeat + 10;
            while (current)
            {
                SESSION *all_session = (SESSION *)current;

                if (all_session->service && all_session->client_dcb && all_session->client_dcb->state == DCB_STATE_POLLING &&
                    hkheartbeat - all_session->client_dcb->last_read > all_session->service->conn_idle_timeout * 10)
                {
                    poll_fake_hangup_event(all_session->client_dcb);
                }

                current = list_iterate(&SESSIONlist, current);
            }
        }
        spinlock_release(&timeout_lock);
    }
}

/**
 * Callback structure for the session list extraction
 */
typedef struct
{
    int index;
    SESSIONLISTFILTER filter;
} SESSIONFILTER;

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
    SESSIONFILTER *cbdata = (SESSIONFILTER *)data;
    int i = 0;
    list_entry_t *current = list_start_iteration(&SESSIONlist);

    /* Skip to the first non-listener if not showing listeners */
    current = skip_maybe_to_next_non_listener(current, cbdata->filter);

    while (i < cbdata->index && current)
    {
        if (cbdata->filter == SESSION_LIST_ALL ||
            (cbdata->filter == SESSION_LIST_CONNECTION &&
            ((SESSION *)current)->state !=  SESSION_STATE_LISTENER))
        {
            i++;
        }
        current = list_iterate(&SESSIONlist, current);
    }

    /* Skip to the next non-listener if not showing listeners */
    current = skip_maybe_to_next_non_listener(current, cbdata->filter);

    if (NULL == current)
    {
        MXS_FREE(data);
        return NULL;
    }
    else
    {
        char buf[20];
        RESULT_ROW *row;
        SESSION *list_session = (SESSION *)current;

        cbdata->index++;
        row = resultset_make_row(set);
        snprintf(buf,19, "%p", list_session);
        buf[19] = '\0';
        resultset_row_set(row, 0, buf);
        resultset_row_set(row, 1, ((list_session->client_dcb && list_session->client_dcb->remote)
                               ? list_session->client_dcb->remote : ""));
        resultset_row_set(row, 2, (list_session->service && list_session->service->name
                               ? list_session->service->name : ""));
        resultset_row_set(row, 3, session_state(list_session->state));
        list_terminate_iteration_early(&SESSIONlist, current);
        return row;
    }
}

/*
 * @brief   Skip to the next non-listener session, if not showing listeners
 *
 * Based on a test of the filter that is the second parameter, along with the
 * state of the sessions.
 *
 * @param       current The session to start the possible skipping
 * @param       filter  The filter the defines the operation
 *
 * @result      The first session beyond those skipped, or the starting session;
 *              NULL if the list of sessions is exhausted.
 */
static list_entry_t *skip_maybe_to_next_non_listener(list_entry_t *current, SESSIONLISTFILTER filter)
{
    /* Skip to the first non-listener if not showing listeners */
    while (current && filter == SESSION_LIST_CONNECTION &&
        ((SESSION *)current)->state == SESSION_STATE_LISTENER)
    {
        current = list_iterate(&SESSIONlist, current);
    }
    return current;
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
    if ((set = resultset_create(sessionRowCallback, data)) == NULL)
    {
        MXS_FREE(data);
        return NULL;
    }
    resultset_add_column(set, "Session", 16, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Client", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Service", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "State", 15, COL_TYPE_VARCHAR);

    return set;
}
/*lint +e429 */

session_trx_state_t session_get_trx_state(const SESSION* ses)
{
    return ses->trx_state;
}

session_trx_state_t session_set_trx_state(SESSION* ses, session_trx_state_t new_state)
{
    session_trx_state_t prev_state = ses->trx_state;

    ses->trx_state = new_state;

    return prev_state;
}

const char* session_trx_state_to_string(session_trx_state_t state)
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
    }

    MXS_ERROR("Unknown session_trx_state_t value: %d", (int)state);
    return "UNKNOWN";
}
