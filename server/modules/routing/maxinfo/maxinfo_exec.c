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
 * @file maxinfo_parse.c - Parse the limited set of SQL that the MaxScale
 * information schema can use
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 17/02/15	Mark Riddoch	Initial implementation
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
#include <monitor.h>
#include <version.h>
#include <modinfo.h>
#include <modutil.h>
#include <atomic.h>
#include <spinlock.h>
#include <dcb.h>
#include <poll.h>
#include <maxinfo.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <resultset.h>
#include <maxconfig.h>

extern int lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

static void exec_show(DCB *dcb, MAXINFO_TREE *tree);
static void exec_select(DCB *dcb, MAXINFO_TREE *tree);
static void exec_show_variables(DCB *dcb, MAXINFO_TREE *filter);
static void exec_show_status(DCB *dcb, MAXINFO_TREE *filter);
static int maxinfo_pattern_match(char *pattern, char *str);

/**
 * Execute a parse tree and return the result set or runtime error
 *
 * @param dcb	The DCB that connects to the client
 * @param tree	The parse tree for the query
 */
void
maxinfo_execute(DCB *dcb, MAXINFO_TREE *tree)
{
	switch (tree->op)
	{
	case MAXOP_SHOW:
		exec_show(dcb, tree);
		break;
	case MAXOP_SELECT:
		exec_select(dcb, tree);
		break;
	case MAXOP_TABLE:
	case MAXOP_COLUMNS:
	case MAXOP_LITERAL:
	case MAXOP_PREDICATE:
	case MAXOP_LIKE:
	case MAXOP_EQUAL:
	default:
		maxinfo_send_error(dcb, 0, "Unexpected operator in parse tree");
	}
}

/**
 * Fetch the list of services and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_services(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = serviceGetList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of listeners and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_listeners(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = serviceGetListenerList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of sessions and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_sessions(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = sessionGetList(SESSION_LIST_ALL)) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of client sessions and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_clients(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = sessionGetList(SESSION_LIST_CONNECTION)) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of servers and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_servers(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = serverGetList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of modules and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_modules(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = moduleGetList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the list of monitors and stream as a result set
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_monitors(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = monitorGetList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * Fetch the event times data
 *
 * @param dcb	DCB to which to stream result set
 * @param tree	Potential like clause (currently unused)
 */
static void
exec_show_eventTimes(DCB *dcb, MAXINFO_TREE *tree)
{
RESULTSET	*set;

	if ((set = eventTimesGetList()) == NULL)
		return;
	
	resultset_stream_mysql(set, dcb);
	resultset_free(set);
}

/**
 * The table of show commands that are supported
 */
static struct {
	char	*name;
	void (*func)(DCB *, MAXINFO_TREE *);
} show_commands[] = { 
	{ "variables", exec_show_variables },
	{ "status", exec_show_status },
	{ "services", exec_show_services },
	{ "listeners", exec_show_listeners },
	{ "sessions", exec_show_sessions },
	{ "clients", exec_show_clients },
	{ "servers", exec_show_servers },
	{ "modules", exec_show_modules },
	{ "monitors", exec_show_monitors },
	{ "eventTimes", exec_show_eventTimes },
	{ NULL, NULL }
};

/**
 * Execute a show command parse tree and return the result set or runtime error
 *
 * @param dcb	The DCB that connects to the client
 * @param tree	The parse tree for the query
 */
static void
exec_show(DCB *dcb, MAXINFO_TREE *tree)
{
int	i;
char	errmsg[120];

	for (i = 0; show_commands[i].name; i++)
	{
		if (strcasecmp(show_commands[i].name, tree->value) == 0)
		{
			(*show_commands[i].func)(dcb, tree->right);
			return;
		}
	}
	if (strlen(tree->value) > 80)	// Prevent buffer overrun
		tree->value[80] = 0;
	sprintf(errmsg, "Unsupported show command '%s'", tree->value);
	maxinfo_send_error(dcb, 0, errmsg);
	LOGIF(LM, (skygw_log_write(LOGFILE_MESSAGE, errmsg)));
}

/**
 * Return the current MaxScale version
 *
 * @return The version string for MaxScale
 */
static char *
getVersion()
{
	return MAXSCALE_VERSION;
}

static char *versionComment = "MariaDB MaxScale";
/**
 * Return the current MaxScale version
 *
 * @return The version string for MaxScale
 */
static char *
getVersionComment()
{
	return versionComment;
}

/**
 * Return the current MaxScale Home Directory
 *
 * @return The version string for MaxScale
 */
static char *
getMaxScaleHome()
{
	return getenv("MAXSCALE_HOME");
}

/* The various methods to fetch the variables */
#define	VT_STRING	1
#define	VT_INT		2

extern int MaxScaleUptime();

typedef void *(*STATSFUNC)();
/**
 * Variables that may be sent in a show variables
 */
static struct {
	char		*name;
	int		type;
	STATSFUNC	func;
} variables[] = {
	{ "version", VT_STRING, (STATSFUNC)getVersion },
	{ "version_comment", VT_STRING, (STATSFUNC)getVersionComment },
	{ "basedir", VT_STRING, (STATSFUNC)getMaxScaleHome},
	{ "MAXSCALE_VERSION", VT_STRING, (STATSFUNC)getVersion },
	{ "MAXSCALE_THREADS", VT_INT, (STATSFUNC)config_threadcount },
	{ "MAXSCALE_NBPOLLS", VT_INT, (STATSFUNC)config_nbpolls },
	{ "MAXSCALE_POLLSLEEP", VT_INT, (STATSFUNC)config_pollsleep },
	{ "MAXSCALE_UPTIME", VT_INT, (STATSFUNC)MaxScaleUptime },
	{ "MAXSCALE_SESSIONS", VT_INT, (STATSFUNC)serviceSessionCountAll },
	{ NULL, 0, 	NULL }
};

typedef struct {
	int	index;
	char	*like;
} VARCONTEXT;

/**
 * Callback function to populate rows of the show variable
 * command
 *
 * @param data	The context point
 * @return	The next row or NULL if end of rows
 */
static RESULT_ROW *
variable_row(RESULTSET *result, void *data)
{
VARCONTEXT	*context = (VARCONTEXT *)data;
RESULT_ROW	*row;
char		buf[80];

	if (variables[context->index].name)
	{
		if (context->like &&
			 maxinfo_pattern_match(context->like,
					variables[context->index].name))
		{
			context->index++;
			return variable_row(result, data);
		}
		row = resultset_make_row(result);
		resultset_row_set(row, 0, variables[context->index].name);
		switch (variables[context->index].type)
		{
		case VT_STRING:
			resultset_row_set(row, 1,
				(char *)(*variables[context->index].func)());
			break;
		case VT_INT:
			snprintf(buf, 80, "%ld",
				(long)(*variables[context->index].func)());
			resultset_row_set(row, 1, buf);
			break;
		}
		context->index++;
		return row;
	}
	return NULL;
}

/**
 * Execute a show variables command applying an optional filter
 *
 * @param dcb		The DCB connected to the client
 * @param filter	A potential like clause or NULL
 */
static void
exec_show_variables(DCB *dcb, MAXINFO_TREE *filter)
{
RESULTSET	*result;
VARCONTEXT	context;

	if (filter)
		context.like = filter->value;
	else
		context.like = NULL;
	context.index = 0;

	if ((result = resultset_create(variable_row, &context)) == NULL)
	{
		maxinfo_send_error(dcb, 0, "No resources available");
		return;
	}
	resultset_add_column(result, "Variable_name", 40, COL_TYPE_VARCHAR);
	resultset_add_column(result, "Value", 40, COL_TYPE_VARCHAR);
	resultset_stream_mysql(result, dcb);
	resultset_free(result);
}

/**
 * Return the show variables output a a result set
 *
 * @return Variables as a result set
 */
RESULTSET *
maxinfo_variables()
{
RESULTSET	*result;
static VARCONTEXT	context;

	context.like = NULL;
	context.index = 0;

	if ((result = resultset_create(variable_row, &context)) == NULL)
	{
		return NULL;
	}
	resultset_add_column(result, "Variable_name", 40, COL_TYPE_VARCHAR);
	resultset_add_column(result, "Value", 40, COL_TYPE_VARCHAR);
	return result;
}

/**
 * Interface to dcb_count_by_usage for all dcbs
 */
static int
maxinfo_all_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_ALL);
}

/**
 * Interface to dcb_count_by_usage for client dcbs
 */
static int
maxinfo_client_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_CLIENT);
}

/**
 * Interface to dcb_count_by_usage for listener dcbs
 */
static int
maxinfo_listener_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_LISTENER);
}

/**
 * Interface to dcb_count_by_usage for backend dcbs
 */
static int
maxinfo_backend_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_BACKEND);
}

/**
 * Interface to dcb_count_by_usage for internal dcbs
 */
static int
maxinfo_internal_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_INTERNAL);
}

/**
 * Interface to dcb_count_by_usage for zombie dcbs
 */
static int
maxinfo_zombie_dcbs()
{
	return dcb_count_by_usage(DCB_USAGE_ZOMBIE);
}

/**
 * Interface to poll stats for reads
 */
static int
maxinfo_read_events()
{
	return poll_get_stat(POLL_STAT_READ);
}

/**
 * Interface to poll stats for writes
 */
static int
maxinfo_write_events()
{
	return poll_get_stat(POLL_STAT_WRITE);
}

/**
 * Interface to poll stats for errors
 */
static int
maxinfo_error_events()
{
	return poll_get_stat(POLL_STAT_ERROR);
}

/**
 * Interface to poll stats for hangup
 */
static int
maxinfo_hangup_events()
{
	return poll_get_stat(POLL_STAT_HANGUP);
}

/**
 * Interface to poll stats for accepts
 */
static int
maxinfo_accept_events()
{
	return poll_get_stat(POLL_STAT_ACCEPT);
}

/**
 * Interface to poll stats for event queue length
 */
static int
maxinfo_event_queue_length()
{
	return poll_get_stat(POLL_STAT_EVQ_LEN);
}

/**
 * Interface to poll stats for event pending queue length
 */
static int
maxinfo_event_pending_queue_length()
{
	return poll_get_stat(POLL_STAT_EVQ_PENDING);
}

/**
 * Interface to poll stats for max event queue length
 */
static int
maxinfo_max_event_queue_length()
{
	return poll_get_stat(POLL_STAT_EVQ_MAX);
}

/**
 * Interface to poll stats for max queue time
 */
static int
maxinfo_max_event_queue_time()
{
	return poll_get_stat(POLL_STAT_MAX_QTIME);
}

/**
 * Interface to poll stats for max event execution time
 */
static int
maxinfo_max_event_exec_time()
{
	return poll_get_stat(POLL_STAT_MAX_EXECTIME);
}

/**
 * Variables that may be sent in a show status
 */
static struct {
	char		*name;
	int		type;
	STATSFUNC	func;
} status[] = {
	{ "Uptime", VT_INT, (STATSFUNC)MaxScaleUptime },
	{ "Uptime_since_flush_status", VT_INT, (STATSFUNC)MaxScaleUptime },
	{ "Threads_created", VT_INT, (STATSFUNC)config_threadcount },
	{ "Threads_running", VT_INT, (STATSFUNC)config_threadcount },
	{ "Threadpool_threads", VT_INT, (STATSFUNC)config_threadcount },
	{ "Threads_connected", VT_INT, (STATSFUNC)serviceSessionCountAll },
	{ "Connections", VT_INT, (STATSFUNC)maxinfo_all_dcbs },
	{ "Client_connections", VT_INT, (STATSFUNC)maxinfo_client_dcbs },
	{ "Backend_connections", VT_INT, (STATSFUNC)maxinfo_backend_dcbs },
	{ "Listeners", VT_INT, (STATSFUNC)maxinfo_listener_dcbs },
	{ "Zombie_connections", VT_INT, (STATSFUNC)maxinfo_zombie_dcbs },
	{ "Internal_descriptors", VT_INT, (STATSFUNC)maxinfo_internal_dcbs },
	{ "Read_events", VT_INT, (STATSFUNC)maxinfo_read_events },
	{ "Write_events", VT_INT, (STATSFUNC)maxinfo_write_events },
	{ "Hangup_events", VT_INT, (STATSFUNC)maxinfo_hangup_events },
	{ "Error_events", VT_INT, (STATSFUNC)maxinfo_error_events },
	{ "Accept_events", VT_INT, (STATSFUNC)maxinfo_accept_events },
	{ "Event_queue_length", VT_INT, (STATSFUNC)maxinfo_event_queue_length },
	{ "Pending_events", VT_INT, (STATSFUNC)maxinfo_event_pending_queue_length },
	{ "Max_event_queue_length", VT_INT, (STATSFUNC)maxinfo_max_event_queue_length },
	{ "Max_event_queue_time", VT_INT, (STATSFUNC)maxinfo_max_event_queue_time },
	{ "Max_event_execution_time", VT_INT, (STATSFUNC)maxinfo_max_event_exec_time },
	{ NULL, 0, 	NULL }
};

/**
 * Callback function to populate rows of the show variable
 * command
 *
 * @param data	The context point
 * @return	The next row or NULL if end of rows
 */
static RESULT_ROW *
status_row(RESULTSET *result, void *data)
{
VARCONTEXT	*context = (VARCONTEXT *)data;
RESULT_ROW	*row;
char		buf[80];

	if (status[context->index].name)
	{
		if (context->like &&
			 maxinfo_pattern_match(context->like,
					status[context->index].name))
		{
			context->index++;
			return status_row(result, data);
		}
		row = resultset_make_row(result);
		resultset_row_set(row, 0, status[context->index].name);
		switch (status[context->index].type)
		{
		case VT_STRING:
			resultset_row_set(row, 1,
				(char *)(*status[context->index].func)());
			break;
		case VT_INT:
			snprintf(buf, 80, "%ld",
				(long)(*status[context->index].func)());
			resultset_row_set(row, 1, buf);
			break;
		}
		context->index++;
		return row;
	}
	return NULL;
}

/**
 * Execute a show status command applying an optional filter
 *
 * @param dcb		The DCB connected to the client
 * @param filter	A potential like clause or NULL
 */
static void
exec_show_status(DCB *dcb, MAXINFO_TREE *filter)
{
RESULTSET	*result;
VARCONTEXT	context;

	if (filter)
		context.like = filter->value;
	else
		context.like = NULL;
	context.index = 0;

	if ((result = resultset_create(status_row, &context)) == NULL)
	{
		maxinfo_send_error(dcb, 0, "No resources available");
		return;
	}
	resultset_add_column(result, "Variable_name", 40, COL_TYPE_VARCHAR);
	resultset_add_column(result, "Value", 40, COL_TYPE_VARCHAR);
	resultset_stream_mysql(result, dcb);
	resultset_free(result);
}

/**
 * Return the show status data as a result set
 *
 * @return The show status data as a result set
 */
RESULTSET *
maxinfo_status()
{
RESULTSET	*result;
static VARCONTEXT	context;

	context.like = NULL;
	context.index = 0;

	if ((result = resultset_create(status_row, &context)) == NULL)
	{
		return NULL;
	}
	resultset_add_column(result, "Variable_name", 40, COL_TYPE_VARCHAR);
	resultset_add_column(result, "Value", 40, COL_TYPE_VARCHAR);
	return result;
}


/**
 * Execute a select command parse tree and return the result set
 * or runtime error
 *
 * @param dcb	The DCB that connects to the client
 * @param tree	The parse tree for the query
 */
static void
exec_select(DCB *dcb, MAXINFO_TREE *tree)
{
	maxinfo_send_error(dcb, 0, "Select not yet implemented");
}

/**
 * Perform a "like" pattern match. Only works for leading and trailing %
 *
 * @param pattern	Pattern to match
 * @param str		String to match against pattern
 * @return 		Zero on match
 */
static int
maxinfo_pattern_match(char *pattern, char *str)
{
int	anchor = 0, len, trailing;
char	*fixed;
extern	char *strcasestr();

	if (*pattern != '%')
	{
		fixed = pattern;
		anchor = 1;
	}
	else
	{
		fixed = &pattern[1];
	}
	len = strlen(fixed);
	if (fixed[len - 1] == '%')
		trailing = 1;
	else
		trailing = 0;
	if (anchor == 1 && trailing == 0)	// No wildcard
		return strcasecmp(pattern, str);
	else if (anchor == 1)
		return strncasecmp(str, pattern, len - trailing);
	else
	{
		char *portion = malloc(len + 1);
		int rval;
		strncpy(portion, fixed, len - trailing);
		portion[len - trailing] = 0;
		rval = (strcasestr(str, portion) != NULL ? 0 : 1);
		free(portion);
		return rval;
	}
}
