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

/**
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
 */

#define MXS_MODULE_NAME "topfilter"

#include <maxscale/ccdefs.hh>
#include <stdio.h>
#include <fcntl.h>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <regex.h>
#include <maxbase/atomic.h>
#include <maxbase/alloc.h>

class TOPN_INSTANCE;

struct TOPNQ
{
    struct timeval duration;
    char*          sql;
};

class TOPN_SESSION : public mxs::FilterSession
{
public:
    TOPN_SESSION(TOPN_INSTANCE* instance, MXS_SESSION* session, SERVICE* service);
    ~TOPN_SESSION();
    int32_t routeQuery(GWBUF* buffer);
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    json_t* diagnostics() const;

private:
    TOPN_INSTANCE* m_instance;
    int            active;
    char*          clientHost;
    char*          userName;
    char*          filename;
    int            fd;
    struct timeval start;
    char*          current;
    TOPNQ**        top;
    int            n_statements;
    struct timeval total;
    struct timeval connect;
    struct timeval disconnect;
};

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", REG_ICASE   },
    {"case",       0           },
    {"extended",   REG_EXTENDED},
    {NULL}
};

class TOPN_INSTANCE : public mxs::Filter<TOPN_INSTANCE, TOPN_SESSION>
{
public:
    static TOPN_INSTANCE* create(const std::string& name, mxs::ConfigParameters* params)
    {
        return new TOPN_INSTANCE(name, params);
    }

    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service)
    {
        return new TOPN_SESSION(this, session, service);
    }

    json_t* diagnostics() const
    {
        return nullptr;
    }

    uint64_t getCapabilities() const
    {
        return RCAP_TYPE_CONTIGUOUS_INPUT;
    }

    mxs::config::Configuration* getConfiguration()
    {
        return nullptr;
    }

    int     sessions;   /* Session count */
    int     topN;       /* Number of queries to store */
    char*   filebase;   /* Base of fielname to log into */
    char*   source;     /* The source of the client connection */
    char*   user;       /* A user name to filter on */
    char*   match;      /* Optional text to match against */
    regex_t re;         /* Compiled regex text */
    char*   exclude;    /* Optional text to match against for exclusion */
    regex_t exre;       /* Compiled regex nomatch text */

private:
    TOPN_INSTANCE(const std::string& name, mxs::ConfigParameters* params)
    {
        sessions = 0;
        topN = params->get_integer("count");
        match = params->get_c_str_copy("match");
        exclude = params->get_c_str_copy("exclude");
        source = params->get_c_str_copy("source");
        user = params->get_c_str_copy("user");
        filebase = params->get_c_str_copy("filebase");

        int cflags = params->get_enum("options", option_values);
        bool error = false;

        if (match && regcomp(&re, match, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s'"
                      " for the 'match' parameter.",
                      match);
            regfree(&re);
            MXS_FREE(match);
            match = NULL;
            error = true;
        }
        if (exclude
            && regcomp(&exre, exclude, cflags))
        {
            MXS_ERROR("Invalid regular expression '%s'"
                      " for the 'nomatch' parameter.\n",
                      exclude);
            regfree(&exre);
            MXS_FREE(exclude);
            exclude = NULL;
            error = true;
        }

        if (error)
        {
            if (exclude)
            {
                regfree(&exre);
                MXS_FREE(exclude);
            }
            if (match)
            {
                regfree(&re);
                MXS_FREE(match);
            }
            MXS_FREE(filebase);
            MXS_FREE(source);
            MXS_FREE(user);
        }
    }
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
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        "A top N query "
        "logging filter",
        "V1.0.1",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &TOPN_INSTANCE::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {
            {"count",                 MXS_MODULE_PARAM_COUNT,   "10"                   },
            {"filebase",              MXS_MODULE_PARAM_STRING,  NULL, MXS_MODULE_OPT_REQUIRED},
            {"match",                 MXS_MODULE_PARAM_STRING},
            {"exclude",               MXS_MODULE_PARAM_STRING},
            {"source",                MXS_MODULE_PARAM_STRING},
            {"user",                  MXS_MODULE_PARAM_STRING},
            {
                "options",
                MXS_MODULE_PARAM_ENUM,
                "ignorecase",
                MXS_MODULE_OPT_NONE,
                option_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
}

TOPN_SESSION::TOPN_SESSION(TOPN_INSTANCE* instance, MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
    , m_instance(instance)
{
    const char* remote;
    const char* user;

    filename = (char*) MXS_MALLOC(strlen(m_instance->filebase) + 20);
    sprintf(filename, "%s.%lu", m_instance->filebase, session->id());

    top = (TOPNQ**) MXS_CALLOC(m_instance->topN + 1, sizeof(TOPNQ*));
    MXS_ABORT_IF_NULL(top);
    for (int i = 0; i < m_instance->topN; i++)
    {
        top[i] = (TOPNQ*) MXS_CALLOC(1, sizeof(TOPNQ));
        MXS_ABORT_IF_NULL(top[i]);
        top[i]->sql = NULL;
    }
    n_statements = 0;
    total.tv_sec = 0;
    total.tv_usec = 0;
    current = NULL;

    if ((remote = session_get_remote(session)) != NULL)
    {
        clientHost = MXS_STRDUP_A(remote);
    }
    else
    {
        clientHost = NULL;
    }
    if ((user = session_get_user(session)) != NULL)
    {
        userName = MXS_STRDUP_A(user);
    }
    else
    {
        userName = NULL;
    }
    active = 1;
    if (m_instance->source && clientHost && strcmp(clientHost, m_instance->source))
    {
        active = 0;
    }
    if (m_instance->user && userName && strcmp(userName, m_instance->user))
    {
        active = 0;
    }

    sprintf(filename,
            "%s.%d",
            m_instance->filebase,
            m_instance->sessions);
    gettimeofday(&connect, NULL);
}

TOPN_SESSION::~TOPN_SESSION()
{
    struct timeval diff;
    int i;
    FILE* fp;
    int statements;

    gettimeofday(&disconnect, NULL);
    timersub((&disconnect), &(connect), &diff);
    if ((fp = fopen(filename, "w")) != NULL)
    {
        statements = n_statements != 0 ? n_statements : 1;

        fprintf(fp,
                "Top %d longest running queries in session.\n",
                m_instance->topN);
        fprintf(fp, "==========================================\n\n");
        fprintf(fp, "Time (sec) | Query\n");
        fprintf(fp, "-----------+-----------------------------------------------------------------\n");
        for (i = 0; i < m_instance->topN; i++)
        {
            if (top[i]->sql)
            {
                fprintf(fp,
                        "%10.3f |  %s\n",
                        (double) ((top[i]->duration.tv_sec * 1000)
                                  + (top[i]->duration.tv_usec / 1000)) / 1000,
                        top[i]->sql);
            }
        }
        fprintf(fp, "-----------+-----------------------------------------------------------------\n");
        struct tm tm;
        localtime_r(&connect.tv_sec, &tm);
        char buffer[32];    // asctime_r documentation requires 26
        asctime_r(&tm, buffer);
        fprintf(fp, "\n\nSession started %s", buffer);
        if (clientHost)
        {
            fprintf(fp,
                    "Connection from %s\n",
                    clientHost);
        }
        if (userName)
        {
            fprintf(fp,
                    "Username        %s\n",
                    userName);
        }
        fprintf(fp,
                "\nTotal of %d statements executed.\n",
                statements);
        fprintf(fp,
                "Total statement execution time   %5d.%d seconds\n",
                (int) total.tv_sec,
                (int) total.tv_usec / 1000);
        fprintf(fp,
                "Average statement execution time %9.3f seconds\n",
                (double) ((total.tv_sec * 1000)
                          + (total.tv_usec / 1000))
                / (1000 * statements));
        fprintf(fp,
                "Total connection time            %5d.%d seconds\n",
                (int) diff.tv_sec,
                (int) diff.tv_usec / 1000);
        fclose(fp);
    }

    MXS_FREE(current);

    for (int i = 0; i < m_instance->topN; i++)
    {
        MXS_FREE(top[i]->sql);
        MXS_FREE(top[i]);
    }

    MXS_FREE(top);
    MXS_FREE(clientHost);
    MXS_FREE(userName);
    MXS_FREE(filename);
}

int32_t TOPN_SESSION::routeQuery(GWBUF* queue)
{
    char* ptr;

    if (active)
    {
        if ((ptr = modutil_get_SQL(queue)) != NULL)
        {
            if ((m_instance->match == NULL || regexec(&m_instance->re, ptr, 0, NULL, 0) == 0)
                && (m_instance->exclude == NULL || regexec(&m_instance->exre, ptr, 0, NULL, 0) != 0))
            {
                n_statements++;
                if (current)
                {
                    MXS_FREE(current);
                }
                gettimeofday(&start, NULL);
                current = ptr;
            }
            else
            {
                MXS_FREE(ptr);
            }
        }
    }
    /* Pass the query downstream */
    return mxs::FilterSession::routeQuery(queue);
}

static int cmp_topn(const void* va, const void* vb)
{
    TOPNQ** a = (TOPNQ**) va;
    TOPNQ** b = (TOPNQ**) vb;

    if ((*b)->duration.tv_sec == (*a)->duration.tv_sec)
    {
        return (*b)->duration.tv_usec - (*a)->duration.tv_usec;
    }
    return (*b)->duration.tv_sec - (*a)->duration.tv_sec;
}

int32_t TOPN_SESSION::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    struct timeval tv, diff;
    int i, inserted;

    if (current)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &start, &diff);

        timeradd(&total, &diff, &total);

        inserted = 0;
        for (i = 0; i < m_instance->topN; i++)
        {
            if (top[i]->sql == NULL)
            {
                top[i]->sql = current;
                top[i]->duration = diff;
                inserted = 1;
                break;
            }
        }

        if (inserted == 0 && ((diff.tv_sec > top[m_instance->topN - 1]->duration.tv_sec)
                              || (diff.tv_sec == top[m_instance->topN - 1]->duration.tv_sec
                                  && diff.tv_usec > top[m_instance->topN - 1]->duration.tv_usec)))
        {
            MXS_FREE(top[m_instance->topN - 1]->sql);
            top[m_instance->topN - 1]->sql = current;
            top[m_instance->topN - 1]->duration = diff;
            inserted = 1;
        }

        if (inserted)
        {
            qsort(top, m_instance->topN, sizeof(TOPNQ*), cmp_topn);
        }
        else
        {
            MXS_FREE(current);
        }
        current = NULL;
    }

    /* Pass the result upstream */
    return mxs::FilterSession::clientReply(buffer, down, reply);
}

json_t* TOPN_SESSION::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "session_filename", json_string(filename));

    json_t* arr = json_array();

    for (int i = 0; i < m_instance->topN; i++)
    {
        if (top[i]->sql)
        {
            double exec_time = ((top[i]->duration.tv_sec * 1000.0)
                                + (top[i]->duration.tv_usec / 1000.0)) / 1000.0;

            json_t* obj = json_object();

            json_object_set_new(obj, "rank", json_integer(i + 1));
            json_object_set_new(obj, "time", json_real(exec_time));
            json_object_set_new(obj, "sql", json_string(top[i]->sql));

            json_array_append_new(arr, obj);
        }
    }

    json_object_set_new(rval, "top_queries", arr);

    return rval;
}
