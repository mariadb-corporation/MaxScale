/*
 * This file is distributed as part of MaxScale by MariaDB Corporation.  It is free
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
#include <stdio.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <atomic.h>

/**
 * @file testfilter.c - a very simple test filter.
 * @verbatim
 *
 * This filter is a very simple example used to test the filter API,
 * it merely counts the number of statements that flow through the
 * filter pipeline.
 *
 * Reporting is done via the diagnostics print routine.
 * @endverbatim
 */

MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_BETA_RELEASE,
	FILTER_VERSION,
	"A simple query counting filter"
};

static char *version_str = "V1.0.0";

static	FILTER	*createInstance(char **options, FILTER_PARAMETER **params);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL,		// No upstream requirement
    routeQuery,
    NULL,
    diagnostic,
};

/**
 * A dummy instance structure
 */
typedef struct {
	int	sessions;
} TEST_INSTANCE;

/**
 * A dummy session structure for this test filter
 */
typedef struct {
	DOWNSTREAM	down;
	int		count;
} TEST_SESSION;

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
FILTER_OBJECT *
GetModuleObject()
{
	return &MyObject;
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 * 
 * @param options	The options for this filter
 * @param params	The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options, FILTER_PARAMETER **params)
{
TEST_INSTANCE	*my_instance;

	if ((my_instance = calloc(1, sizeof(TEST_INSTANCE))) != NULL)
		my_instance->sessions = 0;
	return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
TEST_INSTANCE	*my_instance = (TEST_INSTANCE *)instance;
TEST_SESSION	*my_session;

	if ((my_session = calloc(1, sizeof(TEST_SESSION))) != NULL)
	{
		atomic_add(&my_instance->sessions,1);
		my_session->count = 0;
	}

	return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static void
freeSession(FILTER *instance, void *session)
{
	free(session);
        return;
}

/**
 * Set the downstream component for this filter.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 * @param downstream	The downstream filter or router
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
TEST_SESSION	*my_session = (TEST_SESSION *)session;

	my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query shoudl normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
TEST_SESSION	*my_session = (TEST_SESSION *)session;

	if (modutil_is_SQL(queue))
		my_session->count++;
	return my_session->down.routeQuery(my_session->down.instance,
			my_session->down.session, queue);
}

/**
 * Diagnostics routine
 *
 * If fsession is NULL then print diagnostics on the filter
 * instance as a whole, otherwise print diagnostics for the
 * particular session.
 *
 * @param	instance	The filter instance
 * @param	fsession	Filter session, may be NULL
 * @param	dcb		The DCB for diagnostic output
 */
static	void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
TEST_INSTANCE	*my_instance = (TEST_INSTANCE *)instance;
TEST_SESSION	*my_session = (TEST_SESSION *)fsession;

	if (my_session)
		dcb_printf(dcb, "\t\tNo. of queries routed by filter: %d\n",
			my_session->count);
	else
		dcb_printf(dcb, "\t\tNo. of sessions created: %d\n",
			my_instance->sessions);
}
