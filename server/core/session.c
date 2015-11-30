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

static struct session session_dummy_struct;

static int session_setup_filters(SESSION *session);
static void session_simple_free(SESSION *session, DCB *dcb);

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

    session = (SESSION *)calloc(1, sizeof(SESSION));
    ss_info_dassert(session != NULL, "Allocating memory for session failed.");

    if (session == NULL)
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Failed to allocate memory for "
                  "session object due error %d, %s.",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
        /* Does this possibly need a lock? */
        /*
         * This is really not the right way to do this.  The data in a DCB is
         * router specific and should be freed by a function in the relevant
         * router.  This would be better achieved by placing a function reference
         * in the DCB and having dcb_final_free call it to dispose of the data
         * at the final destruction of the DCB.  However, this piece of code is
         * only run following a calloc failure, so the system is probably on
         * the point of crashing anyway.
         *
         */
        if (client_dcb->data && !DCB_IS_CLONE(client_dcb))
        {
            void * clientdata = client_dcb->data;
            client_dcb->data = NULL;
            free(clientdata);
        }
        return NULL;
    }
#if defined(SS_DEBUG)
    session->ses_chk_top = CHK_NUM_SESSION;
    session->ses_chk_tail = CHK_NUM_SESSION;
#endif
    session->ses_is_child = (bool) DCB_IS_CLONE(client_dcb);
    spinlock_init(&session->ses_lock);
    session->service = service;
    session->client = client_dcb;
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
    session->data = client_dcb->data;
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

            MXS_ERROR("%lu [%s] Error : Failed to create %s session because router"
                      "could not establish a new router session, see earlier error.",
                      pthread_self(),
                      __func__,
                      service->name);
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

        if (session->client->user == NULL)
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
                     session->client->user,
                     session->client->remote);
        }
    }
    else
    {
        MXS_INFO("Start %s client session [%lu] for '%s' from %s failed, will be "
                 "closed as soon as all related DCBs have been closed.",
                 service->name,
                 session->ses_id,
                 session->client->user,
                 session->client->remote);
    }
    spinlock_acquire(&session_spin);
    /** Assign a session id and increase, insert session into list */
    session->ses_id = ++session_id;
    session->next = allSessions;
    allSessions = session;
    spinlock_release(&session_spin);
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
    session->client = NULL;
    session->n_filters = 0;
    memset(&session->stats, 0, sizeof(SESSION_STATS));
    session->stats.connect = 0;
    session->state = SESSION_STATE_DUMMY;
    session->data = NULL;
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
 * @param ses      session
 * @param priority syslog priority
 */
void session_enable_log_priority(SESSION* ses, int priority)
{
    ses->enabled_log_priorities |= (1 << priority);
    atomic_add((int *)&mxs_log_session_count[priority], 1);
}

/**
 * Disable specified log priority for the current session and decrease logger
 * counter.
 * Generic logging setting has precedence over session-specific setting.
 *
 * @param ses   session
 * @param priority syslog priority
 */
void session_disable_log_priority(SESSION* ses, int priority)
{
    if (ses->enabled_log_priorities & (1 << priority))
    {
        ses->enabled_log_priorities &= ~(1 << priority);
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

int session_unlink_dcb(SESSION* session,
                       DCB*     dcb)
{
    int nlink;

    CHK_SESSION(session);

    spinlock_acquire(&session->ses_lock);
    ss_dassert(session->refcount > 0);
    /*<
     * Remove dcb from session's router_client_session.
     */
    nlink = atomic_add(&session->refcount, -1);
    nlink -= 1;

    if (nlink == 0)
    {
        session->state = SESSION_STATE_TO_BE_FREED;
    }

    if (dcb != NULL)
    {
        if (session->client == dcb)
        {
            session->client = NULL;
        }
        dcb->session = NULL;
    }
    spinlock_release(&session->ses_lock);

    return nlink;
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

    free(session);
}

/**
 * Deallocate the specified session
 *
 * @param session       The session to deallocate
 */
bool
session_free(SESSION *session)
{
    if (session && SESSION_STATE_DUMMY == session->state)
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

    /* First of all remove from the linked list */
    spinlock_acquire(&session_spin);
    if (allSessions == session)
    {
        allSessions = session->next;
    }
    else
    {
        SESSION *chksession;
        chksession = allSessions;
        while (chksession && chksession->next != session)
        {
            chksession = chksession->next;
        }
        if (chksession)
        {
            chksession->next = session->next;
        }
    }
    spinlock_release(&session_spin);
    atomic_add(&session->service->stats.n_current, -1);

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

        if (session->data)
        {
            free(session->data);
        }
        free(session);
    }
    return true;
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
    SESSION *ptr;
    int rval = 0;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    while (ptr)
    {
        if (ptr == session)
        {
            rval = 1;
            break;
        }
        ptr = ptr->next;
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
    printf("\tClient DCB:   %p\n", session->client);
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
    SESSION *ptr;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    while (ptr)
    {
        printSession(ptr);
        ptr = ptr->next;
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
    SESSION *ptr;
    int noclients = 0;
    int norouter = 0;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    while (ptr)
    {
        if (ptr->state != SESSION_STATE_LISTENER ||
            ptr->state != SESSION_STATE_LISTENER_STOPPED)
        {
            if (ptr->client == NULL && ptr->refcount)
            {
                if (noclients == 0)
                {
                    printf("Sessions without a client DCB.\n");
                    printf("==============================\n");
                }
                printSession(ptr);
                noclients++;
            }
        }
        ptr = ptr->next;
    }
    spinlock_release(&session_spin);
    if (noclients)
    {
        printf("%d Sessions have no clients\n", noclients);
    }
    spinlock_acquire(&session_spin);
    ptr = allSessions;
    while (ptr)
    {
        if (ptr->state != SESSION_STATE_LISTENER ||
            ptr->state != SESSION_STATE_LISTENER_STOPPED)
        {
            if (ptr->router_session == NULL && ptr->refcount)
            {
                if (norouter == 0)
                {
                    printf("Sessions without a router session.\n");
                    printf("==================================\n");
                }
                printSession(ptr);
                norouter++;
            }
        }
        ptr = ptr->next;
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
    struct tm result;
    char timebuf[40];
    SESSION *ptr;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    while (ptr)
    {
        dcb_printf(dcb, "Session %d (%p)\n",ptr->ses_id, ptr);
        dcb_printf(dcb, "\tState:               %s\n", session_state(ptr->state));
        dcb_printf(dcb, "\tService:             %s (%p)\n", ptr->service->name, ptr->service);
        dcb_printf(dcb, "\tClient DCB:          %p\n", ptr->client);

        if (ptr->client && ptr->client->remote)
        {
            dcb_printf(dcb, "\tClient Address:              %s%s%s\n",
                       ptr->client->user?ptr->client->user:"",
                       ptr->client->user?"@":"",
                       ptr->client->remote);
        }

        dcb_printf(dcb, "\tConnected:           %s",
                   asctime_r(localtime_r(&ptr->stats.connect, &result), timebuf));

        if (ptr->client && ptr->client->state == DCB_STATE_POLLING)
        {
            double idle = (hkheartbeat - ptr->client->last_read);
            idle = idle > 0 ? idle/10.0:0;
            dcb_printf(dcb, "\tIdle:                            %.0f seconds\n",idle);
        }

        ptr = ptr->next;
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
 * @param ptr   The session to print
 */
void
dprintSession(DCB *dcb, SESSION *ptr)
{
    struct tm result;
    char buf[30];
    int i;

    dcb_printf(dcb, "Session %d (%p)\n",ptr->ses_id, ptr);
    dcb_printf(dcb, "\tState:               %s\n", session_state(ptr->state));
    dcb_printf(dcb, "\tService:             %s (%p)\n", ptr->service->name, ptr->service);
    dcb_printf(dcb, "\tClient DCB:          %p\n", ptr->client);
    if (ptr->client && ptr->client->remote)
    {
        double idle = (hkheartbeat - ptr->client->last_read);
        idle = idle > 0 ? idle/10.f : 0;
        dcb_printf(dcb, "\tClient Address:          %s%s%s\n",
                   ptr->client->user?ptr->client->user:"",
                   ptr->client->user?"@":"",
                   ptr->client->remote);
        dcb_printf(dcb, "\tConnected:               %s\n",
                   asctime_r(localtime_r(&ptr->stats.connect, &result), buf));
        if (ptr->client->state == DCB_STATE_POLLING)
        {
            dcb_printf(dcb, "\tIdle:                %.0f seconds\n",idle);
        }

    }
    if (ptr->n_filters)
    {
        for (i = 0; i < ptr->n_filters; i++)
        {
            dcb_printf(dcb, "\tFilter: %s\n",
                       ptr->filters[i].filter->name);
            ptr->filters[i].filter->obj->diagnostics(ptr->filters[i].instance,
                                                     ptr->filters[i].session,
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
    SESSION *ptr;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    if (ptr)
    {
        dcb_printf(dcb, "Sessions.\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
        dcb_printf(dcb, "Session          | Client          | Service        | State\n");
        dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n");
    }
    while (ptr)
    {
        dcb_printf(dcb, "%-16p | %-15s | %-14s | %s\n", ptr,
                   ((ptr->client && ptr->client->remote)
                    ? ptr->client->remote : ""),
                   (ptr->service && ptr->service->name ? ptr->service->name
                    : ""),
                   session_state(ptr->state));
        ptr = ptr->next;
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
session_state(int state)
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
#ifdef SS_DEBUG
    case SESSION_STATE_STOPPING:
        return "Stopping session";
    case SESSION_STATE_TO_BE_FREED:
        return "Session to be freed";
    case SESSION_STATE_FREE:
        return "Freed session";
#endif
    default:
        return "Invalid State";
    }
}

SESSION* get_session_by_router_ses(void* rses)
{
    SESSION* ses = allSessions;

    while (ses->router_session != rses && ses->next != NULL)
    {
        ses = ses->next;
    }

    if (ses->router_session != rses)
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

    return the_session->client->func.write(the_session->client, data);
}

/**
 * Return the client connection address or name
 *
 * @param session       The session whose client address to return
 */
char *
session_get_remote(SESSION *session)
{
    if (session && session->client)
    {
        return session->client->remote;
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
    return (session && session->client) ? session->client->user : NULL;
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
 * Close sessions that have been idle for too long.
 *
 * If the time since a session last sent data is grater than the set value in the
 * service, it is disconnected. The default value for the timeout for a service is 0.
 * This means that connections are never timed out.
 * @param data NULL, this is only here to satisfy the housekeeper function requirements.
 */
void session_close_timeouts(void* data)
{
    SESSION* ses;

    spinlock_acquire(&session_spin);
    ses = get_all_sessions();
    spinlock_release(&session_spin);

    while (ses)
    {
        if (ses->client && ses->client->state == DCB_STATE_POLLING &&
            ses->service->conn_timeout > 0 &&
            hkheartbeat - ses->client->last_read > ses->service->conn_timeout * 10)
        {
            dcb_close(ses->client);
        }

        spinlock_acquire(&session_spin);
        ses = ses->next;
        spinlock_release(&session_spin);
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
    SESSION *ptr;

    spinlock_acquire(&session_spin);
    ptr = allSessions;
    /* Skip to the first non-listener if not showing listeners */
    while (ptr && cbdata->filter == SESSION_LIST_CONNECTION &&
           ptr->state == SESSION_STATE_LISTENER)
    {
        ptr = ptr->next;
    }
    while (i < cbdata->index && ptr)
    {
        if (cbdata->filter == SESSION_LIST_CONNECTION &&
            ptr->state !=  SESSION_STATE_LISTENER)
        {
            i++;
        }
        else if (cbdata->filter == SESSION_LIST_ALL)
        {
            i++;
        }
        ptr = ptr->next;
    }
    /* Skip to the next non-listener if not showing listeners */
    while (ptr && cbdata->filter == SESSION_LIST_CONNECTION &&
           ptr->state == SESSION_STATE_LISTENER)
    {
        ptr = ptr->next;
    }
    if (ptr == NULL)
    {
        spinlock_release(&session_spin);
        free(data);
        return NULL;
    }
    cbdata->index++;
    row = resultset_make_row(set);
    snprintf(buf,19, "%p", ptr);
    buf[19] = '\0';
    resultset_row_set(row, 0, buf);
    resultset_row_set(row, 1, ((ptr->client && ptr->client->remote)
                               ? ptr->client->remote : ""));
    resultset_row_set(row, 2, (ptr->service && ptr->service->name
                               ? ptr->service->name : ""));
    resultset_row_set(row, 3, session_state(ptr->state));
    spinlock_release(&session_spin);
    return row;
}

/**
 * Return a resultset that has the current set of sessions in it
 *
 * @return A Result set
 */
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
