/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "regexfilter"

#include <maxscale/cdefs.h>
#include <string.h>
#include <stdio.h>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/config.h>
#include <maxscale/filter.h>
#include <maxscale/log_manager.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.h>
#include <maxscale/pcre2.h>

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

static MXS_FILTER *createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params);
static MXS_FILTER_SESSION *newSession(MXS_FILTER *instance, MXS_SESSION *session);
static void closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session);
static void setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, MXS_DOWNSTREAM *downstream);
static int routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, GWBUF *queue);
static void diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb);
static uint64_t getCapabilities(MXS_FILTER* instance);

static char *regex_replace(const char *sql, pcre2_code *re, pcre2_match_data *study,
                           const char *replace);

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
    MXS_DOWNSTREAM down; /* The downstream filter */
    SPINLOCK lock;
    int no_change; /* No. of unchanged requests */
    int replacements; /* No. of changed requests */
    int active; /* Is filter active */
} REGEX_SESSION;

void log_match(REGEX_INSTANCE* inst, char* re, char* old, char* new);
void log_nomatch(REGEX_INSTANCE* inst, char* re, char* old);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0},
    {NULL}
};

/**
 * The module entry point routine. It is this routine that
 * must populate the structure that is referred to as the
 * "module object", this is a structure with the set of
 * external entry points for this module.
 *
 * @return The module object
 */
MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        NULL, // No Upstream requirement
        routeQuery,
        NULL, // No clientReply
        diagnostic,
        getCapabilities,
        NULL, // No destroyInstance
    };

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A query rewrite filter that uses regular expressions to rewrite queries",
        "V1.1.0",
        &MyObject,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {"match", MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {"replace", MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
            {"source", MXS_MODULE_PARAM_STRING},
            {"user", MXS_MODULE_PARAM_STRING},
            {"log_trace", MXS_MODULE_PARAM_BOOL, "false"},
            {"log_file", MXS_MODULE_PARAM_STRING},
            {"options", MXS_MODULE_PARAM_ENUM, "ignorecase", MXS_MODULE_OPT_NONE, option_values},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
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

        MXS_FREE(instance->match);
        MXS_FREE(instance->replace);
        MXS_FREE(instance->source);
        MXS_FREE(instance->user);
        MXS_FREE(instance);
    }
}

/**
 * Create an instance of the filter for a particular service
 * within MaxScale.
 *
 * @param name      The name of the instance (as defined in the config file).
 * @param options   The options for this filter
 * @param params    The array of name/value pair parameters for the filter
 *
 * @return The instance data for this new instance
 */
static MXS_FILTER *
createInstance(const char *name, char **options, MXS_CONFIG_PARAMETER *params)
{
    REGEX_INSTANCE *my_instance = MXS_CALLOC(1, sizeof(REGEX_INSTANCE));

    if (my_instance)
    {
        my_instance->match = MXS_STRDUP_A(config_get_string(params, "match"));
        my_instance->replace = MXS_STRDUP_A(config_get_string(params, "replace"));
        my_instance->source = config_copy_string(params, "source");
        my_instance->user = config_copy_string(params, "user");
        my_instance->log_trace = config_get_bool(params, "log_trace");

        const char *logfile = config_get_string(params, "log_file");

        if (*logfile)
        {
            if ((my_instance->logfile = fopen(logfile, "a")) == NULL)
            {
                MXS_ERROR("Failed to open file '%s'.", logfile);
                free_instance(my_instance);
                return NULL;
            }

            fprintf(my_instance->logfile, "\nOpened regex filter log\n");
            fflush(my_instance->logfile);
        }

        int errnumber;
        PCRE2_SIZE erroffset;
        int cflags = config_get_enum(params, "options", option_values);

        if ((my_instance->re = pcre2_compile((PCRE2_SPTR) my_instance->match,
                                             PCRE2_ZERO_TERMINATED,
                                             cflags,
                                             &errnumber,
                                             &erroffset,
                                             NULL)) == NULL)
        {
            char errbuffer[1024];
            pcre2_get_error_message(errnumber, (PCRE2_UCHAR*) & errbuffer, sizeof(errbuffer));
            MXS_ERROR("Compiling regular expression '%s' failed at %lu: %s",
                      my_instance->match, erroffset, errbuffer);
            free_instance(my_instance);
            return NULL;
        }

        if ((my_instance->match_data =
                 pcre2_match_data_create_from_pattern(my_instance->re, NULL)) == NULL)
        {
            MXS_ERROR("Failure to create PCRE2 matching data. "
                      "This is most likely caused by a lack of available memory.");
            free_instance(my_instance);
            return NULL;
        }
    }

    return (MXS_FILTER *) my_instance;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION *
newSession(MXS_FILTER *instance, MXS_SESSION *session)
{
    REGEX_INSTANCE *my_instance = (REGEX_INSTANCE *) instance;
    REGEX_SESSION *my_session;
    const char *remote, *user;

    if ((my_session = MXS_CALLOC(1, sizeof(REGEX_SESSION))) != NULL)
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

        if (my_instance->user && (user = session_get_user(session))
            && strcmp(user, my_instance->user))
        {
            my_session->active = 0;
        }
    }

    return (MXS_FILTER_SESSION*)my_session;
}

/**
 * Close a session with the filter, this is the mechanism
 * by which a filter may cleanup data structure etc.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
closeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void
freeSession(MXS_FILTER *instance, MXS_FILTER_SESSION *session)
{
    MXS_FREE(session);
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
setDownstream(MXS_FILTER *instance, MXS_FILTER_SESSION *session, MXS_DOWNSTREAM *downstream)
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
routeQuery(MXS_FILTER *instance, MXS_FILTER_SESSION *session, GWBUF *queue)
{
    REGEX_INSTANCE *my_instance = (REGEX_INSTANCE *) instance;
    REGEX_SESSION *my_session = (REGEX_SESSION *) session;
    char *sql, *newsql;

    if (my_session->active && modutil_is_SQL(queue))
    {
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
                MXS_FREE(newsql);
                my_session->replacements++;
            }
            else
            {
                spinlock_acquire(&my_session->lock);
                log_nomatch(my_instance, my_instance->match, sql);
                spinlock_release(&my_session->lock);
                my_session->no_change++;
            }
            MXS_FREE(sql);
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
diagnostic(MXS_FILTER *instance, MXS_FILTER_SESSION *fsession, DCB *dcb)
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
    if (pcre2_match(re, (PCRE2_SPTR) sql, PCRE2_ZERO_TERMINATED, 0, 0, match_data, NULL) > 0)
    {
        result_size = strlen(sql) + strlen(replace);
        result = MXS_MALLOC(result_size);

        size_t result_size_tmp = result_size;
        while (result &&
               pcre2_substitute(re, (PCRE2_SPTR) sql, PCRE2_ZERO_TERMINATED, 0,
                                PCRE2_SUBSTITUTE_GLOBAL, match_data, NULL,
                                (PCRE2_SPTR) replace, PCRE2_ZERO_TERMINATED,
                                (PCRE2_UCHAR*) result, (PCRE2_SIZE*) & result_size_tmp) == PCRE2_ERROR_NOMEMORY)
        {
            result_size_tmp = 1.5 * result_size;
            char *tmp;
            if ((tmp = MXS_REALLOC(result, result_size_tmp)) == NULL)
            {
                MXS_FREE(result);
                result = NULL;
            }
            result = tmp;
            result_size = result_size_tmp;
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

/**
 * Capability routine.
 *
 * @return The capabilities of the filter.
 */
static uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}
