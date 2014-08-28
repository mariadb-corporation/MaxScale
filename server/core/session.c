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
 * @file session.c  - A representation of the session within the gateway.
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 17/06/13	Mark Riddoch		Initial implementation
 * 02/09/13	Massimiliano Pinto	Added session refcounter
 * 29/05/14	Mark Riddoch		Addition of filter mechanism
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

extern int lm_enabled_logfiles_bitmask;

static SPINLOCK	session_spin = SPINLOCK_INIT;
static SESSION	*allSessions = NULL;


static int session_setup_filters(SESSION *session);

/**
 * Allocate a new session for a new client of the specified service.
 *
 * Create the link to the router session by calling the newSession
 * entry point of the router using the router instance of the
 * service this session is part of.
 *
 * @param service	The service this connection was established by
 * @param client_dcb	The client side DCB
 * @return		The newly created session or NULL if an error occured
 */
SESSION *
session_alloc(SERVICE *service, DCB *client_dcb)
{
        SESSION 	*session;

        session = (SESSION *)calloc(1, sizeof(SESSION));
        ss_info_dassert(session != NULL,
                        "Allocating memory for session failed.");
        
        if (session == NULL) {
                int eno = errno;
                errno = 0;
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to allocate memory for "
                        "session object due error %d, %s.",
                        eno,
                        strerror(eno))));
		goto return_session;
        }
#if defined(SS_DEBUG)
        session->ses_chk_top = CHK_NUM_SESSION;
        session->ses_chk_tail = CHK_NUM_SESSION;
#endif
        spinlock_init(&session->ses_lock);
        /*<
         * Prevent backend threads from accessing before session is completely
         * initialized.
         */
        spinlock_acquire(&session->ses_lock);
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
	client_dcb->session = session;
	session->refcount = 1;
        /*<
         * This indicates that session is ready to be shared with backend
         * DCBs. Note that this doesn't mean that router is initialized yet!
         */
        session->state = SESSION_STATE_READY;
        
        /*< Release session lock */
        spinlock_release(&session->ses_lock);

	/*
	 * Only create a router session if we are not the listening 
	 * DCB or an internal DCB. Creating a router session may create a connection to a
	 * backend server, depending upon the router module implementation
	 * and should be avoided for the listener session
	 *
	 * Router session creation may create other DCBs that link to the
	 * session, therefore it is important that the session lock is
         * relinquished beforethe router call.
	 */
	if (client_dcb->state != DCB_STATE_LISTENING && 
                client_dcb->dcb_role != DCB_ROLE_INTERNAL)
	{
		session->router_session =
                    service->router->newSession(service->router_instance,
                                                session);
	
                if (session->router_session == NULL) {
                        /**
                         * Inform other threads that session is closing.
                         */
                        session->state = SESSION_STATE_STOPPING;
                        /*<
                         * Decrease refcount, set dcb's session pointer NULL
                         * and set session pointer to NULL.
                         */
                        session_free(session);
                        client_dcb->session = NULL;
                        session = NULL;
                        LOGIF(LE, (skygw_log_write_flush(
                                LOGFILE_ERROR,
                                "Error : Failed to create %s session.",
                                service->name)));
                        
                        goto return_session;
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

		if (service->n_filters > 0)
		{
			if (!session_setup_filters(session))
			{
				/**
				 * Inform other threads that session is closing.
				 */
				session->state = SESSION_STATE_STOPPING;
				/*<
				 * Decrease refcount, set dcb's session pointer NULL
				 * and set session pointer to NULL.
				 */
				session_free(session);
				client_dcb->session = NULL;
				session = NULL;
				LOGIF(LE, (skygw_log_write_flush(
					LOGFILE_ERROR,
					"Error : Failed to create %s session.",
					service->name)));
				goto return_session;
			}
		}
        }

	spinlock_acquire(&session_spin);
        
        if (session->state != SESSION_STATE_READY)
        {
                session_free(session);
                client_dcb->session = NULL;
                session = NULL;
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to create %s session.",
                        service->name)));
                spinlock_release(&session_spin);
        }
        else
        {
                session->state = SESSION_STATE_ROUTER_READY;
                session->next = allSessions;
                allSessions = session;
                spinlock_release(&session_spin);
                atomic_add(&service->stats.n_sessions, 1);
                atomic_add(&service->stats.n_current, 1);
                CHK_SESSION(session);
        }        
return_session:
	return session;
}

/**
 * Link a session to a DCB.
 *
 * @param session	The session to link with the dcb
 * @param dcb		The DCB to be linked
 * @return		True if the session was sucessfully linked to the DCB
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

int session_unlink_dcb(
        SESSION* session,
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
                session->state = SESSION_STATE_FREE;
        }

        if (dcb != NULL)
        {
                 dcb->session = NULL;
        }
        spinlock_release(&session->ses_lock);
        
        return nlink;
}

/**
 * Deallocate the specified session
 *
 * @param session	The session to deallocate
 */
bool session_free(
        SESSION *session)
{
        bool    succp = false;
        SESSION *ptr;
        int     nlink;
	int	i;

        CHK_SESSION(session);

        /*<
         * Remove one reference. If there are no references left,
         * free session.
         */
        nlink = session_unlink_dcb(session, NULL);

        if (nlink != 0) {
                ss_dassert(nlink > 0);
                goto return_succp;
        }
        
	/* First of all remove from the linked list */
	spinlock_acquire(&session_spin);
	if (allSessions == session)
	{
		allSessions = session->next;
	}
	else
	{
		ptr = allSessions;
		while (ptr && ptr->next != session)
		{
			ptr = ptr->next;
		}
		if (ptr)
			ptr->next = session->next;
	}
	spinlock_release(&session_spin);
	atomic_add(&session->service->stats.n_current, -1);

	/* Free router_session and session */
        if (session->router_session) {
                session->service->router->freeSession(
                        session->service->router_instance,
                        session->router_session);
        }
	if (session->n_filters)
	{
		for (i = 0; i < session->n_filters; i++)
		{
			session->filters[i].filter->obj->closeSession(
					session->filters[i].instance,
					session->filters[i].session);
		}
		for (i = 0; i < session->n_filters; i++)
		{
			session->filters[i].filter->obj->freeSession(
					session->filters[i].instance,
					session->filters[i].session);
		}
		free(session->filters);
	}
	free(session);
        succp = true;
        
return_succp :
        return succp;
}

/**
 * Check to see if a session is valid, i.e. in the list of all sessions
 *
 * @param session	Session to check
 * @return		1 if the session is valid otherwise 0
 */
int
session_isvalid(SESSION *session)
{
SESSION		*ptr;
int		rval = 0;

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
 * @param session	Session to print
 */
void
printSession(SESSION *session)
{
	printf("Session %p\n", session);
	printf("\tState:    	%s\n", session_state(session->state));
	printf("\tService:	%s (%p)\n", session->service->name, session->service);
	printf("\tClient DCB:	%p\n", session->client);
	printf("\tConnected:	%s", asctime(localtime(&session->stats.connect)));
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
SESSION	*ptr;

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
SESSION	*ptr;
int	noclients = 0;
int	norouter = 0;

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
		printf("%d Sessions have no clients\n", noclients);
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
		printf("%d Sessions have no router session\n", norouter);
}

/**
 * Print all sessions to a DCB
 *
 * Designed to be called within a debugger session in order
 * to display all active sessions within the gateway
 *
 * @param dcb	The DCB to print to
 */
void
dprintAllSessions(DCB *dcb)
{
SESSION	*ptr;

	spinlock_acquire(&session_spin);
	ptr = allSessions;
	while (ptr)
	{
		dcb_printf(dcb, "Session %p\n", ptr);
		dcb_printf(dcb, "\tState:    		%s\n", session_state(ptr->state));
		dcb_printf(dcb, "\tService:		%s (%p)\n", ptr->service->name, ptr->service);
		dcb_printf(dcb, "\tClient DCB:		%p\n", ptr->client);
		if (ptr->client && ptr->client->remote)
			dcb_printf(dcb, "\tClient Address:		%s\n", ptr->client->remote);
		dcb_printf(dcb, "\tConnected:		%s", asctime(localtime(&ptr->stats.connect)));
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
 * @param dcb	The DCB to print to
 * @param ptr	The session to print
 */
void
dprintSession(DCB *dcb, SESSION *ptr)
{
int	i;

	dcb_printf(dcb, "Session %p\n", ptr);
	dcb_printf(dcb, "\tState:    		%s\n", session_state(ptr->state));
	dcb_printf(dcb, "\tService:		%s (%p)\n", ptr->service->name, ptr->service);
	dcb_printf(dcb, "\tClient DCB:		%p\n", ptr->client);
	if (ptr->client && ptr->client->remote)
		dcb_printf(dcb, "\tClient Address:		%s\n", ptr->client->remote);
	dcb_printf(dcb, "\tConnected:		%s", asctime(localtime(&ptr->stats.connect)));
	if (ptr->n_filters)
	{
		for (i = 0; i < ptr->n_filters; i++)
		{
			dcb_printf(dcb, "\tFilter: %s\n",
					ptr->filters[i].filter->name);
			ptr->filters[i].filter->obj->diagnostics(
					ptr->filters[i].instance,
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
 * @param dcb	The DCB to print to
 */
void
dListSessions(DCB *dcb)
{
SESSION	*ptr;

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
		dcb_printf(dcb, "-----------------+-----------------+----------------+--------------------------\n\n");
	spinlock_release(&session_spin);
}

/**
 * Convert a session state to a string representation
 *
 * @param state		The session state
 * @return A string representation of the session state
 */
char *
session_state(int state)
{
	switch (state)
	{
	case SESSION_STATE_ALLOC:
		return "Session Allocated";
	case SESSION_STATE_READY:
		return "Session Ready";
	case SESSION_STATE_ROUTER_READY:
		return "Session ready for routing";
	case SESSION_STATE_LISTENER:
		return "Listener Session";
	case SESSION_STATE_LISTENER_STOPPED:
		return "Stopped Listener Session";
	default:
		return "Invalid State";
	}
}

SESSION* get_session_by_router_ses(
        void* rses)
{
        SESSION* ses = allSessions;
        
        while (ses->router_session != rses && ses->next != NULL)
                ses = ses->next;
        
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
 * @param	session		The session that requires the chain
 * @return	0 if filter creation fails
 */
static int
session_setup_filters(SESSION *session)
{
SERVICE		*service = session->service;
DOWNSTREAM 	*head;
UPSTREAM	*tail;
int		i;

	if ((session->filters = calloc(service->n_filters,
				sizeof(SESSION_FILTER))) == NULL)
	{
                LOGIF(LE, (skygw_log_write_flush(
			LOGFILE_ERROR,
			"Insufficient memory to allocate session filter "
			"tracking.\n")));
			return 0;
	}
	session->n_filters = service->n_filters;
	for (i = service->n_filters - 1; i >= 0; i--)
	{
		if ((head = filterApply(service->filters[i], session,
						&session->head)) == NULL)
		{
                	LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Failed to create filter '%s' for service '%s'.\n",
					service->filters[i]->name,
					service->name)));
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
                	LOGIF(LE, (skygw_log_write_flush(
				LOGFILE_ERROR,
				"Failed to create filter '%s' for service '%s'.\n",
					service->filters[i]->name,
					service->name)));
			return 0;
		}
		session->tail = *tail;
	}

	return 1;
}

/**
 * Entry point for the final element int he upstream filter, i.e. the writing
 * of the data to the client.
 *
 * @param	instance	The "instance" data
 * @param	session		The session
 * @param	data		The buffer chain to write
 */
int
session_reply(void *instance, void *session, GWBUF *data)
{
SESSION		*the_session = (SESSION *)session;

	return the_session->client->func.write(the_session->client, data);
}

/**
 * Return the client connection address or name
 *
 * @param session	The session whose client address to return
 */
char *
session_get_remote(SESSION *session)
{
	if (session && session->client)
		return session->client->remote;
	return NULL;
}

bool session_route_query (
        SESSION* ses,
        GWBUF*   buf)
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
 * @param session		The session pointer.
 * @return	The user name or NULL if it can not be determined.
 */
char *
session_getUser(SESSION *session)
{
	return (session && session->client) ? session->client->user : NULL;
}
