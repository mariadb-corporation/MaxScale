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

/**
 * @file tpmfilter.c - Transaction Performance Monitoring Filter
 * @verbatim
 *
 * A simple filter that groups queries into a transaction with the latency.
 *
 * The filter reads the routed queries, groups them into a transaction by
 * detecting 'commit' statement at the end. The transactions are timestamped with a
 * unix-timestamp and the latency of a transaction is recorded in milliseconds.
 * The filter will not record transactions that are rolled back.
 * Please note that the filter only works with 'autocommit' option disabled.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * Optional parameters:
 *	filename=<name of the file to which transaction performance logs are written (default=tpm.log)>
 *	delimiter=<delimiter for columns in a log (default='|')>
 *	query_delimiter=<delimiter for query statements in a transaction (default=';')>
 *	source=<source address to limit filter>
 *	user=<username to limit filter>
 *
 * Date		Who		Description
 * 06/12/2015	Dong Young Yoon	Initial implementation
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
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <atomic.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];
extern __thread log_info_t tls_log_info;

MODULE_INFO 	info = {
	MODULE_API_FILTER,
	MODULE_GA,
	FILTER_VERSION,
	"Transaction Performance Monitoring filter"
};

static char *version_str = "V1.0.0";
static size_t buf_size = 10;
static size_t sql_size_limit = 64 * 1024 * 1024; /* The maximum size for query statements in a transaction (64MB) */

/*
 * The filter entry points
 */
static	FILTER	*createInstance(char **options, FILTER_PARAMETER **);
static	void	*newSession(FILTER *instance, SESSION *session);
static	void 	closeSession(FILTER *instance, void *session);
static	void 	freeSession(FILTER *instance, void *session);
static	void	setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static	void	setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static	int	routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static	int	clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static	void	diagnostic(FILTER *instance, void *fsession, DCB *dcb);

static FILTER_OBJECT MyObject = {
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    setUpstream,
    routeQuery,
    clientReply,
    diagnostic,
};

/**
 * A instance structure, every instance will write to a same file.
 */
typedef struct {
	int	sessions;	/* Session count */
	char	*source;	/* The source of the client connection */
	char	*user;	/* The user name to filter on */
	char	*filename;	/* filename */
	char	*delimiter; /* delimiter for columns in a log */
	char	*query_delimiter; /* delimiter for query statements in a transaction */

	int query_delimiter_size; /* the length of the query delimiter */
	FILE* fp;
} DBS_INSTANCE;

/**
 * The session structure for this DBS filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct {
	DOWNSTREAM	down;
	UPSTREAM	up;
	int		active;
	char		*clientHost;
	char		*userName;
	char* sql;
	struct timeval	start;
	char		*current;
	int		n_statements;
	struct timeval	total;
	struct timeval	current_start;
	bool query_end;
	char	*buf;
	int	sql_index;
	size_t		max_sql_size;
} DBS_SESSION;

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
int		i;
DBS_INSTANCE	*my_instance;

	if ((my_instance = calloc(1, sizeof(DBS_INSTANCE))) != NULL)
	{
		my_instance->source = NULL;
		my_instance->user = NULL;

		/* set default log filename */
		my_instance->filename = strdup("tpm.log");
		/* set default delimiter */
		my_instance->delimiter = strdup("|");
		/* set default query delimiter */
		my_instance->query_delimiter = strdup(";");
		my_instance->query_delimiter_size = 1;

		for (i = 0; params && params[i]; i++)
		{
			if (!strcmp(params[i]->name, "filename"))
			{
				free(my_instance->filename);
				my_instance->filename = strdup(params[i]->value);
			}
			else if (!strcmp(params[i]->name, "source"))
				my_instance->source = strdup(params[i]->value);
			else if (!strcmp(params[i]->name, "user"))
				my_instance->user = strdup(params[i]->value);
			else if (!strcmp(params[i]->name, "delimiter"))
			{
				free(my_instance->delimiter);
				my_instance->delimiter = strdup(params[i]->value);
			}
			else if (!strcmp(params[i]->name, "query_delimiter"))
			{
				free(my_instance->query_delimiter);
				my_instance->query_delimiter = strdup(params[i]->value);
				my_instance->query_delimiter_size = strlen(my_instance->query_delimiter);
			}
		}
		my_instance->sessions = 0;
	  my_instance->fp = fopen(my_instance->filename, "w");
		if (my_instance->fp == NULL)
		{
			skygw_log_write(LOGFILE_ERROR, "Error: Opening output file '%s' for tpmfilter failed due to %d, %s", my_instance->filename, errno, strerror(errno));
			return NULL;
		}
	}
	return (FILTER *)my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Every session uses the same log file.
 *
 * @param instance	The filter instance data
 * @param session	The session itself
 * @return Session specific data for this session
 */
static	void	*
newSession(FILTER *instance, SESSION *session)
{
DBS_INSTANCE	*my_instance = (DBS_INSTANCE *)instance;
DBS_SESSION	*my_session;
int		i;
char		*remote, *user;

	if ((my_session = calloc(1, sizeof(DBS_SESSION))) != NULL)
	{
		atomic_add(&my_instance->sessions,1);

		my_session->max_sql_size = 4 * 1024; // default max query size of 4k.
		my_session->sql = (char*)malloc(my_session->max_sql_size);
		memset(my_session->sql, 0x00, my_session->max_sql_size);
		my_session->buf = (char*)malloc(buf_size);
		my_session->sql_index = 0;
		my_session->n_statements = 0;
		my_session->total.tv_sec = 0;
		my_session->total.tv_usec = 0;
		my_session->current = NULL;
		if ((remote = session_get_remote(session)) != NULL)
			my_session->clientHost = strdup(remote);
		else
			my_session->clientHost = NULL;
		if ((user = session_getUser(session)) != NULL)
			my_session->userName = strdup(user);
		else
			my_session->userName = NULL;
		my_session->active = 1;
		if (my_instance->source && my_session->clientHost && strcmp(my_session->clientHost,
							my_instance->source))
			my_session->active = 0;
		if (my_instance->user && my_session->userName && strcmp(my_session->userName,
							my_instance->user))
			my_session->active = 0;
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
	DBS_SESSION	*my_session = (DBS_SESSION *)session;
	DBS_INSTANCE	*my_instance = (DBS_INSTANCE *)instance;
	if (my_instance->fp != NULL)
	{
		// flush FP when a session is closed.
		fflush(my_instance->fp);
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
DBS_SESSION	*my_session = (DBS_SESSION *)session;

	free(my_session->clientHost);
	free(my_session->userName);
	free(my_session->sql);
	free(my_session->buf);
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
DBS_SESSION	*my_session = (DBS_SESSION *)session;

	my_session->down = *downstream;
}

/**
 * Set the upstream filter or session to which results will be
 * passed from this filter.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param upstream	The upstream filter or session.
 */
static void
setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
DBS_SESSION	*my_session = (DBS_SESSION *)session;

	my_session->up = *upstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance	The filter instance data
 * @param session	The filter session
 * @param queue		The query data
 */
static	int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
DBS_INSTANCE	*my_instance = (DBS_INSTANCE *)instance;
DBS_SESSION	*my_session = (DBS_SESSION *)session;
char		*ptr = NULL;
size_t i;

	if (my_session->active)
	{
		if (queue->next != NULL)
		{
			queue = gwbuf_make_contiguous(queue);
		}
		if ((ptr = modutil_get_SQL(queue)) != NULL)
		{
			my_session->query_end = false;
			/* check for commit and rollback */
			if (strlen(ptr) > 5)
			{
				size_t ptr_size = strlen(ptr)+1;
				char* buf = my_session->buf;
				for (i=0; i < ptr_size && i < buf_size; ++i)
				{
					buf[i] = tolower(ptr[i]);
				}
				if (strncmp(buf, "commit", 6) == 0)
				{
					my_session->query_end = true;
				}
				else if (strncmp(buf, "rollback", 8) == 0)
				{
					my_session->query_end = true;
					my_session->sql_index = 0;
				}
			}

			/* for normal sql statements */
			if (!my_session->query_end)
			{
				 /* check and expand buffer size first. */
				size_t new_sql_size = my_session->max_sql_size;
				size_t len = my_session->sql_index + strlen(ptr) + my_instance->query_delimiter_size + 1;

				/* if the total length of query statements exceeds the maximum limit, print an error and return */
				if (len > sql_size_limit)
				{
						skygw_log_write(LOGFILE_ERROR, "Error: The size of query statements exceeds the maximum buffer limit of 64MB.");
						goto retblock;
				}

				/* double buffer size until the buffer fits the query */
				while (len > new_sql_size)
				{
					new_sql_size *= 2;
				}
				if (new_sql_size > my_session->max_sql_size)
				{
					char* new_sql = (char*)malloc(new_sql_size);
					if (new_sql == NULL)
					{
						skygw_log_write(LOGFILE_ERROR, "Error: Memory allocation failure.");
						goto retblock;
					}
					memcpy(new_sql, my_session->sql, my_session->sql_index);
					free(my_session->sql);
					my_session->sql = new_sql;
					my_session->max_sql_size = new_sql_size;
				}

				/* first statement */
				if (my_session->sql_index == 0)
				{
					memcpy(my_session->sql, ptr, strlen(ptr));
					my_session->sql_index += strlen(ptr);
					gettimeofday(&my_session->current_start, NULL);
				}
				/* otherwise, append the statement with semicolon as a statement delimiter */
				else
				{
					/* append a query delimiter */
					memcpy(my_session->sql + my_session->sql_index, my_instance->query_delimiter, my_instance->query_delimiter_size);
					/* append the next query statement */
					memcpy(my_session->sql + my_session->sql_index + my_instance->query_delimiter_size, ptr, strlen(ptr));
					/* set new pointer for the buffer */
					my_session->sql_index += (my_instance->query_delimiter_size + strlen(ptr));
				}
			}
		}
	}

retblock:

	free(ptr);
	/* Pass the query downstream */
	return my_session->down.routeQuery(my_session->down.instance,
			my_session->down.session, queue);
}

static int
clientReply(FILTER *instance, void *session, GWBUF *reply)
{
DBS_INSTANCE	*my_instance = (DBS_INSTANCE *)instance;
DBS_SESSION	*my_session = (DBS_SESSION *)session;
struct		timeval		tv, diff;
int		i, inserted;

/* found 'commit' and sql statements exist. */
	if (my_session->query_end && my_session->sql_index > 0)
	{
		gettimeofday(&tv, NULL);
		timersub(&tv, &(my_session->current_start), &diff);

		/* get latency */
		uint64_t millis = (diff.tv_sec * (uint64_t)1000 + diff.tv_usec / 1000);
		/* get timestamp */
		uint64_t timestamp = (tv.tv_sec + (tv.tv_usec / (1000*1000)));

		*(my_session->sql + my_session->sql_index) = '\0';

		/* print to log. */
		fprintf(my_instance->fp, "%ld%s%s%s%s%s%ld%s%s\n",
				timestamp,
				my_instance->delimiter,
				my_session->clientHost,
				my_instance->delimiter,
				my_session->userName,
				my_instance->delimiter,
				millis,
				my_instance->delimiter,
				my_session->sql);

		my_session->sql_index = 0;
	}

	/* Pass the result upstream */
	return my_session->up.clientReply(my_session->up.instance,
			my_session->up.session, reply);
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
DBS_INSTANCE	*my_instance = (DBS_INSTANCE *)instance;
DBS_SESSION	*my_session = (DBS_SESSION *)fsession;
int		i;

	if (my_instance->source)
		dcb_printf(dcb, "\t\tLimit logging to connections from 	%s\n",
				my_instance->source);
	if (my_instance->user)
		dcb_printf(dcb, "\t\tLimit logging to user		%s\n",
				my_instance->user);
	if (my_session)
	{
		dcb_printf(dcb, "\t\tLogging to file %s.\n",
			my_instance->filename);
	}
}
