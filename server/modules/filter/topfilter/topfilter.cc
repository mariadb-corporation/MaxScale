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
#include <fstream>
#include <iomanip>

#include <maxbase/regex.hh>
#include <maxbase/stopwatch.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>

class TopFilter;

class Query
{
public:
    Query(const mxb::Duration& d, const std::string& s)
        : m_d(d)
        , m_sql(s)
    {
    }

    double seconds() const
    {
        return mxb::to_secs(m_d);
    }

    const std::string& sql() const
    {
        return m_sql;
    }

    // Used for sorting queries that took longer before faster ones
    struct Sort
    {
        bool operator()(const Query& lhs, const Query& rhs)
        {
            return rhs.m_d < lhs.m_d;
        }
    };

private:
    mxb::Duration m_d;
    std::string   m_sql;
};

class TopSession : public mxs::FilterSession
{
public:
    TopSession(TopFilter* instance, MXS_SESSION* session, SERVICE* service);
    ~TopSession();
    int32_t routeQuery(GWBUF* buffer);
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);
    json_t* diagnostics() const;

private:
    TopFilter*           m_instance;
    bool                 m_active = true;
    std::string          m_filename;
    std::string          m_current;
    int                  m_n_statements = 0;
    wall_time::TimePoint m_connect;
    mxb::Duration        m_stmt_time;
    mxb::StopWatch       m_watch;
    std::vector<Query>   m_top;
};

namespace
{

namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamCount s_count(&s_spec, "count", "How many SQL statements to store", 10);
cfg::ParamString s_filebase(&s_spec, "filebase", "The basename of the output file created for each session");
cfg::ParamRegex s_match(&s_spec, "match", "Only include queries matching this pattern", "");
cfg::ParamRegex s_exclude(&s_spec, "exclude", "Exclude queries matching this pattern", "");
cfg::ParamString s_source(&s_spec, "source", "Only include queries done from this address", "");
cfg::ParamString s_user(&s_spec, "user", "Only include queries done by this user", "");
cfg::ParamEnum<uint32_t> s_options(&s_spec, "options", "Regular expression options",
    {
        {PCRE2_CASELESS, "ignorecase"},
        {0, "case"},
        {PCRE2_EXTENDED, "extended"},
    }, 0);
}

struct Config : public mxs::config::Configuration
{
    Config(const std::string& name)
        : mxs::config::Configuration(name, &s_spec)
    {
        add_native(&Config::count, &s_count);
        add_native(&Config::filebase, &s_filebase);
        add_native(&Config::source, &s_source);
        add_native(&Config::user, &s_user);
        add_native(&Config::options, &s_options);
        add_native(&Config::match, &s_match);
        add_native(&Config::exclude, &s_exclude);
    }

    int64_t                 count;      /* Number of queries to store */
    std::string             filebase;   /* Base of fielname to log into */
    std::string             source;     /* The source of the client connection */
    std::string             user;       /* A user name to filter on */
    uint32_t                options;    /* Regex options */
    mxs::config::RegexValue match;      /* Optional text to match against */
    mxs::config::RegexValue exclude;    /* Optional text to match against for exclusion */
};

class TopFilter : public mxs::Filter<TopFilter, TopSession>
{
public:
    static TopFilter* create(const std::string& name, mxs::ConfigParameters* params)
    {
        return new TopFilter(name);
    }

    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service)
    {
        return new TopSession(this, session, service);
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
        return &m_config;
    }

    const Config& config() const
    {
        return m_config;
    }

private:
    TopFilter(const std::string& name)
        : m_config(name)
    {
    }

    Config m_config;
};

TopSession::TopSession(TopFilter* instance, MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
    , m_instance(instance)
    , m_filename(m_instance->config().filebase + "." + std::to_string(session->id()))
    , m_connect(wall_time::Clock::now())
{
    const auto& config = m_instance->config();

    if ((!config.source.empty() && session->client_remote() != config.source)
        || (!config.user.empty() && session->user() != config.user))
    {
        m_active = false;
    }
}

TopSession::~TopSession()
{
    std::ofstream file(m_filename);

    if (file)
    {
        int statements = std::max(m_n_statements, 1);
        auto total = mxb::to_secs(m_watch.split());
        double stmt = mxb::to_secs(m_stmt_time);
        double avg = stmt / statements;

        file << std::fixed << std::setprecision(3);
        file << "Top " << m_instance->config().count << " longest running queries in session.\n"
             << "==========================================\n\n"
             << "Time (sec) | Query\n"
             << "-----------+-----------------------------------------------------------------\n";

        for (const auto& t : m_top)
        {
            if (!t.sql().empty())
            {
                file << std::setw(10) << t.seconds() << " |  " << t.sql() << "\n";
            }
        }

        file << "-----------+-----------------------------------------------------------------\n"
             << "\n\nSession started " << wall_time::to_string(m_connect, "%a %b %e %T %Y") << "\n"
             << "Connection from " << m_pSession->client_remote() << "\n"
             << "Username        " << m_pSession->user() << "\n"
             << "\nTotal of " << statements << " statements executed.\n"
             << "Total statement execution time   " << stmt << " seconds\n"
             << "Average statement execution time " << avg << " seconds\n"
             << "Total connection time            " << total << " seconds\n";
    }
}

int32_t TopSession::routeQuery(GWBUF* queue)
{
    if (m_active)
    {
        const auto& config = m_instance->config();
        auto sql = mxs::extract_sql(queue);

        if (!sql.empty()
            && (!config.match || config.match.match(sql))
            && (!config.exclude || !config.exclude.match(sql)))
        {
            m_n_statements++;
            m_watch.lap();
            m_current = sql;
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

int32_t TopSession::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    if (!m_current.empty())
    {
        auto lap = m_watch.lap();
        m_stmt_time += lap;
        m_top.emplace_back(lap, m_current);
        m_current.clear();

        std::sort(m_top.begin(), m_top.end(), Query::Sort {});

        if (m_top.size() > (size_t)m_instance->config().count)
        {
            m_top.pop_back();
        }
    }

    /* Pass the result upstream */
    return mxs::FilterSession::clientReply(buffer, down, reply);
}

json_t* TopSession::diagnostics() const
{
    json_t* rval = json_object();

    json_object_set_new(rval, "session_filename", json_string(m_filename.c_str()));

    json_t* arr = json_array();
    int i = 0;

    for (const auto& t : m_top)
    {
        if (!t.sql().empty())
        {
            json_t* obj = json_object();

            json_object_set_new(obj, "rank", json_integer(++i));
            json_object_set_new(obj, "time", json_real(t.seconds()));
            json_object_set_new(obj, "sql", json_string(t.sql().c_str()));

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
        &TopFilter::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {{nullptr}},
        &s_spec
    };

    return &info;
}
}
