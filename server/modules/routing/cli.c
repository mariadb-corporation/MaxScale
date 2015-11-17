/*
 * This file is distributed as part of MaxScale.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file cli.c - A "routing module" that in fact merely gives access
 * to a command line interface
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 18/06/13	Mark Riddoch	Initial implementation
 * 13/06/14	Mark Riddoch	Creted from the debugcli
 *
 * @endverbatim
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <service.h>
#include <session.h>
#include <router.h>
#include <modules.h>
#include <modinfo.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <debugcli.h>
#include <skygw_utils.h>
#include <log_manager.h>


MODULE_INFO 	info = {
	MODULE_API_ROUTER,
	MODULE_GA,
	ROUTER_VERSION,
	"The admin user interface"
};

static char *version_str = "V1.0.0";

/* The router entry points */
static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *router_session);
static	void 	freeSession(ROUTER *instance, void *router_session);
static	int	execute(ROUTER *instance, void *router_session, GWBUF *queue);
static	void	diagnostics(ROUTER *instance, DCB *dcb);
static  int getCapabilities ();

/** The module object definition */
static ROUTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    execute,
    diagnostics,
    NULL,
    NULL,
    getCapabilities
};

extern int execute_cmd(CLI_SESSION *cli);

static SPINLOCK		instlock;
static CLI_INSTANCE	*instances;

/**
 * Implementation of the mandatory version entry point
 *
 * @return version string of the module
 */
char *
version()
{
	return version_str;
}

/**
 * The module initialisation routine, called when the module
 * is first loaded.
 */
void
ModuleInit()
{
	MXS_NOTICE("Initialise CLI router module %s.", version_str);
	spinlock_init(&instlock);
	instances = NULL;
}

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
ROUTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 * @param options	Any array of options for the query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
CLI_INSTANCE	*inst;
int		i;

	if ((inst = malloc(sizeof(CLI_INSTANCE))) == NULL)
		return NULL;

	inst->service = service;
	spinlock_init(&inst->lock);
	inst->sessions = NULL;
	inst->mode = CLIM_USER;

	if (options)
	{
		for (i = 0; options[i]; i++)
		{
                    MXS_ERROR("Unknown option for CLI '%s'", options[i]);
		}
	}

	/*
	 * We have completed the creation of the instance data, so now
	 * insert this router instance into the linked list of routers
	 * that have been created with this module.
	 */
	spinlock_acquire(&instlock);
	inst->next = instances;
	instances = inst;
	spinlock_release(&instlock);

	return (ROUTER *)inst;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The router instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(ROUTER *instance, SESSION *session)
{
CLI_INSTANCE	*inst = (CLI_INSTANCE *)instance;
CLI_SESSION	*client;

	if ((client = (CLI_SESSION *)malloc(sizeof(CLI_SESSION))) == NULL)
	{
		return NULL;
	}
	client->session = session;

	memset(client->cmdbuf, 0, 80);

	spinlock_acquire(&inst->lock);
	client->next = inst->sessions;
	inst->sessions = client;
	spinlock_release(&inst->lock);

	session->state = SESSION_STATE_READY;
	client->mode = inst->mode;

	return (void *)client;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance		The router instance data
 * @param router_session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *router_session)
{
CLI_INSTANCE	*inst = (CLI_INSTANCE *)instance;
CLI_SESSION	*session = (CLI_SESSION *)router_session;


	spinlock_acquire(&inst->lock);
	if (inst->sessions == session)
		inst->sessions = session->next;
	else
	{
		CLI_SESSION *ptr = inst->sessions;
		while (ptr && ptr->next != session)
			ptr = ptr->next;
		if (ptr)
			ptr->next = session->next;
	}
	spinlock_release(&inst->lock);
        /**
         * Router session is freed in session.c:session_close, when session who
         * owns it, is freed.
         */
}

/**
 * Free a debugcli session
 *
 * @param router_instance	The router session
 * @param router_client_session	The router session as returned from newSession
 */
static void freeSession(
        ROUTER* router_instance,
        void*   router_client_session)
{
	free(router_client_session);
        return;
}

/**
 * We have data from the client, we must route it to the backend.
 * This is simply a case of sending it to the connection that was
 * chosen when we started the client session.
 *
 * @param instance		The router instance
 * @param router_session	The router session returned from the newSession call
 * @param queue			The queue of data buffers to route
 * @return The number of bytes sent
 */
static	int	
execute(ROUTER *instance, void *router_session, GWBUF *queue)
{
CLI_SESSION	*session = (CLI_SESSION *)router_session;

	/* Extract the characters */
	while (queue)
	{
		strncat(session->cmdbuf, GWBUF_DATA(queue), MIN(GWBUF_LENGTH(queue),cmdbuflen-1));
		queue = gwbuf_consume(queue, GWBUF_LENGTH(queue));
	}

	execute_cmd(session);
	return 1;
}

/**
 * Display router diagnostics
 *
 * @param instance	Instance of the router
 * @param dcb		DCB to send diagnostics to
 */
static	void
diagnostics(ROUTER *instance, DCB *dcb)
{
	return;	/* Nothing to do currently */
}

static int getCapabilities()
{
        return 0;
}
