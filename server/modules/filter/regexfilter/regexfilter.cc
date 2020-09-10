/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
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

static char* regex_replace(const char* sql,
                           pcre2_code* re,
                           pcre2_match_data* study,
                           const char* replace);

struct RegexSession;

/**
 * Instance structure
 */
struct RegexInstance : public mxs::Filter<RegexInstance, RegexSession>
{
    RegexInstance(const char* name);
    ~RegexInstance();

    static RegexInstance* create(const char* name, mxs::ConfigParameters* params);
    mxs::FilterSession*   newSession(MXS_SESSION* session, SERVICE* service) override;

    json_t* diagnostics() const override
    {
        return nullptr;
    }

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_CONTIGUOUS_INPUT;
    }

    mxs::config::Configuration* getConfiguration() override
    {
        return nullptr;
    }

    bool matching_connection(MXS_SESSION* session);
    void log_match(char* old, char* newsql);
    void log_nomatch(char* old);

    char*       source;             /*< Source address to restrict matches */
    char*       user;               /*< User name to restrict matches */
    char*       match;              /*< Regular expression to match */
    char*       replace;            /*< Replacement text */
    pcre2_code* re;                 /*< Compiled regex text */
    FILE*       logfile = nullptr;  /*< Log file */
    bool        log_trace;          /*< Whether messages should be printed to tracelog */
};

/**
 * The session structure for this regex filter
 */
struct RegexSession : public mxs::FilterSession
{
    RegexSession(MXS_SESSION* session, SERVICE* service, RegexInstance* instance)
        : mxs::FilterSession(session, service)
        , m_instance(instance)
    {
        if (m_instance->matching_connection(session))
        {
            match_data = pcre2_match_data_create_from_pattern(m_instance->re, nullptr);
        }
    }

    ~RegexSession()
    {
        pcre2_match_data_free(match_data);
    }

    json_t* diagnostics() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "altered", json_integer(no_change));
        json_object_set_new(rval, "unaltered", json_integer(replacements));
        return rval;
    }

    int32_t clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
    {
        return mxs::FilterSession::clientReply(buffer, down, reply);
    }

    int32_t routeQuery(GWBUF* buffer);

    int               no_change = 0;            /* No. of unchanged requests */
    int               replacements = 0;         /* No. of changed requests */
    pcre2_match_data* match_data = nullptr;     /*< Matching data used by the compiled regex */

private:
    RegexInstance* m_instance;
};

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
        &RegexInstance::s_object,
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

// static
RegexInstance* RegexInstance::create(const char* name, mxs::ConfigParameters* params)
{
    RegexInstance* my_instance = new RegexInstance(name);

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
            delete my_instance;
            return nullptr;
        }

        fprintf(my_instance->logfile, "\nOpened regex filter log\n");
        fflush(my_instance->logfile);
    }

    int cflags = params->get_enum("options", option_values);

    if (!(my_instance->re = params->get_compiled_regex("match", cflags, nullptr).release()))
    {
        delete my_instance;
        return nullptr;
    }

    return my_instance;
}

RegexInstance::RegexInstance(const char* name)
{
}

RegexInstance::~RegexInstance()
{
    MXS_FREE(match);
    MXS_FREE(replace);
    MXS_FREE(source);
    MXS_FREE(user);
    pcre2_code_free(re);

    if (logfile)
    {
        fclose(logfile);
    }
}

mxs::FilterSession* RegexInstance::newSession(MXS_SESSION* session, SERVICE* service)
{
    return new RegexSession(session, service, this);
}

bool RegexInstance::matching_connection(MXS_SESSION* session)
{
    bool rval = true;

    if (source && strcmp(session_get_remote(session), source) != 0)
    {
        rval = false;
    }
    else if (user && strcmp(session_get_user(session), user) != 0)
    {
        rval = false;
    }

    return rval;
}

int32_t RegexSession::routeQuery(GWBUF* queue)
{
    char* sql;
    char* newsql;

    if (match_data && modutil_is_SQL(queue))
    {
        if ((sql = modutil_get_SQL(queue)) != NULL)
        {
            newsql = regex_replace(sql,
                                   m_instance->re,
                                   match_data,
                                   m_instance->replace);
            if (newsql)
            {
                queue = modutil_replace_SQL(queue, newsql);
                queue = gwbuf_make_contiguous(queue);
                m_instance->log_match(sql, newsql);
                MXS_FREE(newsql);
                replacements++;
            }
            else
            {
                m_instance->log_nomatch(sql);
                no_change++;
            }
            MXS_FREE(sql);
        }
    }

    return mxs::FilterSession::routeQuery(queue);
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

void RegexInstance::log_match(char* old, char* newsql)
{
    if (logfile)
    {
        fprintf(logfile, "Matched %s: [%s] -> [%s]\n", match, old, newsql);
        fflush(logfile);
    }
    if (log_trace)
    {
        MXS_INFO("Match %s: [%s] -> [%s]", match, old, newsql);
    }
}

void RegexInstance::log_nomatch(char* old)
{
    if (logfile)
    {
        fprintf(logfile, "No match %s: [%s]\n", match, old);
        fflush(logfile);
    }
    if (log_trace)
    {
        MXS_INFO("No match %s: [%s]", match, old);
    }
}
