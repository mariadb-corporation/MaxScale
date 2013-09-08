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


static SPINLOCK	session_spin = SPINLOCK_INIT;
static SESSION	*allSessions = NULL;

/**
 * Allocate a new session for a new client of the specified service.
 *
 * Create the link to the router session by calling the newSession
 * entry point of the router using the router instance of the
 * service this session is part of.
 *
 * @param service	The service this connection was established by
 * @param client	The client side DCB
 * @return		The newly created session or NULL if an error occured
 */
SESSION *
session_alloc(SERVICE *service, DCB *client)
{
        SESSION 	*session;

        session = (SESSION *)calloc(1, sizeof(SESSION));
        ss_info_dassert(session != NULL, "Allocating memory for session failed.");
        
        if (session == NULL) {
                int eno = errno;
                errno = 0;
                skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "%lu [session_alloc] Allocating memory for session "
                        "object failed. Errno %d, %s.",
                        pthread_self(),
                        eno,
                        strerror(eno));
		return NULL;
        }


        session->ses_chk_top = CHK_NUM_SESSION;
        session->ses_chk_tail = CHK_NUM_SESSION;
        spinlock_init(&session->ses_lock);
        /**
         * Prevent backend threads from accessing before session is completely
         * initialized.
         */
        spinlock_acquire(&session->ses_lock);
        session->service = service;
	session->client = client;
	memset(&session->stats, 0, sizeof(SESSION_STATS));
	session->stats.connect = time(0);
	session->state = SESSION_STATE_ALLOC;
        /**
	 * Associate the session to the client DCB and set the reference count on
	 * the session to indicate that there is a single reference to the session.
	 * There is no need to protect this or use atomic add as the session has not
	 * been made available to the other threads at this point.
         */
        session->data = client->data;
	client->session = session;
	session->refcount = 1;
        /** This indicates that session is ready to be shared with backend DCBs. */
        session->state = SESSION_STATE_READY;
        
        /** Release session lock */
        spinlock_release(&session->ses_lock);

	/*
	 * Only create a router session if we are not the listening 
	 * DCB. Creating a router session may create a connection to a
	 * backend server, depending upon the router module implementation
	 * and should be avoided for the listener session
	 *
	 * Router session creation may create other DCBs that link to the
	 * session, therefore it is important that the session lock is relinquished
	 * beforethe router call.
	 */
	if (client->state != DCB_STATE_LISTENING)
	{
		session->router_session =
                    service->router->newSession(service->router_instance, session);
	}
	spinlock_acquire(&session_spin);
	session->next = allSessions;
	allSessions = session;
	spinlock_release(&session_spin);
	atomic_add(&service->stats.n_sessions, 1);
	atomic_add(&service->stats.n_current, 1);
        CHK_SESSION(session);
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

        CHK_SESSION(session);

	spinlock_acquire(&session->ses_lock);
	if (atomic_add(&session->refcount, -1) > 1)
	{
		/*
		 * There are still other references to the session
		 * so we simply return after decrementing the session
		 * count.
		 */
		spinlock_release(&session->ses_lock);
		goto return_succp;
	}
	session->state = SESSION_STATE_FREE;
	spinlock_release(&session->ses_lock);

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
        free(session->router_session);
	free(session);
        succp = true;
        
return_succp :
        return succp;
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
	dcb_printf(dcb, "Session %p\n", ptr);
	dcb_printf(dcb, "\tState:    		%s\n", session_state(ptr->state));
	dcb_printf(dcb, "\tService:		%s (%p)\n", ptr->service->name, ptr->service);
	dcb_printf(dcb, "\tClient DCB:		%p\n", ptr->client);
	if (ptr->client && ptr->client->remote)
		dcb_printf(dcb, "\tClient Address:		%s\n", ptr->client->remote);
	dcb_printf(dcb, "\tConnected:		%s", asctime(localtime(&ptr->stats.connect)));
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
	case SESSION_STATE_LISTENER:
		return "Listener Session";
	case SESSION_STATE_LISTENER_STOPPED:
		return "Stopped Listener Session";
	default:
		return "Invalid State";
	}
}
