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

#define PCRE2_CODE_UNIT_WIDTH 8
#include <stdio.h>
#include <filter.h>
#include <modinfo.h>
#include <modutil.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <string.h>
#include <pcre2.h>
#include <atomic.h>
#include "maxconfig.h"

/**
 * @file regexfilter.c - a very simple regular expression rewrite filter.
 * @verbatim
 *
 * A simple regular expression query rewrite filter.
 * Two parameters should be defined in the filter configuration
 *      match=<regular expression>
 *      replace=<replacement text>
 * Two optional parameters
 *      source=<source address to limit filter>
 *      user=<username to limit filter>
 *
 * Date         Who             Description
 * 19/06/2014   Mark Riddoch    Addition of source and user parameters
 * @endverbatim
 */

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_GA,
    FILTER_VERSION,
    "A query rewrite filter that uses regular expressions to rewite queries"
};

static char *version_str = "V1.1.0";

static FILTER *createInstance(char **options, FILTER_PARAMETER **params);
static void *newSession(FILTER *instance, SESSION *session);
static void closeSession(FILTER *instance, void *session);
static void freeSession(FILTER *instance, void *session);
static void setDownstream(FILTER *instance, void *fsession, DOWNSTREAM *downstream);
static int routeQuery(FILTER *instance, void *fsession, GWBUF *queue);
static void diagnostic(FILTER *instance, void *fsession, DCB *dcb);

static char *regex_replace(const char *sql, pcre2_code *re, pcre2_match_data *study,
                           const char *replace);

static FILTER_OBJECT MyObject =
{
    createInstance,
    newSession,
    closeSession,
    freeSession,
    setDownstream,
    NULL, // No Upstream requirement
    routeQuery,
    NULL,
    diagnostic,
};

/**
 * Instance structure
 */
typedef struct
{
    char *source; /*< Source address to restrict matches */
    char *user; /*< User name to restrict matches */
    char *match; /*< Regular expression to match */
    char *replace; /*< Replacement text */
    pcre2_code *re; /*< Compiled regex text */
    pcre2_match_data *match_data; /*< Matching data used by the compiled regex */
    FILE* logfile; /*< Log file */
    bool log_trace; /*< Whether messages should be printed to tracelog */
} REGEX_INSTANCE;

/**
 * The session structure for this regex filter
 */
typedef struct
{
    DOWNSTREAM down; /* The downstream filter */
    SPINLOCK lock;
    int no_change; /* No. of unchanged requests */
    int replacements; /* No. of changed requests */
    int active; /* Is filter active */
} REGEX_SESSION;

void log_match(REGEX_INSTANCE* inst, char* re, char* old, char* new);
void log_nomatch(REGEX_INSTANCE* inst, char* re, char* old);

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
 * Free a regexfilter instance.
 * @param instance instance to free
 */
void free_instance(REGEX_INSTANCE *instance)
{
    if (instance)
    {
        if (instance->re)
        {
            pcre2_code_free(instance->re);
        }

        if (instance->match_data)
        {
            pcre2_match_data_free(instance->match_data);
        }

        free(instance->match);
        free(instance->replace);
        free(instance->source);
        free(instance->user);
        free(instance);
    }
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
    REGEX_INSTANCE *my_instance;
    int i, errnumber, cflags = PCRE2_CASELESS;
    PCRE2_SIZE erroffset;
    char *logfile = NULL;
    const char *errmsg;

    if ((my_instance = calloc(1, sizeof(REGEX_INSTANCE))) != NULL)
    {
        my_instance->match = NULL;
        my_instance->replace = NULL;

        for (i = 0; params && params[i]; i++)
        {
            if (!strcmp(params[i]->name, "match"))
            {
                my_instance->match = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "replace"))
            {
                my_instance->replace = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "source"))
            {
                my_instance->source = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "user"))
            {
                my_instance->user = strdup(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "log_trace"))
            {
                my_instance->log_trace = config_truth_value(params[i]->value);
            }
            else if (!strcmp(params[i]->name, "log_file"))
            {
                if (logfile)
                {
                    free(logfile);
                }
                logfile = strdup(params[i]->value);
            }
            else if (!filter_standard_parameter(params[i]->name))
            {
                MXS_ERROR("regexfilter: Unexpected parameter '%s'.",
                          params[i]->name);
            }
        }

        if (options)
        {
            for (i = 0; options[i]; i++)
            {
                if (!strcasecmp(options[i], "ignorecase"))
                {
                    cflags |= PCRE2_CASELESS;
                }
                else if (!strcasecmp(options[i], "case"))
                {
                    cflags &= ~PCRE2_CASELESS;
                }
                else
                {
                    MXS_ERROR("regexfilter: unsupported option '%s'.",
                              options[i]);
                }
            }
        }

        if (logfile != NULL)
        {
            if ((my_instance->logfile = fopen(logfile, "a")) == NULL)
            {
                MXS_ERROR("regexfilter: Failed to open file '%s'.", logfile);
                free_instance(my_instance);
                free(logfile);
                return NULL;
            }

            fprintf(my_instance->logfile, "\nOpened regex filter log\n");
            fflush(my_instance->logfile);
        }
        free(logfile);

        if (my_instance->match == NULL || my_instance->replace == NULL)
        {
            free_instance(my_instance);
            return NULL;
        }

        if ((my_instance->re = pcre2_compile((PCRE2_SPTR) my_instance->match,
                                             PCRE2_ZERO_TERMINATED,
                                             cflags,
                                             &errnumber,
                                             &erroffset,
                                             NULL)) == NULL)
        {
            char errbuffer[1024];
            pcre2_get_error_message(errnumber, (PCRE2_UCHAR*) & errbuffer, sizeof(errbuffer));
            MXS_ERROR("regexfilter: Compiling regular expression '%s' failed at %lu: %s",
                      my_instance->match, erroffset, errbuffer);
            free_instance(my_instance);
            return NULL;
        }

        if ((my_instance->match_data =
             pcre2_match_data_create_from_pattern(my_instance->re, NULL)) == NULL)
        {
            MXS_ERROR("regexfilter: Failure to create PCRE2 matching data. "
                      "This is most likely caused by a lack of available memory.");
            free_instance(my_instance);
            return NULL;
        }
    }
    return(FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static void *
newSession(FILTER *instance, SESSION *session)
{
    REGEX_INSTANCE *my_instance = (REGEX_INSTANCE *) instance;
    REGEX_SESSION *my_session;
    char *remote, *user;

    if ((my_session = calloc(1, sizeof(REGEX_SESSION))) != NULL)
    {
        my_session->no_change = 0;
        my_session->replacements = 0;
        my_session->active = 1;
        if (my_instance->source
            && (remote = session_get_remote(session)) != NULL)
        {
            if (strcmp(remote, my_instance->source))
            {
                my_session->active = 0;
            }
        }

        if (my_instance->user && (user = session_getUser(session))
            && strcmp(user, my_instance->user))
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
static void
closeSession(FILTER *instance, void *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
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
 * @param instance  The filter instance data
 * @param session   The session being closed
 * @param downstream    The downstream filter or router
 */
static void
setDownstream(FILTER *instance, void *session, DOWNSTREAM *downstream)
{
    REGEX_SESSION *my_session = (REGEX_SESSION *) session;
    my_session->down = *downstream;
}

/**
 * The routeQuery entry point. This is passed the query buffer
 * to which the filter should be applied. Once applied the
 * query shoudl normally be passed to the downstream component
 * (filter or router) in the filter chain.
 *
 * @param instance  The filter instance data
 * @param session   The filter session
 * @param queue     The query data
 */
static int
routeQuery(FILTER *instance, void *session, GWBUF *queue)
{
    REGEX_INSTANCE *my_instance = (REGEX_INSTANCE *) instance;
    REGEX_SESSION *my_session = (REGEX_SESSION *) session;
    char *sql, *newsql;

    if (modutil_is_SQL(queue))
    {
        if (queue->next != NULL)
        {
            queue = gwbuf_make_contiguous(queue);
        }
        if ((sql = modutil_get_SQL(queue)) != NULL)
        {
            newsql = regex_replace(sql,
                                   my_instance->re,
                                   my_instance->match_data,
                                   my_instance->replace);
            if (newsql)
            {
                queue = modutil_replace_SQL(queue, newsql);
                queue = gwbuf_make_contiguous(queue);
                spinlock_acquire(&my_session->lock);
                log_match(my_instance, my_instance->match, sql, newsql);
                spinlock_release(&my_session->lock);
                free(newsql);
                my_session->replacements++;
            }
            else
            {
                spinlock_acquire(&my_session->lock);
                log_nomatch(my_instance, my_instance->match, sql);
                spinlock_release(&my_session->lock);
                my_session->no_change++;
            }
            free(sql);
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
 * @param   instance    The filter instance
 * @param   fsession    Filter session, may be NULL
 * @param   dcb     The DCB for diagnostic output
 */
static void
diagnostic(FILTER *instance, void *fsession, DCB *dcb)
{
    REGEX_INSTANCE *my_instance = (REGEX_INSTANCE *) instance;
    REGEX_SESSION *my_session = (REGEX_SESSION *) fsession;

    dcb_printf(dcb, "\t\tSearch and replace:            s/%s/%s/\n",
               my_instance->match, my_instance->replace);
    if (my_session)
    {
        dcb_printf(dcb, "\t\tNo. of queries unaltered by filter:    %d\n",
                   my_session->no_change);
        dcb_printf(dcb, "\t\tNo. of queries altered by filter:      %d\n",
                   my_session->replacements);
    }
    if (my_instance->source)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limited to connections from     %s\n",
                   my_instance->source);
    }
    if (my_instance->user)
    {
        dcb_printf(dcb,
                   "\t\tReplacement limit to user           %s\n",
                   my_instance->user);
    }
}

/**
 * Perform a regular expression match and substitution on the SQL
 *
 * @param   sql The original SQL text
 * @param   re  The compiled regular expression
 * @param   match_data The PCRE2 matching data buffer
 * @param   replace The replacement text
 * @return  The replaced text or NULL if no replacement was done.
 */
static char *
regex_replace(const char *sql, pcre2_code *re, pcre2_match_data *match_data, const char *replace)
{
    char *result = NULL;
    size_t result_size;

    /** This should never fail with rc == 0 because we used pcre2_match_data_create_from_pattern() */
    if (pcre2_match(re, (PCRE2_SPTR) sql, PCRE2_ZERO_TERMINATED, 0, 0, match_data, NULL))
    {
        result_size = strlen(sql) + strlen(replace);
        result = malloc(result_size);

        while (result &&
               pcre2_substitute(re, (PCRE2_SPTR) sql, PCRE2_ZERO_TERMINATED, 0,
                                PCRE2_SUBSTITUTE_GLOBAL, match_data, NULL,
                                (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                (PCRE2_UCHAR*) result, (PCRE2_SIZE*) & result_size) == PCRE2_ERROR_NOMEMORY)
        {
            char *tmp;
            if ((tmp = realloc(result, (result_size *= 1.5))) == NULL)
            {
                free(result);
                result = NULL;
            }
            result = tmp;
        }
    }
    return result;
}

/**
 * Log a matching query to either MaxScale's trace log or a separate log file.
 * The old SQL and the new SQL statements are printed in the log.
 * @param inst Regex filter instance
 * @param re Regular expression
 * @param old Old SQL statement
 * @param new New SQL statement
 */
void log_match(REGEX_INSTANCE* inst, char* re, char* old, char* new)
{
    if (inst->logfile)
    {
        fprintf(inst->logfile, "Matched %s: [%s] -> [%s]\n", re, old, new);
        fflush(inst->logfile);
    }
    if (inst->log_trace)
    {
        MXS_INFO("Match %s: [%s] -> [%s]", re, old, new);
    }
}

/**
 * Log a non-matching query to either MaxScale's trace log or a separate log file.
 * @param inst Regex filter instance
 * @param re Regular expression
 * @param old SQL statement
 */
void log_nomatch(REGEX_INSTANCE* inst, char* re, char* old)
{
    if (inst->logfile)
    {
        fprintf(inst->logfile, "No match %s: [%s]\n", re, old);
        fflush(inst->logfile);
    }
    if (inst->log_trace)
    {
        MXS_INFO("No match %s: [%s]", re, old);
    }
}
