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
#include <stdio.h>
#include <router.h>

static char *version_str = "V1.0.0";

static	ROUTER	*createInstance(SERVICE *service);
static	void	*newSession(ROUTER *instance, SESSION *session);
static	void 	closeSession(ROUTER *instance, void *session);
static	int	routeQuery(ROUTER *instance, void *session, GWBUF *queue);

static ROUTER_OBJECT MyObject = { createInstance, newSession, closeSession, routeQuery };

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
	fprintf(stderr, "Initial test router module.\n");
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
	fprintf(stderr, "Returing test router module object.\n");
	return &MyObject;
}

/**
 * Create an instance of the router for a particular service
 * within the gateway.
 * 
 * @param service	The service this router is being create for
 *
 * @return The instance data for this new instance
 */
static	ROUTER	*
createInstance(SERVICE *service)
{
	return NULL;
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
	return NULL;
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


static	int	
routeQuery(ROUTER *instance, void *session, GWBUF *queue)
{
	return 0;
}
