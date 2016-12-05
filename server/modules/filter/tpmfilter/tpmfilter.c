/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
 *  filename=<name of the file to which transaction performance logs are written (default=tpm.log)>
 *  delimiter=<delimiter for columns in a log (default='|')>
 *  query_delimiter=<delimiter for query statements in a transaction (default=';')>
 *  source=<source address to limit filter>
 *  user=<username to limit filter>
 *
 * Date         Who             Description
 * 06/12/2015   Dong Young Yoon Initial implementation
 *
 * @endverbatim
 */

#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <maxscale/alloc.h>
#include <maxscale/filter.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/log_manager.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <maxscale/atomic.h>
#include <maxscale/query_classifier.h>

/** Defined in log_manager.cc */
extern int            lm_enabled_logfiles_bitmask;
extern size_t         log_ses_count[];

MODULE_INFO     info =
{
    MODULE_API_FILTER,
    MODULE_GA,
    FILTER_VERSION,
    "Transaction Performance Monitoring filter"
};

static char *version_str = "V1.0.0";
static size_t buf_size = 10;
static size_t sql_size_limit = 64 * 1024 *
                               1024; /* The maximum size for query statements in a transaction (64MB) */
static const int default_sql_size = 4 * 1024;
static const char* default_query_delimiter = "@@@";
static const char* default_log_delimiter = ":::";

/*
 * The filter entry points
 */
static  FILTER  *createInstance(const char *name, char **options, FILTER_PARAMETER **);
static  void    *newSession(FILTER *instance, SESSION *session);
static  void    closeSession(FILTER *instance, void *session);
static  void    MXS_FREESession(FILTER *instance, void *session);
static  void    setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static  void    setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static  int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static  int clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static  void    diagnostic(FILTER *instance, void *fsession, DCB *dcb);
static uint64_t getCapabilities(void);

static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    MXS_FREESession,
    setDownstream,
    setUpstream,
    routeQuery,
    clientReply,
    diagnostic,
    getCapabilities,
    NULL, // No destroyInstance
};

/**
 * A instance structure, every instance will write to a same file.
 */
typedef struct
{
    int sessions;   /* Session count */
    char    *source;    /* The source of the client connection */
    char    *user;  /* The user name to filter on */
    char    *filename;  /* filename */
    char    *delimiter; /* delimiter for columns in a log */
    char    *query_delimiter; /* delimiter for query statements in a transaction */

    int query_delimiter_size; /* the length of the query delimiter */
    FILE* fp;
} TPM_INSTANCE;

/**
 * The session structure for this TPM filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct
{
    DOWNSTREAM  down;
    UPSTREAM    up;
    int     active;
    char        *clientHost;
    char        *userName;
    char* sql;
    struct timeval  start;
    char        *current;
    int     n_statements;
    struct timeval  total;
    struct timeval  current_start;
    bool query_end;
    char    *buf;
    int sql_index;
    size_t      max_sql_size;
} TPM_SESSION;

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
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static  FILTER  *
createInstance(const char *name, char **options, FILTER_PARAMETER **params)
{
    int     i;
    TPM_INSTANCE    *my_instance;

    if ((my_instance = calloc(1, sizeof(TPM_INSTANCE))) != NULL)
    {
        my_instance->source = NULL;
        my_instance->user = NULL;

        /* set default log filename */
        my_instance->filename = MXS_STRDUP_A("tpm.log");
        /* set default delimiter */
        my_instance->delimiter = MXS_STRDUP_A("|");
        /* set default query delimiter */
        my_instance->query_delimiter = MXS_STRDUP_A(";");
        my_instance->query_delimiter_size = 1;

        for (i = 0; params && params[i]; i++)
        {
            if (!strcmp(params[i]->name, "filename"))
            {
                MXS_FREE(my_instance->filename);
                my_instance->filename = MXS_STRDUP_A(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "source"))
            {
                my_instance->source = MXS_STRDUP_A(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "user"))
            {
                my_instance->user = MXS_STRDUP_A(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "delimiter"))
            {
                MXS_FREE(my_instance->delimiter);
                my_instance->delimiter = MXS_STRDUP_A(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "query_delimiter"))
            {
                MXS_FREE(my_instance->query_delimiter);
                my_instance->query_delimiter = MXS_STRDUP_A(params[i]->value);
                my_instance->query_delimiter_size = strlen(my_instance->query_delimiter);
            }
        }
        my_instance->sessions = 0;
        my_instance->fp = fopen(my_instance->filename, "w");
        if (my_instance->fp == NULL)
        {
            MXS_ERROR("Opening output file '%s' for tpmfilter failed due to %d, %s", my_instance->filename, errno,
                      strerror(errno));
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
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static  void    *
newSession(FILTER *instance, SESSION *session)
{
    TPM_INSTANCE    *my_instance = (TPM_INSTANCE *)instance;
    TPM_SESSION *my_session;
    int     i;
    char        *remote, *user;

    if ((my_session = calloc(1, sizeof(TPM_SESSION))) != NULL)
    {
        atomic_add(&my_instance->sessions, 1);

        my_session->max_sql_size = default_sql_size; // default max query size of 4k.
        my_session->sql = (char*)malloc(my_session->max_sql_size);
        memset(my_session->sql, 0x00, my_session->max_sql_size);
        my_session->buf = (char*)malloc(buf_size);
        my_session->sql_index = 0;
        my_session->n_statements = 0;
        my_session->total.tv_sec = 0;
        my_session->total.tv_usec = 0;
        my_session->current = NULL;
        if ((remote = session_get_remote(session)) != NULL)
        {
            my_session->clientHost = MXS_STRDUP_A(remote);
        }
        else
        {
            my_session->clientHost = NULL;
        }
        if ((user = session_getUser(session)) != NULL)
        {
            my_session->userName = MXS_STRDUP_A(user);
        }
        else
        {
            my_session->userName = NULL;
        }
        my_session->active = 1;
        if (my_instance->source && my_session->clientHost && strcmp(my_session->clientHost,
                                                                    my_instance->source))
        {
            my_session->active = 0;
        }
        if (my_instance->user && my_session->userName && strcmp(my_session->userName,
                                                                my_instance->user))
        {
            my_session->active = 0;
        }
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static  void
closeSession(FILTER *instance, void *session)
{
    TPM_SESSION *my_session = (TPM_SESSION *)session;
    TPM_INSTANCE    *my_instance = (TPM_INSTANCE *)instance;
    if (my_instance->fp != NULL)
    {
        // flush FP when a session is closed.
        fflush(my_instance->fp);
    }

}

/**
 * MXS_FREE the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void
MXS_FREESession(FILTER *instance, void *session)
{
    TPM_SESSION *my_session = (TPM_SESSION *)session;

    MXS_FREE(my_session->clientHost);
    MXS_FREE(my_session->userName);
    MXS_FREE(my_session->sql);
    MXS_FREE(my_session->buf);
    MXS_FREE(session);
    return;
}

/**
 * Set the downstream filter or router to which queries will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param downstream    The downstream filter or router.
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    TPM_SESSION *my_session = (TPM_SESSION *)session;

    my_session->down = *downstream;
}

/**
 * Set the upstream filter or session to which results will be
 * passed from this filter.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param upstream  The upstream filter or session.
 */
static void
setUpstream(FILTER *instance, void *session, UPSTREAM *upstream)
{
    TPM_SESSION *my_session = (TPM_SESSION *)session;

    my_session->up = *upstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query should normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static  int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    TPM_INSTANCE    *my_instance = (TPM_INSTANCE *)instance;
    TPM_SESSION *my_session = (TPM_SESSION *)session;
    char        *ptr = NULL;
    size_t i;

    if (my_session->active)
    {
        uint32_t query_type = qc_get_type(queue);
        if ((ptr = modutil_get_SQL(queue)) != NULL)
        {
            my_session->query_end = false;

            /* check for commit and rollback */
            if (query_type & QUERY_TYPE_COMMIT)
            {
                my_session->query_end = true;
            }
            else if (query_type & QUERY_TYPE_ROLLBACK)
            {
                my_session->query_end = true;
                my_session->sql_index = 0;
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
                    MXS_ERROR("The size of query statements exceeds the maximum buffer limit of 64MB.");
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
                        MXS_ERROR("Memory allocation failure.");
                        goto retblock;
                    }
                    memcpy(new_sql, my_session->sql, my_session->sql_index);
                    MXS_FREE(my_session->sql);
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
                    memcpy(my_session->sql + my_session->sql_index, my_instance->query_delimiter,
                           my_instance->query_delimiter_size);
                    /* append the next query statement */
                    memcpy(my_session->sql + my_session->sql_index + my_instance->query_delimiter_size, ptr, strlen(ptr));
                    /* set new pointer for the buffer */
                    my_session->sql_index += (my_instance->query_delimiter_size + strlen(ptr));
                }
            }
        }
    }

retblock:

    MXS_FREE(ptr);
    /* Pass the query downstream */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
}

static int
clientReply(FILTER *instance, void *session, GWBUF *reply)
{
    TPM_INSTANCE    *my_instance = (TPM_INSTANCE *)instance;
    TPM_SESSION *my_session = (TPM_SESSION *)session;
    struct      timeval     tv, diff;
    int     i, inserted;

    /* found 'commit' and sql statements exist. */
    if (my_session->query_end && my_session->sql_index > 0)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->current_start), &diff);

        /* get latency */
        uint64_t millis = (diff.tv_sec * (uint64_t)1000 + diff.tv_usec / 1000);
        /* get timestamp */
        uint64_t timestamp = (tv.tv_sec + (tv.tv_usec / (1000 * 1000)));

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
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static  void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    TPM_INSTANCE    *my_instance = (TPM_INSTANCE *)instance;
    TPM_SESSION *my_session = (TPM_SESSION *)fsession;
    int     i;

    if (my_instance->source)
        dcb_printf(dcb, "\t\tLimit logging to connections from 	%s\n",
                   my_instance->source);
    if (my_instance->user)
        dcb_printf(dcb, "\t\tLimit logging to user		%s\n",
                   my_instance->user);
    if (my_instance->filename)
        dcb_printf(dcb, "\t\tLogging to file %s.\n",
                   my_instance->filename);
    if (my_instance->delimiter)
        dcb_printf(dcb, "\t\tLogging with delimiter %s.\n",
                   my_instance->delimiter);
    if (my_instance->query_delimiter)
        dcb_printf(dcb, "\t\tLogging with query delimiter %s.\n",
                   my_instance->query_delimiter);
}

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(void)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}
