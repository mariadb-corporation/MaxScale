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
#include <skygw_utils.h>
#include <log_manager.h>
#include <string.h>
#include <hint.h>
#include <query_classifier.h>
#include <regex.h>

/**
 * @file slavelag.c - a very simple filter designed to send queries to the 
 * master server after data modification has occurred. This is done to prevent
 * replication lag affecting the outcome of a select query.
 * 
 * @verbatim
 *
 * Two optional parameters that define the behavior after a data modifying query
 * is executed:
 *
 *      count=<number of queries>   Queries to route to master after data modification.
 *      time=<time period>          Seconds to wait before queries are routed to slaves.
 *      match=<regex>               Regex for matching
 *      ignore=<regex>              Regex for ignoring
 *
 * The filter also has two options: @c case, which makes the regex case-sensitive, and @c ignorecase, which does the opposite.
 * Date		Who		Description
 * 03/03/2015	Markus Mäkelä	Written for demonstrative purposes
 * @endverbatim
 */

MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_GA,
	FILTER_VERSION,
	"A routing hint filter that send queries to the master after data modification"
};

static char *version_str = "V1.1.0";

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
    NULL,		// No Upstream requirement
    routeQuery,
    NULL,
    diagnostic,
};

struct LAGSTATS{
	    int		n_add_count;	/*< No. of statements diverted based on count */
	    int		n_add_time;	/*< No. of statements diverted based on time */
	    int		n_modified;	/*< No. of statements not diverted */
};
/**
 * Instance structure
 */
typedef struct {
	char	*match;		/* Regular expression to match */
	char	*nomatch;	/* Regular expression to ignore */
	int	time;		/*< The number of seconds to wait before routing queries
				 * to slave servers after a data modification operation
				 * is done. */
	int count;		/*< Number of hints to add after each operation
				 * that modifies data. */
	struct LAGSTATS stats;
	regex_t	re;		/* Compiled regex text of match */
	regex_t	nore;		/* Compiled regex text of ignore */
} LAG_INSTANCE;

/**
 * The session structure for this filter
 */
typedef struct {
	DOWNSTREAM	down;		/*< The downstream filter */
	int		hints_left;	/*< Number of hints left to add to queries*/
	time_t		last_modification; /*< Time of the last modifying operation */
	int		active;		/*< Is filter active */
} LAG_SESSION;

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
 * The module initialization routine, called when the module
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
LAG_INSTANCE	*my_instance;
int		i,cflags = 0;

	if ((my_instance = calloc(1, sizeof(LAG_INSTANCE))) != NULL)
	{
	    my_instance->count = 0;
	    my_instance->time = 0;
	    my_instance->stats.n_add_count = 0;
	    my_instance->stats.n_add_time = 0;
	    my_instance->stats.n_modified = 0;
	    my_instance->match = NULL;
	    my_instance->nomatch = NULL;

	    for (i = 0; params && params[i]; i++)
	    {
		if (!strcmp(params[i]->name, "count"))
		    my_instance->count = atoi(params[i]->value);
		else if (!strcmp(params[i]->name, "time"))
		    my_instance->time = atoi(params[i]->value);
		else if (!strcmp(params[i]->name, "match"))
		    my_instance->match = strdup(params[i]->value);
		else if (!strcmp(params[i]->name, "ignore"))
		    my_instance->nomatch = strdup(params[i]->value);
		else
		{
		    MXS_ERROR("lagfilter: Unexpected parameter '%s'.\n",
                              params[i]->name);
		}
	    }

		if (options)
		{
		    for (i = 0; options[i]; i++)
		    {
			if (!strcasecmp(options[i], "ignorecase"))
			{
			    cflags |= REG_ICASE;
			}
			else if (!strcasecmp(options[i], "case"))
			{
			    cflags &= ~REG_ICASE;
			}
			else
			{
			    MXS_ERROR("lagfilter: unsupported option '%s'.",
                                      options[i]);
			}
		    }
		}

	    if(my_instance->match)
	    {
		if(regcomp(&my_instance->re,my_instance->match,cflags))
		{
		    MXS_ERROR("lagfilter: Failed to compile regex '%s'.",
                              my_instance->match);
		}
	    }
	    if(my_instance->nomatch)
	    {
		if(regcomp(&my_instance->nore,my_instance->nomatch,cflags))
		{
		    MXS_ERROR("lagfilter: Failed to compile regex '%s'.",
                              my_instance->nomatch)));
		}
	    }
	}
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
LAG_INSTANCE	*my_instance = (LAG_INSTANCE *)instance;
LAG_SESSION	*my_session;

	if ((my_session = malloc(sizeof(LAG_SESSION))) != NULL)
	{
		my_session->active = 1;
		my_session->hints_left = 0;
		my_session->last_modification = 0;
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
LAG_SESSION	*my_session = (LAG_SESSION *)session;

	my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * If the regular expressed configured in the match parameter of the
 * filter definition matches the SQL text then add the hint
 * "Route to named server" with the name defined in the server parameter
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int	
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
LAG_INSTANCE	*my_instance = (LAG_INSTANCE *)instance;
LAG_SESSION	*my_session = (LAG_SESSION *)session;
char			*sql;
time_t now = time(NULL);

	if (modutil_is_SQL(queue))
	{
	    if (queue->next != NULL)
	    {
		queue = gwbuf_make_contiguous(queue);
	    }

	    if(!query_is_parsed(queue))
	    {
		parse_query(queue);
	    }

	    if(query_classifier_get_operation(queue) & (QUERY_OP_DELETE|QUERY_OP_INSERT|QUERY_OP_UPDATE))
	    {
		if((sql = modutil_get_SQL(queue)) != NULL)
		{
		    if(my_instance->nomatch == NULL||(my_instance->nomatch && regexec(&my_instance->nore,sql,0,NULL,0) != 0))
		    {
			if(my_instance->match == NULL||
			 (my_instance->match && regexec(&my_instance->re,sql,0,NULL,0) == 0))
			{
			    my_session->hints_left = my_instance->count;
			    my_session->last_modification = now;
			    my_instance->stats.n_modified++;
			}
		    }
		    free(sql);
		}
	    }
	    else if(my_session->hints_left > 0)
	    {
		queue->hint = hint_create_route(queue->hint,
					 HINT_ROUTE_TO_MASTER,
					 NULL);
		my_session->hints_left--;
		my_instance->stats.n_add_count++;
	    }
	    else if(difftime(now,my_session->last_modification) < my_instance->time)
	    {
		queue->hint = hint_create_route(queue->hint,
					 HINT_ROUTE_TO_MASTER,
					 NULL);
		my_instance->stats.n_add_time++;
	    }
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
LAG_INSTANCE	*my_instance = (LAG_INSTANCE *)instance;
LAG_SESSION	*my_session = (LAG_SESSION *)fsession;

	dcb_printf(dcb, "Configuration:\n\tCount: %d\n",
			my_instance->count);
	dcb_printf(dcb, "\tTime: %d seconds\n\n",
		 my_instance->time);
	dcb_printf(dcb, "Statistics:\n");
	dcb_printf(dcb, "\tNo. of data modifications: %d\n",
		 my_instance->stats.n_modified);
	dcb_printf(dcb, "\tNo. of hints added based on count: %d\n",
		 my_instance->stats.n_add_count);
	dcb_printf(dcb, "\tNo. of hints added based on time: %d\n",
		 my_instance->stats.n_add_time);

}
