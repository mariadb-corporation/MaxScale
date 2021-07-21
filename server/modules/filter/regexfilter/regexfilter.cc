/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "regexfilter"

#include <maxscale/ccdefs.hh>
#include <string.h>
#include <stdio.h>
#include <maxbase/alloc.h>
#include <maxbase/atomic.h>
#include <maxscale/config.hh>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/pcre2.hh>

/**
 * @file regexfilter.c - a very simple regular expression rewrite filter.
 *
 * A simple regular expression query rewrite filter.
 * Two parameters should be defined in the filter configuration
 *      match=<regular expression>
 *      replace=<replacement text>
 * Two optional parameters
 *      source=<source address to limit filter>
 *      user=<username to limit filter>
 */

static MXS_FILTER*         createInstance(const char* name, mxs::ConfigParameters* params);
static void                destroyInstance(MXS_FILTER* instance);
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance,
                                      MXS_SESSION* session,
                                      SERVICE* service,
                                      mxs::Downstream* down,
                                      mxs::Upstream* up);
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session);
static void setDownstream(MXS_FILTER* instance,
                          MXS_FILTER_SESSION* fsession,
                          mxs::Downstream* downstream);
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, GWBUF* queue);
static int clientReply(MXS_FILTER* instance,
                       MXS_FILTER_SESSION* session,
                       GWBUF* reply,
                       const mxs::ReplyRoute& down,
                       const mxs::Reply& r);
static json_t*  diagnostics(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession);
static uint64_t getCapabilities(MXS_FILTER* instance);

static char* regex_replace(const char* sql,
                           pcre2_code* re,
                           pcre2_match_data* study,
                           const char* replace);

/**
 * Instance structure
 */
struct RegexInstance
{
    char*       source;         /*< Source address to restrict matches */
    char*       user;           /*< User name to restrict matches */
    char*       match;          /*< Regular expression to match */
    char*       replace;        /*< Replacement text */
    pcre2_code* re;             /*< Compiled regex text */
    FILE*       logfile;        /*< Log file */
    bool        log_trace;      /*< Whether messages should be printed to tracelog */
};

/**
 * The session structure for this regex filter
 */
struct RegexSession
{
    mxs::Downstream*  down; /* The downstream filter */
    mxs::Upstream*    up;   /* The upstream filter */
    pthread_mutex_t   lock;
    int               no_change;    /* No. of unchanged requests */
    int               replacements; /* No. of changed requests */
    pcre2_match_data* match_data;   /*< Matching data used by the compiled regex */
};

void log_match(RegexInstance* inst, char* re, char* old, char* newsql);
void log_nomatch(RegexInstance* inst, char* re, char* old);

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {NULL}
};

extern "C"
{

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
        routeQuery,
        clientReply,
        diagnostics,
        getCapabilities,
        destroyInstance,
    };

    static const char description[] = "A query rewrite filter that uses regular "
                                      "expressions to rewrite queries";
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &MyObject,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {
                "match",
                MXS_MODULE_PARAM_STRING,
                NULL,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                "replace",
                MXS_MODULE_PARAM_STRING,
                NULL,
                MXS_MODULE_OPT_REQUIRED
            },
            {
                "options",
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {
                "log_trace",
                MXS_MODULE_PARAM_BOOL,
                "false"
            },
            {"source",                  MXS_MODULE_PARAM_STRING },
            {"user",                    MXS_MODULE_PARAM_STRING },
            {"log_file",                MXS_MODULE_PARAM_STRING },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

/**
 * Free a regexfilter instance.
 * @param instance instance to free
 */
void free_instance(RegexInstance* instance)
{
    if (instance)
    {
        if (instance->re)
        {
            pcre2_code_free(instance->re);
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
static MXS_FILTER* createInstance(const char* name, mxs::ConfigParameters* params)
{
    RegexInstance* my_instance = static_cast<RegexInstance*>(MXS_CALLOC(1, sizeof(RegexInstance)));

    if (my_instance)
    {
        my_instance->match = params->get_c_str_copy("match");
        my_instance->replace = params->get_c_str_copy("replace");
        my_instance->source = params->get_c_str_copy("source");
        my_instance->user = params->get_c_str_copy("user");
        my_instance->log_trace = params->get_bool("log_trace");

        std::string logfile = params->get_string("log_file");

        if (!logfile.empty())
        {
            if ((my_instance->logfile = fopen(logfile.c_str(), "a")) == NULL)
            {
                MXS_ERROR("Failed to open file '%s'.", logfile.c_str());
                free_instance(my_instance);
                return NULL;
            }

            fprintf(my_instance->logfile, "\nOpened regex filter log\n");
            fflush(my_instance->logfile);
        }

        int cflags = params->get_enum("options", option_values);

        if (!(my_instance->re = params->get_compiled_regex("match", cflags, nullptr).release()))
        {
            free_instance(my_instance);
            return NULL;
        }
    }

    return (MXS_FILTER*) my_instance;
}

static void destroyInstance(MXS_FILTER* instance)
{
    RegexInstance* my_instance = reinterpret_cast<RegexInstance*>(instance);
    MXS_FREE(my_instance->match);
    MXS_FREE(my_instance->replace);
    MXS_FREE(my_instance->source);
    MXS_FREE(my_instance->user);
    pcre2_code_free(my_instance->re);

    if (my_instance->logfile)
    {
        fclose(my_instance->logfile);
    }

    MXS_FREE(my_instance);
}

bool matching_connection(RegexInstance* my_instance, MXS_SESSION* session)
{
    bool rval = true;

    if (my_instance->source && strcmp(session_get_remote(session), my_instance->source) != 0)
    {
        rval = false;
    }
    else if (my_instance->user && strcmp(session_get_user(session), my_instance->user) != 0)
    {
        rval = false;
    }

    return rval;
}

/**
 * Associate a new session with this instance of the filter.
 *
 * @param instance  The filter instance data
 * @param session   The session itself
 * @return Session specific data for this session
 */
static MXS_FILTER_SESSION* newSession(MXS_FILTER* instance,
                                      MXS_SESSION* session,
                                      SERVICE* service,
                                      mxs::Downstream* down,
                                      mxs::Upstream* up)
{
    RegexInstance* my_instance = (RegexInstance*) instance;
    RegexSession* my_session = static_cast<RegexSession*>(MXS_CALLOC(1, sizeof(RegexSession)));

    if (my_session)
    {
        my_session->no_change = 0;
        my_session->replacements = 0;
        my_session->match_data = nullptr;
        my_session->down = down;
        my_session->up = up;

        if (matching_connection(my_instance, session))
        {
            my_session->match_data = pcre2_match_data_create_from_pattern(my_instance->re, NULL);
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
static void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
}

/**
 * Free the memory associated with this filter session.
 *
 * @param instance  The filter instance data
 * @param session   The session being closed
 */
static void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    RegexSession* my_session = (RegexSession*)session;
    pcre2_match_data_free(my_session->match_data);
    MXS_FREE(my_session);
    return;
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
static int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    RegexInstance* my_instance = (RegexInstance*) instance;
    RegexSession* my_session = (RegexSession*) session;
    char* sql, * newsql;

    if (my_session->match_data && modutil_is_SQL(queue))
    {
        if ((sql = modutil_get_SQL(queue)) != NULL)
        {
            newsql = regex_replace(sql,
                                   my_instance->re,
                                   my_session->match_data,
                                   my_instance->replace);
            if (newsql)
            {
                queue = modutil_replace_SQL(queue, newsql);
                queue = gwbuf_make_contiguous(queue);
                pthread_mutex_lock(&my_session->lock);
                log_match(my_instance, my_instance->match, sql, newsql);
                pthread_mutex_unlock(&my_session->lock);
                MXS_FREE(newsql);
                my_session->replacements++;
            }
            else
            {
                pthread_mutex_lock(&my_session->lock);
                log_nomatch(my_instance, my_instance->match, sql);
                pthread_mutex_unlock(&my_session->lock);
                my_session->no_change++;
            }
            MXS_FREE(sql);
        }
    }
    return my_session->down->routeQuery(my_session->down->instance,
                                        my_session->down->session,
                                        queue);
}

static int clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* buffer,
                       const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    RegexSession* my_session = (RegexSession*) session;
    return my_session->up->clientReply(my_session->up->instance, my_session->up->session,
                                       buffer, down, reply);
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
 */
static json_t* diagnostics(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    RegexInstance* my_instance = (RegexInstance*)instance;
    RegexSession* my_session = (RegexSession*)fsession;

    json_t* rval = json_object();

    json_object_set_new(rval, "match", json_string(my_instance->match));
    json_object_set_new(rval, "replace", json_string(my_instance->replace));

    if (my_session)
    {
        json_object_set_new(rval, "altered", json_integer(my_session->no_change));
        json_object_set_new(rval, "unaltered", json_integer(my_session->replacements));
    }

    if (my_instance->source)
    {
        json_object_set_new(rval, "source", json_string(my_instance->source));
    }

    if (my_instance->user)
    {
        json_object_set_new(rval, "user", json_string(my_instance->user));
    }

    return rval;
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
static char* regex_replace(const char* sql, pcre2_code* re, pcre2_match_data* match_data, const char* replace)
{
    char* result = NULL;
    size_t result_size;

    /** This should never fail with rc == 0 because we used pcre2_match_data_create_from_pattern() */
    if (pcre2_match(re, (PCRE2_SPTR) sql, PCRE2_ZERO_TERMINATED, 0, 0, match_data, NULL) > 0)
    {
        result_size = strlen(sql) + strlen(replace);
        result = static_cast<char*>(MXS_MALLOC(result_size));

        size_t result_size_tmp = result_size;
        while (result
               && pcre2_substitute(re,
                                   (PCRE2_SPTR) sql,
                                   PCRE2_ZERO_TERMINATED,
                                   0,
                                   PCRE2_SUBSTITUTE_GLOBAL,
                                   match_data,
                                   NULL,
                                   (PCRE2_SPTR) replace,
                                   PCRE2_ZERO_TERMINATED,
                                   (PCRE2_UCHAR*) result,
                                   (PCRE2_SIZE*) &result_size_tmp) == PCRE2_ERROR_NOMEMORY)
        {
            result_size_tmp = 1.5 * result_size;
            char* tmp;
            if ((tmp = static_cast<char*>(MXS_REALLOC(result, result_size_tmp))) == NULL)
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
void log_match(RegexInstance* inst, char* re, char* old, char* newsql)
{
    if (inst->logfile)
    {
        fprintf(inst->logfile, "Matched %s: [%s] -> [%s]\n", re, old, newsql);
        fflush(inst->logfile);
    }
    if (inst->log_trace)
    {
        MXS_INFO("Match %s: [%s] -> [%s]", re, old, newsql);
    }
}

/**
 * Log a non-matching query to either MaxScale's trace log or a separate log file.
 * @param inst Regex filter instance
 * @param re Regular expression
 * @param old SQL statement
 */
void log_nomatch(RegexInstance* inst, char* re, char* old)
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
    return RCAP_TYPE_NONE;
}
