/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
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
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <session.h>
#include <service.h>
#include <router.h>
#include <dcb.h>
#include <spinlock.h>
#include <atomic.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <housekeeper.h>

/** Global session id; updated safely by holding session_spin */
static size_t session_id;

static SPINLOCK session_spin = SPINLOCK_INIT;
static SESSION *allSessions = NULL;
static SESSION *lastSession = NULL;
static SESSION *wasfreeSession = NULL;
static int freeSessionCount = 0;

static struct session session_dummy_struct;

/**
 * These two are declared in session.h
 */
bool check_timeouts = false;
long next_timeout_check = 0;

static SPINLOCK timeout_lock = SPINLOCK_INIT;

static int session_setup_filters(SESSION *session);
static void session_simple_free(SESSION *session, DCB *dcb);
static void session_add_to_all_list(SESSION *session);
static SESSION *session_find_free();
static void session_final_free(SESSION *session);

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

    spinlock_acquire(&session_spin);
    session = session_find_free();
    spinlock_release(&session_spin);
    ss_info_dassert(session != NULL, "Allocating memory for session failed.");

    if (session == NULL)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to allocate memory for "
                  "session object due error %d, %s.",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        return NULL;
    }
#if defined(SS_DEBUG)
    session->ses_chk_top = CHK_NUM_SESSION;
    session->ses_chk_tail = CHK_NUM_SESSION;
#endif
    session->ses_is_child = (bool) DCB_IS_CLONE(client_dcb);
    spinlock_init(&session->ses_lock);
    session->service = service;
    session->client_dcb = client_dcb;
    session->n_filters = 0;
    memset(&session->stats, 0, sizeof(SESSION_STATS));
    session->stats.connect = time(0);
    session->state = SESSION_STATE_ALLOC;
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
    spinlock_acquire(&session_spin);
    /** Assign a session id and increase, insert session into list */
    session->ses_id = ++session_id;
    spinlock_release(&session_spin);
    atomic_add(&service->stats.n_sessions, 1);
    atomic_add(&service->stats.n_current, 1);
    CHK_SESSION(session);

    client_dcb->session = session;
    return SESSION_STATE_TO_BE_FREED == session->state ? NULL : session;
}

/**
 * Add a new session to the list of all sessions.
 *
 * Must be called with the general session lock held.
 *
 * A pointer, lastSession, is held to find the end of the list, and the new session
 * is linked to the end of the list.  The pointer, wasfreeSession, that is used to
 * search for a free session is initialised if not already set. There cannot be
 * any free sessions (or any at all) until this routine has been called at least
 * once. Hence it will not be referred to until after it is initialised.
 *
 * @param session       The session to be added to the list
 */
static void
session_add_to_all_list(SESSION *session)
{
    if (allSessions == NULL)
    {
        allSessions = session;
    }
    else
    {
        lastSession->next = session;
    }
    lastSession = session;
    if (NULL == wasfreeSession)
    {
        wasfreeSession = session;
    }
}

/**
 * Find a free session or allocate memory for a new one.
 *
 * This routine looks to see whether there are free session memory areas.
 * If not, new memory is allocated, if possible, and the new session is added to
 * the list of all sessions.
 *
 * Must be called with the general session lock held.
 *
 * @return An available session or NULL if none could be allocated.
 */
static SESSION *
session_find_free()
{
    SESSION *nextsession;

    if (freeSessionCount <= 0)
    {
        SESSION *newsession;
        if ((newsession = calloc(1, sizeof(SESSION))) == NULL)
        {
            return NULL;
        }
        newsession->next = NULL;
        session_add_to_all_list(newsession);
        newsession->ses_is_in_use = true;
        return newsession;
    }
    /* Starting at the last place a free session was found, loop through the */
    /* list of sessions searching for one that is not in use. */
    while (wasfreeSession->ses_is_in_use)
    {
        int loopcount = 0;
        wasfreeSession = wasfreeSession->next;
        if (NULL == wasfreeSession)
        {
            loopcount++;
            if (loopcount > 1)
            {
                /* Shouldn't need to loop round more than once */
                MXS_ERROR("Find free session failed to find a session even"
                          " though free count was positive");
                return NULL;
            }
            wasfreeSession = allSessions;
        }
    }
    /* Dropping out of the loop means we have found a session that is not in use */
    freeSessionCount--;
    /* Clear the old data, then reset the list forward link */
    nextsession = wasfreeSession->next;
    memset(wasfreeSession, 0, sizeof(SESSION));
    wasfreeSession->next = nextsession;
    wasfreeSession->ses_is_in_use = true;
    return wasfreeSession;
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
#if defined(SS_DEBUG)
    session->ses_chk_top = CHK_NUM_SESSION;
    session->ses_chk_tail = CHK_NUM_SESSION;
#endif
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
        free(clientdata);
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
        free(session->filters);
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
    spinlock_acquire(&session_spin);
    session->ses_is_in_use = false;
    freeSessionCount++;
    spinlock_release(&session_spin);
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
    SESSION *list_session;
    int rval = 0;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    while (list_session)
    {
        if (list_session->ses_is_in_use && list_session == session)
        {
            rval = 1;
            break;
        }
        list_session = list_session->next;
    }
    spinlock_release(&session_spin);

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
    SESSION *list_session;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    while (list_session)
    {
        if (list_session->ses_is_in_use)
        {
            printSession(list_session);
        }
        list_session = list_session->next;
    }
    spinlock_release(&session_spin);
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
    SESSION *list_session;
    int noclients = 0;
    int norouter = 0;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    while (list_session)
    {
        if (false == list_session->ses_is_in_use)
        {
            list_session = list_session->next;
            continue;
        }
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
        list_session = list_session->next;
    }
    spinlock_release(&session_spin);
    if (noclients)
    {
        printf("%d Sessions have no clients\n", noclients);
    }
    spinlock_acquire(&session_spin);
    list_session = allSessions;
    while (list_session)
    {
        if (false == list_session->ses_is_in_use)
        {
            list_session = list_session->next;
            continue;
        }
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
        list_session = list_session->next;
    }
    spinlock_release(&session_spin);
    if (norouter)
    {
        printf("%d Sessions have no router session\n", norouter);
    }
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
    SESSION *list_session;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    while (list_session)
    {
        if (false == list_session->ses_is_in_use)
        {
            list_session = list_session->next;
            continue;
        }

        dprintSession(dcb, list_session);

        list_session = list_session->next;
    }
    spinlock_release(&session_spin);
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
    dcb_printf(dcb, "Session %lu (%p)\n", print_session->ses_id, print_session);
    dcb_printf(dcb, "\tState:               %s\n", session_state(print_session->state));
    dcb_printf(dcb, "\tService:             %s (%p)\n", print_session->service->name, print_session->service);
    dcb_printf(dcb, "\tClient DCB:          %p\n", print_session->client_dcb);

    if (print_session->client_dcb && print_session->client_dcb->remote)
    {
        dcb_printf(dcb, "\tClient Address:      %s%s%s\n",
                   print_session->client_dcb->user ? print_session->client_dcb->user : "",
                   print_session->client_dcb->user ? "@" : "",
                   print_session->client_dcb->remote);
    }

    struct tm result;
    char buf[30];

    dcb_printf(dcb, "\tConnected:           %s", // asctime inserts newline.
               asctime_r(localtime_r(&print_session->stats.connect, &result), buf));

    if (print_session->client_dcb && print_session->client_dcb->state == DCB_STATE_POLLING)
    {
        double idle = (hkheartbeat - print_session->client_dcb->last_read);
        idle = idle > 0 ? idle / 10.f : 0;
        dcb_printf(dcb, "\tIdle:                %.0f seconds\n", idle);
    }

    if (print_session->n_filters)
    {
        for (int i = 0; i < print_session->n_filters; i++)
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
    SESSION *list_session;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    if (list_session)
    {
        dcb_printf(dcb, "Sessions.\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
        dcb_printf(dcb, "Session          | Client          | Service        | State\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
    }
    while (list_session)
    {
        if (list_session->ses_is_in_use)
        {
            dcb_printf(dcb, "%-16p | %-15s | %-14s | %s\n", list_session,
                       ((list_session->client_dcb && list_session->client_dcb->remote)
                        ? list_session->client_dcb->remote : ""),
                       (list_session->service && list_session->service->name ? list_session->service->name
                        : ""),
                       session_state(list_session->state));
        }
        list_session = list_session->next;
    }
    if (allSessions)
    {
        dcb_printf(dcb,
                   "-----------------+-----------------+----------------+--------------------------\n\n");
    }
    spinlock_release(&session_spin);
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

SESSION* get_session_by_router_ses(void* rses)
{
    SESSION* ses = allSessions;

    while (((ses->ses_is_in_use == false) || (ses->router_session != rses)) && ses->next != NULL)
    {
        ses = ses->next;
    }

    if (ses->ses_is_in_use == false || ses->router_session != rses)
    {
        ses = NULL;
    }
    return ses;
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

    if ((session->filters = calloc(service->n_filters,
                                   sizeof(SESSION_FILTER))) == NULL)
    {
        MXS_ERROR("Insufficient memory to allocate session filter "
                  "tracking.\n");
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
        free(head);
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
            free(tail);
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
 * Return the pointer to the list of all sessions.
 * @return Pointer to the list of all sessions.
 */
SESSION *get_all_sessions()
{
    return allSessions;
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
            /** Because the resolution of the timeout is one second, we only need to
             * check for it once per second. One heartbeat is 100 milliseconds. */
            next_timeout_check = hkheartbeat + 10;
            spinlock_acquire(&session_spin);
            SESSION *all_session = allSessions;

            while (all_session)
            {
                if (all_session->ses_is_in_use &&
                    all_session->service && all_session->client_dcb && all_session->client_dcb->state == DCB_STATE_POLLING &&
                    hkheartbeat - all_session->client_dcb->last_read > all_session->service->conn_idle_timeout * 10)
                {
                    dcb_close(all_session->client_dcb);
                }

                all_session = all_session->next;
            }
            spinlock_release(&session_spin);
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
    char buf[20];
    RESULT_ROW *row;
    SESSION *list_session;

    spinlock_acquire(&session_spin);
    list_session = allSessions;
    /* Skip to the first non-listener if not showing listeners */
    while (false == list_session->ses_is_in_use ||
           (list_session && cbdata->filter == SESSION_LIST_CONNECTION &&
            list_session->state == SESSION_STATE_LISTENER))
    {
        list_session = list_session->next;
    }
    while (i < cbdata->index && list_session)
    {
        if (list_session->ses_is_in_use)
        {
            if (cbdata->filter == SESSION_LIST_CONNECTION &&
                list_session->state !=  SESSION_STATE_LISTENER)
            {
                i++;
            }
            else if (cbdata->filter == SESSION_LIST_ALL)
            {
                i++;
            }
        }
        list_session = list_session->next;
    }
    /* Skip to the next non-listener if not showing listeners */
    while (list_session && (false == list_session->ses_is_in_use ||
                            (cbdata->filter == SESSION_LIST_CONNECTION &&
                             list_session->state == SESSION_STATE_LISTENER)))
    {
        list_session = list_session->next;
    }
    if (list_session == NULL)
    {
        spinlock_release(&session_spin);
        free(data);
        return NULL;
    }
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
    spinlock_release(&session_spin);
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

    if ((data = (SESSIONFILTER *)malloc(sizeof(SESSIONFILTER))) == NULL)
    {
        return NULL;
    }
    data->index = 0;
    data->filter = filter;
    if ((set = resultset_create(sessionRowCallback, data)) == NULL)
    {
        free(data);
        return NULL;
    }
    resultset_add_column(set, "Session", 16, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Client", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "Service", 15, COL_TYPE_VARCHAR);
    resultset_add_column(set, "State", 15, COL_TYPE_VARCHAR);

    return set;
}
/*lint +e429 */

