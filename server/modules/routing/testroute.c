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
#include <stdio.h>
#include <router.h>
#include <modinfo.h>

static char *version_str = "V1.0.0";

MODULE_INFO 	info = {
	MODULE_API_ROUTER,
	MODULE_IN_DEVELOPMENT,
	ROUTER_VERSION,
	"A test router - not for use in real systems"
};

static	ROUTER	*createInstance(SERVICE *service, char **options);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *session);
static	void 	freeSession(ROUTER *instance, void *session);
static	int	routeQuery(ROUTER *instance, void *session, GWBUF *queue);
static  void	clientReply(ROUTER *instance, void *session, GWBUF *queue,DCB*);
static	void	diagnostic(ROUTER *instance, DCB *dcb);
static  int getCapabilities ();
static void handleError(
	ROUTER           *instance,
	void             *router_session,
	GWBUF            *errbuf,
	DCB              *backend_dcb,
	error_action_t   action,
	bool             *succp);

static ROUTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    routeQuery,
    diagnostic,
    clientReply,
    handleError,
    getCapabilities
};

typedef struct{
}TESTROUTER;

typedef struct{
}TESTSESSION;

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
 * @param options	The options for this query router
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service, char **options)
{
    
	return (ROUTER*)malloc(sizeof(TESTROUTER));
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
	return (SESSION*)malloc(sizeof(TESTSESSION));
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance	The router instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(ROUTER *instance, void *session)
{
}

static void freeSession(
        ROUTER* router_instance,
        void*   router_client_session)
{
    free(router_client_session);
}

static	int	
routeQuery(ROUTER *instance, void *session, GWBUF *queue)
{
	return 0;
}

void clientReply(ROUTER* instance, void* session, GWBUF* queue, DCB* dcb)
{
}

/**
 * Diagnostics routine
 *
 * @param	instance	The router instance
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(ROUTER *instance, DCB *dcb)
{
}

static int getCapabilities()
{
        return 0;
}


static void handleError(
	ROUTER           *instance,
	void             *router_session,
	GWBUF            *errbuf,
	DCB              *backend_dcb,
	error_action_t   action,
	bool             *succp)
{
}
