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
 * @file qlafilter.c - Quary Log All Filter
 * @verbatim
 *
 * QLA Filter - Query Log All. A primitive query logging filter, simply
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
 * 03/06/2014   Mark Riddoch    Initial implementation
 * 11/06/2014   Mark Riddoch    Addition of source and match parameters
 * 19/06/2014   Mark Riddoch    Addition of user parameter
 *
 * @endverbatim
 */

#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <string.h>
#include <atomic.h>

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_GA,
    FILTER_VERSION,
    "A simple query logging filter"
};

static char *version_str = "V1.1.1";

/*
 * The filter entry points
 */
static FILTER *createInstance(char **options, FILTER_PARAMETER **);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);


static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL, // No Upstream requirement
    routeQuery,
    NULL, // No client reply
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
    int sessions; /* The count of sessions */
    char *filebase; /* The filename base */
    char *source; /* The source of the client connection */
    char *userName; /* The user name to filter on */
    char *match; /* Optional text to match against */
    regex_t re; /* Compiled regex text */
    char *nomatch; /* Optional text to match against for exclusion */
    regex_t nore; /* Compiled regex nomatch text */
} QLA_INSTANCE;

/**
 * The session structure for this QLA filter.
 * This stores the downstream filter information, such that the
 * filter is able to pass the query on to the next filter (or router)
 * in the chain.
 *
 * It also holds the file descriptor to which queries are written.
 */
typedef struct
{
    DOWNSTREAM down;
    char *filename;
    FILE *fp;
    int active;
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
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static FILTER *
createInstance(char **options, FILTER_PARAMETER **params)
{
    QLA_INSTANCE *my_instance;
    int i;

    if ((my_instance = calloc(1, sizeof(QLA_INSTANCE))) != NULL)
    {
        if (options)
        {
            my_instance->filebase = strdup(options[0]);
        }
        else
        {
            my_instance->filebase = strdup("qla");
        }
        my_instance->source = NULL;
        my_instance->userName = NULL;
        my_instance->match = NULL;
        my_instance->nomatch = NULL;
        if (params)
        {
            for (i = 0; params[i]; i++)
            {
                if (!strcmp(params[i]->name, "match"))
                {
                    my_instance->match = strdup(params[i]->value);
                }
                else if (!strcmp(params[i]->name, "exclude"))
                {
                    my_instance->nomatch = strdup(params[i]->value);
                }
                else if (!strcmp(params[i]->name, "source"))
                {
                    my_instance->source = strdup(params[i]->value);
                }
                else if (!strcmp(params[i]->name, "user"))
                {
                    my_instance->userName = strdup(params[i]->value);
                }
                else if (!strcmp(params[i]->name, "filebase"))
                {
                    if (my_instance->filebase)
                    {
                        free(my_instance->filebase);
                        my_instance->filebase = NULL;
                    }
                    my_instance->filebase = strdup(params[i]->value);
                }
                else if (!filter_standard_parameter(params[i]->name))
                {
                    MXS_ERROR("qlafilter: Unexpected parameter '%s'.",
                              params[i]->name);
                }
            }
        }
        my_instance->sessions = 0;
        if (my_instance->match &&
            regcomp(&my_instance->re, my_instance->match, REG_ICASE))
        {
            MXS_ERROR("qlafilter: Invalid regular expression '%s'"
                      " for the match parameter.\n",
                      my_instance->match);
            free(my_instance->match);
            free(my_instance->source);
            if (my_instance->filebase)
            {
                free(my_instance->filebase);
            }
            free(my_instance);
            return NULL;
        }
        if (my_instance->nomatch &&
            regcomp(&my_instance->nore, my_instance->nomatch,
                    REG_ICASE))
        {
            MXS_ERROR("qlafilter: Invalid regular expression '%s'"
                      " for the nomatch paramter.",
                      my_instance->match);
            if (my_instance->match)
            {
                regfree(&my_instance->re);
            }
            free(my_instance->match);
            free(my_instance->source);
            if (my_instance->filebase)
            {
                free(my_instance->filebase);
            }
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
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session;
    char *remote, *userName;

    if ((my_session = calloc(1, sizeof(QLA_SESSION))) != NULL)
    {
        if ((my_session->filename =
             (char *) malloc(strlen(my_instance->filebase) + 20))
            == NULL)
        {
            char errbuf[STRERROR_BUFLEN];
            MXS_ERROR("Memory allocation for qla filter "
                      "file name failed due to %d, %s.",
                      errno,
                      strerror_r(errno, errbuf, sizeof(errbuf)));
            free(my_session);
            return NULL;
        }
        my_session->active = 1;

        if (my_instance->source
            && (remote = session_get_remote(session)) != NULL)
        {
            if (strcmp(remote, my_instance->source))
            {
                my_session->active = 0;
            }
        }
        userName = session_getUser(session);

        if (my_instance->userName &&
            userName &&
            strcmp(userName, my_instance->userName))
        {
            my_session->active = 0;
        }
        sprintf(my_session->filename, "%s.%d",
                my_instance->filebase,
                my_instance->sessions);

        // Multiple sessions can try to update my_instance->sessions simultaneously
        atomic_add(&(my_instance->sessions), 1);

        if (my_session->active)
        {
            my_session->fp = fopen(my_session->filename, "w");

            if (my_session->fp == NULL)
            {
                char errbuf[STRERROR_BUFLEN];
                MXS_ERROR("Opening output file for qla "
                          "fileter failed due to %d, %s",
                          errno,
                          strerror_r(errno, errbuf, sizeof(errbuf)));
                free(my_session->filename);
                free(my_session);
                my_session = NULL;
            }
        }
    }
    else
    {
        char errbuf[STRERROR_BUFLEN];
        MXS_ERROR("Memory allocation for qla filter failed due to "
                  "%d, %s.",
                  errno,
                  strerror_r(errno, errbuf, sizeof(errbuf)));
    }
    return my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 * In the case of the QLA filter we simple close the file descriptor.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(FILTER *instance, void *session)
{
    QLA_SESSION *my_session = (QLA_SESSION *) session;

    if (my_session->active && my_session->fp)
    {
        fclose(my_session->fp);
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
    QLA_SESSION *my_session = (QLA_SESSION *) session;

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
    QLA_SESSION *my_session = (QLA_SESSION *) session;

    my_session->down = *downstream;
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
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session = (QLA_SESSION *) session;
    char *ptr;
    int length = 0;
    struct tm t;
    struct timeval tv;

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
                (my_instance->nomatch == NULL ||
                 regexec(&my_instance->nore, ptr, 0, NULL, 0) != 0))
            {
                gettimeofday(&tv, NULL);
                localtime_r(&tv.tv_sec, &t);
                fprintf(my_session->fp,
                        "%02d:%02d:%02d.%-3d %d/%02d/%d, ",
                        t.tm_hour, t.tm_min, t.tm_sec, (int) (tv.tv_usec / 1000),
                        t.tm_mday, t.tm_mon + 1, 1900 + t.tm_year);
                fprintf(my_session->fp, "%s\n", ptr);

            }
            free(ptr);
        }
    }
    /* Pass the query downstream */
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
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    QLA_INSTANCE *my_instance = (QLA_INSTANCE *) instance;
    QLA_SESSION *my_session = (QLA_SESSION *) fsession;

    if (my_session)
    {
        dcb_printf(dcb, "\t\tLogging to file            %s.\n",
                   my_session->filename);
    }
    if (my_instance->source)
    {
        dcb_printf(dcb, "\t\tLimit logging to connections from  %s\n",
                   my_instance->source);
    }
    if (my_instance->userName)
    {
        dcb_printf(dcb, "\t\tLimit logging to user      %s\n",
                   my_instance->userName);
    }
    if (my_instance->match)
    {
        dcb_printf(dcb, "\t\tInclude queries that match     %s\n",
                   my_instance->match);
    }
    if (my_instance->nomatch)
    {
        dcb_printf(dcb, "\t\tExclude queries that match     %s\n",
                   my_instance->nomatch);
    }
}
