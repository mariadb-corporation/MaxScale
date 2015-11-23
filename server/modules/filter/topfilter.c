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
 * @file topfilter.c - Top N Longest Running Queries
 * @verbatim
 *
 * TOPN Filter - Query Log All. A primitive query logging filter, simply
 * used to verify the filter mechanism for downstream filters. All queries
 * that are passed through the filter will be written to file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * A single option may be passed to the filter, this is the name of the
 * file to which the queries are logged. A serial number is appended to this
 * name in order that each session logs to a different file.
 *
 * Date         Who             Description
 * 18/06/2014   Mark Riddoch    Addition of source and user filters
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

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_GA,
    FILTER_VERSION,
    "A top N query logging filter"
};

static char *version_str = "V1.0.1";

/*
 * The filter entry points
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static void setUpstream(FILTER *instance, void *fsession, UPSTREAM *upstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static int clientReply(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject =
{
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
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
typedef struct
{
    int sessions; /* Session count */
    int topN; /* Number of queries to store */
    char *filebase; /* Base of fielname to log into */
    char *source; /* The source of the client connection */
    char *user; /* A user name to filter on */
    char *match; /* Optional text to match against */
    regex_t re; /* Compiled regex text */
    char *exclude; /* Optional text to match against for exclusion */
    regex_t exre; /* Compiled regex nomatch text */
} TOPN_INSTANCE;

/**
 * Structure to hold the Top N queries
 */
typedef struct topnq
{
    struct timeval duration;
    char *sql;
} TOPNQ;

/**
 * The session structure for this TOPN filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct
{
    DOWNSTREAM down;
    UPSTREAM up;
    int active;
    char *clientHost;
    char *userName;
    char *filename;
    int fd;
    struct timeval start;
    char *current;
    TOPNQ **top;
    int n_statements;
    struct timeval total;
    struct timeval connect;
    struct timeval disconnect;
} TOPN_SESSION;

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
static FILTER *
createInstance(char **options, FILTER_PARAMETER **params)
{
    int i;
    TOPN_INSTANCE *my_instance;

    if ((my_instance = calloc(1, sizeof(TOPN_INSTANCE))) != NULL)
    {
        my_instance->topN = 10;
        my_instance->match = NULL;
        my_instance->exclude = NULL;
        my_instance->source = NULL;
        my_instance->user = NULL;
        my_instance->filebase = strdup("top");
        for (i = 0; params && params[i]; i++)
        {
            if (!strcmp(params[i]->name, "count"))
            {
                my_instance->topN = atoi(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "filebase"))
            {
                free(my_instance->filebase);
                my_instance->filebase = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "match"))
            {
                my_instance->match = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "exclude"))
            {
                my_instance->exclude = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "source"))
            {
                my_instance->source = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "user"))
            {
                my_instance->user = strdup(params[i]->value);
            }
            else if (!filter_standard_parameter(params[i]->name))
            {
                MXS_ERROR("topfilter: Unexpected parameter '%s'.",
                          params[i]->name);
            }
        }
        if (options)
        {
            MXS_ERROR("topfilter: Options are not supported by this "
                      " filter. They will be ignored.");
        }
        my_instance->sessions = 0;
        if (my_instance->match &&
            regcomp(&my_instance->re, my_instance->match, REG_ICASE))
        {
            MXS_ERROR("topfilter: Invalid regular expression '%s'"
                      " for the match parameter.",
                      my_instance->match);
            free(my_instance->match);
            free(my_instance->source);
            free(my_instance->user);
            free(my_instance->filebase);
            free(my_instance);
            return NULL;
        }
        if (my_instance->exclude &&
            regcomp(&my_instance->exre, my_instance->exclude,
                    REG_ICASE))
        {
            MXS_ERROR("qlafilter: Invalid regular expression '%s'"
                      " for the nomatch paramter.\n",
                      my_instance->match);
            regfree(&my_instance->re);
            free(my_instance->match);
            free(my_instance->source);
            free(my_instance->user);
            free(my_instance->filebase);
            free(my_instance);
            return NULL;
        }
    }
    return(FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * Create the file to log to and open it.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    TOPN_INSTANCE *my_instance = (TOPN_INSTANCE *) instance;
    TOPN_SESSION *my_session;
    int i;
    char *remote, *user;

    if ((my_session = calloc(1, sizeof(TOPN_SESSION))) != NULL)
    {
        if ((my_session->filename =
             (char *) malloc(strlen(my_instance->filebase) + 20))
            == NULL)
        {
            free(my_session);
            return NULL;
        }
        sprintf(my_session->filename, "%s.%d", my_instance->filebase,
                my_instance->sessions);
        atomic_add(&my_instance->sessions, 1);
        my_session->top = (TOPNQ **) calloc(my_instance->topN + 1,
                                            sizeof(TOPNQ *));
        for (i = 0; i < my_instance->topN; i++)
        {
            my_session->top[i] = (TOPNQ *) calloc(1, sizeof(TOPNQ));
            my_session->top[i]->sql = NULL;
        }
        my_session->n_statements = 0;
        my_session->total.tv_sec = 0;
        my_session->total.tv_usec = 0;
        my_session->current = NULL;
        if ((remote = session_get_remote(session)) != NULL)
        {
            my_session->clientHost = strdup(remote);
        }
        else
        {
            my_session->clientHost = NULL;
        }
        if ((user = session_getUser(session)) != NULL)
        {
            my_session->userName = strdup(user);
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

        sprintf(my_session->filename, "%s.%d", my_instance->filebase,
                my_instance->sessions);
        gettimeofday(&my_session->connect, NULL);
    }

    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the TOPN filter we simple close the file descriptor.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(FILTER *instance, void *session)
{
    TOPN_INSTANCE *my_instance = (TOPN_INSTANCE *) instance;
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;
    struct timeval diff;
    int i;
    FILE *fp;
    int statements;

    gettimeofday(&my_session->disconnect, NULL);
    timersub((&my_session->disconnect), &(my_session->connect), &diff);
    if ((fp = fopen(my_session->filename, "w")) != NULL)
    {
        statements = my_session->n_statements != 0 ? my_session->n_statements : 1;

        fprintf(fp, "Top %d longest running queries in session.\n",
                my_instance->topN);
        fprintf(fp, "==========================================\n\n");
        fprintf(fp, "Time (sec) | Query\n");
        fprintf(fp, "-----------+-----------------------------------------------------------------\n");
        for (i = 0; i < my_instance->topN; i++)
        {
            if (my_session->top[i]->sql)
            {
                fprintf(fp, "%10.3f |  %s\n",
                        (double) ((my_session->top[i]->duration.tv_sec * 1000)
                                  + (my_session->top[i]->duration.tv_usec / 1000)) / 1000,
                        my_session->top[i]->sql);
            }
        }
        fprintf(fp, "-----------+-----------------------------------------------------------------\n");
        struct tm tm;
        localtime_r(&my_session->connect.tv_sec, &tm);
        char buffer[32]; // asctime_r documentation requires 26
        asctime_r(&tm, buffer);
        fprintf(fp, "\n\nSession started %s", buffer);
        if (my_session->clientHost)
        {
            fprintf(fp, "Connection from %s\n",
                    my_session->clientHost);
        }
        if (my_session->userName)
        {
            fprintf(fp, "Username        %s\n",
                    my_session->userName);
        }
        fprintf(fp, "\nTotal of %d statements executed.\n",
                statements);
        fprintf(fp, "Total statement execution time   %5d.%d seconds\n",
                (int) my_session->total.tv_sec,
                (int) my_session->total.tv_usec / 1000);
        fprintf(fp, "Average statement execution time %9.3f seconds\n",
                (double) ((my_session->total.tv_sec * 1000)
                          + (my_session->total.tv_usec / 1000))
                / (1000 * statements));
        fprintf(fp, "Total connection time            %5d.%d seconds\n",
                (int) diff.tv_sec, (int) diff.tv_usec / 1000);
        fclose(fp);
    }
}

/**
 * Free the memory associated with the session
 *
 * @param instance  The filter instance
 * @param session   The filter session
 */
static void
freeSession(FILTER *instance, void *session)
{
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;

    free(my_session->filename);
    free(session);
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
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;

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
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;

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
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    TOPN_INSTANCE *my_instance = (TOPN_INSTANCE *) instance;
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;
    char *ptr;

    if (my_session->active)
    {
        if (queue->next != NULL)
        {
            queue = gwbuf_make_contiguous(queue);
        }
        if ((ptr = modutil_get_SQL(queue)) != NULL)
        {
            if ((my_instance->match == NULL ||
                 regexec(&my_instance->re, ptr, 0, NULL, 0) == 0) &&
                (my_instance->exclude == NULL ||
                 regexec(&my_instance->exre, ptr, 0, NULL, 0) != 0))
            {
                my_session->n_statements++;
                if (my_session->current)
                {
                    free(my_session->current);
                }
                gettimeofday(&my_session->start, NULL);
                my_session->current = ptr;
            }
            else
            {
                free(ptr);
            }
        }
    }
    /* Pass the query downstream */
    return my_session->down.routeQuery(my_session->down.instance,
                                       my_session->down.session, queue);
}

static int
cmp_topn(const void *va, const void *vb)
{
    TOPNQ **a = (TOPNQ **) va;
    TOPNQ **b = (TOPNQ **) vb;

    if ((*b)->duration.tv_sec == (*a)->duration.tv_sec)
    {
        return(*b)->duration.tv_usec - (*a)->duration.tv_usec;
    }
    return(*b)->duration.tv_sec - (*a)->duration.tv_sec;
}

static int
clientReply(FILTER *instance, void *session, GWBUF *reply)
{
    TOPN_INSTANCE *my_instance = (TOPN_INSTANCE *) instance;
    TOPN_SESSION *my_session = (TOPN_SESSION *) session;
    struct timeval tv, diff;
    int i, inserted;

    if (my_session->current)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->start), &diff);

        timeradd(&(my_session->total), &diff, &(my_session->total));

        inserted = 0;
        for (i = 0; i < my_instance->topN; i++)
        {
            if (my_session->top[i]->sql == NULL)
            {
                my_session->top[i]->sql = my_session->current;
                my_session->top[i]->duration = diff;
                inserted = 1;
                break;
            }
        }

        if (inserted == 0 && ((diff.tv_sec > my_session->top[my_instance->topN - 1]->duration.tv_sec) || (diff.tv_sec == my_session->top[my_instance->topN - 1]->duration.tv_sec && diff.tv_usec > my_session->top[my_instance->topN - 1]->duration.tv_usec)))
        {
            free(my_session->top[my_instance->topN - 1]->sql);
            my_session->top[my_instance->topN - 1]->sql = my_session->current;
            my_session->top[my_instance->topN - 1]->duration = diff;
            inserted = 1;
        }

        if (inserted)
        {
            qsort(my_session->top, my_instance->topN,
                  sizeof(TOPNQ *), cmp_topn);
        }
        else
        {
            free(my_session->current);
        }
        my_session->current = NULL;
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
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    TOPN_INSTANCE *my_instance = (TOPN_INSTANCE *) instance;
    TOPN_SESSION *my_session = (TOPN_SESSION *) fsession;
    int i;

    dcb_printf(dcb, "\t\tReport size            %d\n",
               my_instance->topN);
    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tLimit logging to connections from  %s\n",
                   my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb, "\t\tLimit logging to user      %s\n",
                   my_instance->user);
    }
    if (my_instance->match)
    {
        dcb_printf(dcb, "\t\tInclude queries that match     %s\n",
                   my_instance->match);
    }
    if (my_instance->exclude)
    {
        dcb_printf(dcb, "\t\tExclude queries that match     %s\n",
                   my_instance->exclude);
    }
    if (my_session)
    {
        dcb_printf(dcb, "\t\tLogging to file %s.\n",
                   my_session->filename);
        dcb_printf(dcb, "\t\tCurrent Top %d:\n", my_instance->topN);
        for (i = 0; i < my_instance->topN; i++)
        {
            if (my_session->top[i]->sql)
            {
                dcb_printf(dcb, "\t\t%d place:\n", i + 1);
                dcb_printf(dcb, "\t\t\tExecution time: %.3f seconds\n",
                           (double) ((my_session->top[i]->duration.tv_sec * 1000)
                                     + (my_session->top[i]->duration.tv_usec / 1000)) / 1000);
                dcb_printf(dcb, "\t\t\tSQL: %s\n",
                           my_session->top[i]->sql);
            }
        }
    }
}
