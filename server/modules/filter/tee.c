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

/**
 * @file tee.c	A filter that splits the processing pipeline in two
 * @verbatim
 *
 * Conditionally duplicate requests and send the duplicates to another service
 * within MaxScale.
 *
 * Parameters
 * ==========
 *
 * service	The service to send the duplicates to
 * source	The source address to match in order to duplicate (optional)
 * match	A regular expression to match in order to perform duplication
 *		of the request (optional)
 * nomatch	A regular expression to match in order to prevent duplication
 *		of the request (optional)
 * user		A user name to match against. If present only requests that
 *		originate from this user will be duplciated (optional)
 *
 * Revision History
 * ================
 *
 * Date		Who		Description
 * 20/06/2014	Mark Riddoch	Initial implementation
 * 24/06/2014	Mark Riddoch	Addition of support for multi-packet queries
 *
 * @endverbatim
 */
#include <stdio.h>
#include <fcntl.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <sys/time.h>
#include <regex.h>
#include <string.h>
#include <service.h>
#include <router.h>
#include <dcb.h>

extern int lm_enabled_logfiles_bitmask; 

MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_BETA_RELEASE,
	FILTER_VERSION,
	"A tee piece in the filter plumbing"
};

static char *version_str = "V1.0.0";

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
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
    NULL,		// No Upstream requirement
    routeQuery,
    NULL,		// No client reply
    diagnostic,
};

/**
 * The instance structure for the TEE filter - this holds the configuration
 * information for the filter.
 */
typedef struct {
	SERVICE	*service;	/* The service to duplicate requests to */
	char	*source;	/* The source of the client connection */
	char	*userName;	/* The user name to filter on */
	char	*match;		/* Optional text to match against */
	regex_t	re;		/* Compiled regex text */
	char	*nomatch;	/* Optional text to match against for exclusion */
	regex_t	nore;		/* Compiled regex nomatch text */
} TEE_INSTANCE;

/**
 * The session structure for this TEE filter.
 * This stores the downstream filter information, such that the	
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct {
	DOWNSTREAM	down;		/* The downstream filter */
	int		active;		/* filter is active? */
	DCB		*branch_dcb;	/* Client DCB for "branch" service */
	SESSION		*branch_session;/* The branch service session */
	int		n_duped;	/* Number of duplicated queries */
	int		n_rejected;	/* Number of rejected queries */
	int		residual;	/* Any outstanding SQL text */
} TEE_SESSION;

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
TEE_INSTANCE	*my_instance;
int		i;

	if ((my_instance = calloc(1, sizeof(TEE_INSTANCE))) != NULL)
	{
		if (options)
		{
			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"tee: The tee filter has been passed an option, "
				"this filter does not support any options.\n")));
		}
		my_instance->service = NULL;
		my_instance->source = NULL;
		my_instance->userName = NULL;
		my_instance->match = NULL;
		my_instance->nomatch = NULL;
		if (params)
		{
			for (i = 0; params[i]; i++)
			{
				if (!strcmp(params[i]->name, "service"))
				{
					if ((my_instance->service = service_find(params[i]->value)) == NULL)
					{
						LOGIF(LE, (skygw_log_write_flush(
							LOGFILE_ERROR,
							"tee: service '%s' "
							"not found.\n",
							params[i]->value)));
					}
				}
				else if (!strcmp(params[i]->name, "match"))
				{
					my_instance->match = strdup(params[i]->value);
				}
				else if (!strcmp(params[i]->name, "exclude"))
				{
					my_instance->nomatch = strdup(params[i]->value);
				}
				else if (!strcmp(params[i]->name, "source"))
					my_instance->source = strdup(params[i]->value);
				else if (!strcmp(params[i]->name, "user"))
					my_instance->userName = strdup(params[i]->value);
				else if (!filter_standard_parameter(params[i]->name))
				{
					LOGIF(LE, (skygw_log_write_flush(
						LOGFILE_ERROR,
						"tee: Unexpected parameter '%s'.\n",
						params[i]->name)));
				}
			}
		}
		if (my_instance->service == NULL)
		{
			free(my_instance->match);
			free(my_instance->source);
			free(my_instance);
			return NULL;
		}
		if (my_instance->match &&
			regcomp(&my_instance->re, my_instance->match, REG_ICASE))
		{
			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"tee: Invalid regular expression '%s'"
				" for the match parameter.\n",
					my_instance->match)));
			free(my_instance->match);
			free(my_instance->source);
			free(my_instance);
			return NULL;
		}
		if (my_instance->nomatch &&
			regcomp(&my_instance->nore, my_instance->nomatch,
								REG_ICASE))
		{
			LOGIF(LE, (skygw_log_write_flush(LOGFILE_ERROR,
				"tee: Invalid regular expression '%s'"
				" for the nomatch paramter.\n",
					my_instance->match)));
			if (my_instance->match)
				regfree(&my_instance->re);
			free(my_instance->match);
			free(my_instance->source);
			free(my_instance);
			return NULL;
		}
	}
	return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Create the file to log to and open it.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
TEE_INSTANCE	*my_instance = (TEE_INSTANCE *)instance;
TEE_SESSION	*my_session;
char		*remote, *userName;

	if ((my_session = calloc(1, sizeof(TEE_SESSION))) != NULL)
	{
		my_session->active = 1;
		my_session->residual = 0;
		if (my_instance->source 
			&& (remote = session_get_remote(session)) != NULL)
		{
			if (strcmp(remote, my_instance->source))
				my_session->active = 0;
		}
		userName = session_getUser(session);
		if (my_instance->userName && userName && strcmp(userName,
							my_instance->userName))
			my_session->active = 0;
		if (my_session->active)
		{
			my_session->branch_dcb = dcb_clone(session->client);
			my_session->branch_session = session_alloc(my_instance->service, my_session->branch_dcb);
		}
	}

	return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the tee filter we need to close down the
 * "branch" session.
 *
 * @param instance	The filter instance data
 * @param session	The session being closed
 */
static	void 	
closeSession(FILTER *instance, void *session)
{
TEE_SESSION	*my_session = (TEE_SESSION *)session;
ROUTER_OBJECT	*router;
void		*router_instance, *rsession;
SESSION		*bsession;

	if (my_session->active)
	{
		bsession = my_session->branch_session;
		router = bsession->service->router;
                router_instance = bsession->service->router_instance;
                rsession = bsession->router_session;
                /** Close router session and all its connections */
                router->closeSession(router_instance, rsession);
		dcb_free(my_session->branch_dcb);
		/* No need to free the session, this is done as
		 * a side effect of closing the client DCB of the
		 * session.
		 */
	}
}

/**
 * Free the memory associated with the session
 *
 * @param instance	The filter instance
 * @param session	The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
TEE_SESSION	*my_session = (TEE_SESSION *)session;

	free(session);
        return;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session 
 * @param downstream	The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
TEE_SESSION	*my_session = (TEE_SESSION *)session;

	my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * If my_session->residual is set then duplicate that many bytes
 * and send them to the branch.
 *
 * If my_session->residual is zero then this must be a new request
 * Extract the SQL text if possible, match against that text and forward
 * the request. If the requets is not contained witin the packet we have
 * then set my_session->residual to the number of outstanding bytes
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
TEE_INSTANCE	*my_instance = (TEE_INSTANCE *)instance;
TEE_SESSION	*my_session = (TEE_SESSION *)session;
char		*ptr;
int		length, rval, residual;
GWBUF		*clone = NULL;

	if (my_session->residual)
	{
		clone = gwbuf_clone(queue);
		if (my_session->residual < GWBUF_LENGTH(clone))
			GWBUF_RTRIM(clone, GWBUF_LENGTH(clone) - residual);
		my_session->residual -= GWBUF_LENGTH(clone);
		if (my_session->residual < 0)
			my_session->residual = 0;
	}
	else if (my_session->active &&
			modutil_MySQL_Query(queue, &ptr, &length, &residual))
	{
		if ((my_instance->match == NULL ||
			regexec(&my_instance->re, ptr, 0, NULL, 0) == 0) &&
			(my_instance->nomatch == NULL ||
				regexec(&my_instance->nore,ptr,0,NULL, 0) != 0))
		{
			clone = gwbuf_clone(queue);
			my_session->residual = residual;
		}
	}

	/* Pass the query downstream */
	rval = my_session->down.routeQuery(my_session->down.instance,
			my_session->down.session, queue);
	if (clone)
	{
		my_session->n_duped++;
		SESSION_ROUTE_QUERY(my_session->branch_session, clone);
	}
	else
	{
		my_session->n_rejected++;
	}
	return rval;
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
TEE_INSTANCE	*my_instance = (TEE_INSTANCE *)instance;
TEE_SESSION	*my_session = (TEE_SESSION *)fsession;

	if (my_instance->source)
		dcb_printf(dcb, "\t\tLimit to connections from 		%s\n",
				my_instance->source);
	dcb_printf(dcb, "\t\tDuplicate statements to service		%s\n",
				my_instance->service->name);
	if (my_instance->userName)
		dcb_printf(dcb, "\t\tLimit to user			%s\n",
				my_instance->userName);
	if (my_instance->match)
		dcb_printf(dcb, "\t\tInclude queries that match		%s\n",
				my_instance->match);
	if (my_instance->nomatch)
		dcb_printf(dcb, "\t\tExclude queries that match		%s\n",
				my_instance->nomatch);
	if (my_session)
	{
		dcb_printf(dcb, "\t\tNo. of statements duplicated:	%d.\n",
			my_session->n_duped);
		dcb_printf(dcb, "\t\tNo. of statements rejected:	%d.\n",
			my_session->n_rejected);
	}
}
