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

#define MXS_MODULE_NAME "regexfilter"

#include <maxscale/ccdefs.hh>

#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>

namespace
{
namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamRegex s_match(&s_spec, "match", "PCRE2 pattern used for matching");
cfg::ParamString s_replace(&s_spec, "replace", "PCRE2 replacement text for the match pattern");
cfg::ParamString s_source(&s_spec, "source", "Only match queries done from this address", "");
cfg::ParamString s_user(&s_spec, "user", "Only match queries done by this user", "");
cfg::ParamString s_log_file(&s_spec, "log_file", "Log matching information to this file", "");

cfg::ParamBool s_log_trace(
    &s_spec, "log_trace", "Log matching information to the MaxScale log on the info level", false);

cfg::ParamEnum<uint32_t> s_options(&s_spec, "options", "Regular expression options",
    {
        {PCRE2_CASELESS, "ignorecase"},
        {0, "case"},
        {PCRE2_EXTENDED, "extended"},
    }, PCRE2_CASELESS);
}

class RegexSession;
class RegexInstance;

struct Config : mxs::config::Configuration
{
    Config(const char* name, RegexInstance* instance)
        : mxs::config::Configuration(name, &s_spec)
        , m_instance(instance)
    {
        add_native(&Config::match, &s_match);
        add_native(&Config::replace, &s_replace);
        add_native(&Config::log_trace, &s_log_trace);
        add_native(&Config::source, &s_source);
        add_native(&Config::user, &s_user);
        add_native(&Config::log_file, &s_log_file);
        add_native(&Config::options, &s_options);
    }

    mxs::config::RegexValue match;
    std::string             replace;
    uint32_t                options;
    bool                    log_trace;
    std::string             source;
    std::string             user;
    std::string             log_file;

protected:
    bool post_configure();

private:
    RegexInstance* m_instance;
};

class RegexInstance : public mxs::Filter<RegexInstance, RegexSession>
{
public:

    RegexInstance(const char* name);

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
        return &m_config;
    }

    const Config& config()const
    {
        return m_config;
    }

    bool open(const std::string& filename);
    bool matching_connection(MXS_SESSION* session);
    void log_match(const std::string& old, const std::string& newsql);
    void log_nomatch(const std::string& old);

private:
    Config m_config;
    FILE*  m_file = nullptr;    /*< Log file */
};

/**
 * The session structure for this regex filter
 */
class RegexSession : public mxs::FilterSession
{
public:
    RegexSession(MXS_SESSION* session, SERVICE* service, RegexInstance* instance)
        : mxs::FilterSession(session, service)
        , m_instance(instance)
        , m_active(m_instance->matching_connection(session))
    {
    }

    json_t* diagnostics() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "altered", json_integer(m_no_change));
        json_object_set_new(rval, "unaltered", json_integer(m_replacements));
        return rval;
    }

    int32_t clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
    {
        return mxs::FilterSession::clientReply(buffer, down, reply);
    }

    int32_t routeQuery(GWBUF* buffer);

private:
    RegexInstance* m_instance;
    int            m_no_change = 0;     /* No. of unchanged requests */
    int            m_replacements = 0;  /* No. of changed requests */
    bool           m_active;
};

bool Config::post_configure()
{
    if (!log_file.empty() && !m_instance->open(log_file))
    {
        MXS_ERROR("Failed to open file '%s'.", log_file.c_str());
        return false;
    }

    match.set_options(options);
    return true;
}

// static
RegexInstance* RegexInstance::create(const char* name, mxs::ConfigParameters* params)
{
    return new RegexInstance(name);
}

RegexInstance::RegexInstance(const char* name)
    : m_config(name, this)
{
}

mxs::FilterSession* RegexInstance::newSession(MXS_SESSION* session, SERVICE* service)
{
    return new RegexSession(session, service, this);
}

bool RegexInstance::matching_connection(MXS_SESSION* session)
{
    return (m_config.source.empty() || session->client_remote() == m_config.source)
           && (m_config.user.empty() || session->user() == m_config.user);
}

int32_t RegexSession::routeQuery(GWBUF* queue)
{
    if (m_active)
    {
        auto sql = mxs::extract_sql(queue);

        if (!sql.empty())
        {
            if (m_instance->config().match.match(sql))
            {
                auto newsql = m_instance->config().match.replace(sql, m_instance->config().replace.c_str());
                queue = modutil_replace_SQL(queue, newsql.c_str());
                queue = gwbuf_make_contiguous(queue);
                m_instance->log_match(sql, newsql);
                m_replacements++;
            }
            else
            {
                m_instance->log_nomatch(sql);
                m_no_change++;
            }
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

bool RegexInstance::open(const std::string& filename)
{
    if ((m_file = fopen(filename.c_str(), "a")))
    {
        fprintf(m_file, "\nOpened regex filter log\n");
        fflush(m_file);
    }

    return m_file;
}

void RegexInstance::log_match(const std::string& old, const std::string& newsql)
{
    if (m_file)
    {
        fprintf(m_file, "Matched %s: [%s] -> [%s]\n", m_config.match.pattern().c_str(),
                old.c_str(), newsql.c_str());
        fflush(m_file);
    }
    if (m_config.log_trace)
    {
        MXS_INFO("Match %s: [%s] -> [%s]", m_config.match.pattern().c_str(),
                 old.c_str(), newsql.c_str());
    }
}

void RegexInstance::log_nomatch(const std::string& old)
{
    if (m_file)
    {
        fprintf(m_file, "No match %s: [%s]\n", m_config.match.pattern().c_str(), old.c_str());
        fflush(m_file);
    }
    if (m_config.log_trace)
    {
        MXS_INFO("No match %s: [%s]", m_config.match.pattern().c_str(), old.c_str());
    }
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
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
        {{nullptr}},
        &s_spec
    };

    return &info;
}
