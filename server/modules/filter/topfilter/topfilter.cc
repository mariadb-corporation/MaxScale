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

#include <vector>
#include <sys/time.h>
#include <cstdio>

#include <maxbase/regex.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>

class TOPN_INSTANCE;

struct TOPNQ
{
    TOPNQ(const timeval& d, const std::string& s)
        : duration(d)
        , sql(s)
    {
    }

    timeval     duration;
    std::string sql;
};

// Used for sorting queries that took longer before faster ones
struct ReverseCompare
{
    bool operator()(const TOPNQ& lhs, const TOPNQ& rhs)
    {
        return lhs.duration.tv_sec == rhs.duration.tv_sec ?
               rhs.duration.tv_usec < lhs.duration.tv_usec :
               rhs.duration.tv_sec < lhs.duration.tv_sec;
    }
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
    TOPN_INSTANCE*     m_instance;
    bool               m_active = true;
    std::string        m_filename;
    std::string        m_current;
    int                m_n_statements = 0;
    timeval            m_start;
    timeval            m_total;
    timeval            m_connect;
    std::vector<TOPNQ> m_top;
};

static const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

struct Config
{
    Config(const mxs::ConfigParameters& params)
        : topN(params.get_integer("count"))
        , filebase(params.get_string("filebase"))
        , source(params.get_string("source"))
        , user(params.get_string("user"))
        , options(params.get_enum("options", option_values))
        , match(params.get_string("match"), options)
        , exclude(params.get_string("exclude"), options)
    {
    }

    int         sessions = 0;   /* Session count */
    int         topN;           /* Number of queries to store */
    std::string filebase;       /* Base of fielname to log into */
    std::string source;         /* The source of the client connection */
    std::string user;           /* A user name to filter on */
    int         options;        /* Regex options */
    mxb::Regex  match;          /* Optional text to match against */
    mxb::Regex  exclude;        /* Optional text to match against for exclusion */
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

    const Config& config() const
    {
        return m_config;
    }

private:
    TOPN_INSTANCE(const std::string& name, mxs::ConfigParameters* params)
        : m_config(*params)
    {
    }

    Config m_config;
};

TOPN_SESSION::TOPN_SESSION(TOPN_INSTANCE* instance, MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
    , m_instance(instance)
{
    const auto& config = m_instance->config();

    m_filename = config.filebase + "." + std::to_string(session->id());
    m_total.tv_sec = 0;
    m_total.tv_usec = 0;
    gettimeofday(&m_connect, NULL);

    if (!config.source.empty() && session->client_remote() != config.source)
    {
        m_active = false;
    }

    if (!config.user.empty() && session->user() != config.user)
    {
        m_active = false;
    }
}

TOPN_SESSION::~TOPN_SESSION()
{
    timeval diff;
    timeval disconnect;
    gettimeofday(&disconnect, NULL);
    timersub(&disconnect, &m_connect, &diff);

    if (FILE* fp = fopen(m_filename.c_str(), "w"))
    {
        int statements = std::max(m_n_statements, 1);

        fprintf(fp, "Top %d longest running queries in session.\n", m_instance->config().topN);
        fprintf(fp, "==========================================\n\n");
        fprintf(fp, "Time (sec) | Query\n");
        fprintf(fp, "-----------+-----------------------------------------------------------------\n");

        for (const auto& t : m_top)
        {
            if (!t.sql.empty())
            {
                fprintf(fp, "%10.3f |  %s\n",
                        (double) ((t.duration.tv_sec * 1000) + (t.duration.tv_usec / 1000)) / 1000,
                        t.sql.c_str());
            }
        }

        fprintf(fp, "-----------+-----------------------------------------------------------------\n");
        struct tm tm;
        localtime_r(&m_connect.tv_sec, &tm);
        char buffer[32];    // asctime_r documentation requires 26
        asctime_r(&tm, buffer);
        fprintf(fp, "\n\nSession started %s", buffer);

        fprintf(fp, "Connection from %s\n", m_pSession->client_remote().c_str());
        fprintf(fp, "Username        %s\n", m_pSession->user().c_str());
        fprintf(fp, "\nTotal of %d statements executed.\n", statements);
        fprintf(fp, "Total statement execution time   %5d.%d seconds\n",
                (int) m_total.tv_sec, (int) m_total.tv_usec / 1000);
        fprintf(fp, "Average statement execution time %9.3f seconds\n",
                (double) ((m_total.tv_sec * 1000) + (m_total.tv_usec / 1000)) / (1000 * statements));
        fprintf(fp, "Total connection time            %5d.%d seconds\n",
                (int) diff.tv_sec, (int) diff.tv_usec / 1000);
        fclose(fp);
    }
}

int32_t TOPN_SESSION::routeQuery(GWBUF* queue)
{
    if (m_active)
    {
        const auto& config = m_instance->config();
        auto sql = mxs::extract_sql(queue);

        if (!sql.empty())
        {
            if ((config.match.empty() || config.match.match(sql))
                && (config.exclude.empty() || !config.exclude.match(sql)))
            {
                m_n_statements++;
                gettimeofday(&m_start, NULL);
                m_current = sql;
            }
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

int32_t TOPN_SESSION::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    struct timeval tv, diff;

    if (!m_current.empty())
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &m_start, &diff);
        timeradd(&m_total, &diff, &m_total);
        m_top.emplace_back(diff, m_current);
        m_current.clear();

        std::sort(m_top.begin(), m_top.end(), ReverseCompare {});

        if (m_top.size() > (size_t)m_instance->config().topN)
        {
            m_top.pop_back();
        }
    }

    /* Pass the result upstream */
    return mxs::FilterSession::clientReply(buffer, down, reply);
}

json_t* TOPN_SESSION::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "session_filename", json_string(m_filename.c_str()));

    json_t* arr = json_array();
    int i = 0;

    for (const auto& t : m_top)
    {
        if (!t.sql.empty())
        {
            double exec_time = ((t.duration.tv_sec * 1000.0) + (t.duration.tv_usec / 1000.0)) / 1000.0;

            json_t* obj = json_object();

            json_object_set_new(obj, "rank", json_integer(++i));
            json_object_set_new(obj, "time", json_real(exec_time));
            json_object_set_new(obj, "sql", json_string(t.sql.c_str()));

            json_array_append_new(arr, obj);
        }
    }

    json_object_set_new(rval, "top_queries", arr);

    return rval;
}

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
            {"match",                 MXS_MODULE_PARAM_REGEX},
            {"exclude",               MXS_MODULE_PARAM_REGEX},
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
