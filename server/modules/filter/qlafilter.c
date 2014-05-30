/*
 * This file is distributed as part of MaxScale by SkySQL.  It is free
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
 * Copyright SkySQL Ab 2014
 */
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>

static char *version_str = "V1.0.0";

static	FILTER	*createInstance(char **options);
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
    routeQuery,
    diagnostic,
};

/**
 * A dummy instance structure
 */
typedef struct {
	int	sessions;
	char	*filebase;
} QLA_INSTANCE;

/**
 * A dummy session structure for this test filter
 */
typedef struct {
	DOWNSTREAM	down;
	int		mysession;
	int		fd;
} QLA_SESSION;

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
 *
 * @return The instance data for this new instance
 */
static	FILTER	*
createInstance(char **options)
{
QLA_INSTANCE	*my_instance;

	if ((my_instance = calloc(1, sizeof(QLA_INSTANCE))) != NULL)
	{
		if (options)
			my_instance->filebase = strdup(options[0]);
		else
			my_instance->filebase = strdup("qla");
		my_instance->sessions = 0;
	}
	return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the router.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
QLA_INSTANCE	*my_instance = (QLA_INSTANCE *)instance;
QLA_SESSION	*my_session;
char		filename[100];

	if ((my_session = calloc(1, sizeof(QLA_SESSION))) != NULL)
	{
		sprintf(filename, "%s.%d", my_instance->filebase,
				my_instance->sessions);
printf("Open file %s\n", filename);
		my_instance->sessions++;
		my_session->fd = open(filename, O_WRONLY|O_CREAT, 0666);
	}

	return my_session;
}

/**
 * Close a session with the router, this is the mechanism
 * by which a router may cleanup data structure etc.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
QLA_SESSION	*my_session;

	close(my_session->fd);
}

static void
freeSession(FILTER *instance, void *session)
{
        return;
}

static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
QLA_SESSION	*my_session = (QLA_SESSION *)session;

	my_session->down = *downstream;
}

static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
QLA_SESSION	*my_session = (QLA_SESSION *)session;
unsigned char	*ptr;
unsigned int	length;

	ptr = GWBUF_DATA(queue);
	length = *ptr++;
	length += (*ptr++ << 8);
	length += (*ptr++ << 8);
	ptr++;	// Skip sequence id
	if (*ptr++ == 0x03)	// COM_QUERY
	{
		write(my_session->fd, ptr, length - 1);
		write(my_session->fd, "\n", 1);
	}
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
QLA_INSTANCE	*my_instance = instance;
QLA_SESSION	*my_session = fsession;

}
