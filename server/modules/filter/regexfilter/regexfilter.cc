/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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

#include <maxbase/format.hh>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/session.hh>
#include <maxscale/workerlocal.hh>

#include <fstream>

namespace
{
namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamRegex s_match(
    &s_spec, "match", "PCRE2 pattern used for matching",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_replace(
    &s_spec, "replace", "PCRE2 replacement text for the match pattern",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_source(
    &s_spec, "source", "Only match queries done from this address", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_user(
    &s_spec, "user", "Only match queries done by this user", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamString s_log_file(
    &s_spec, "log_file", "Log matching information to this file", "",
    cfg::Param::AT_RUNTIME);

cfg::ParamBool s_log_trace(
    &s_spec, "log_trace", "Log matching information to the MaxScale log on the info level", false,
    cfg::Param::AT_RUNTIME);

cfg::ParamEnum<uint32_t> s_options(&s_spec, "options", "Regular expression options",
    {
        {PCRE2_CASELESS, "ignorecase"},
        {0, "case"},
        {PCRE2_EXTENDED, "extended"},
    }, PCRE2_CASELESS, cfg::Param::AT_RUNTIME);
}

class RegexSession;
class RegexInstance;

struct Config : mxs::config::Configuration
{
    struct Values
    {
        mxs::config::RegexValue match;
        std::string             replace;
        uint32_t                options;
        bool                    log_trace;
        std::string             source;
        std::string             user;
        std::string             log_file;

        std::ofstream open_file() const
        {
            std::ofstream file;

            if (!log_file.empty())
            {
                // Disable buffering
                file.rdbuf()->pubsetbuf(nullptr, 0);

                file.open(log_file, std::ios_base::app | std::ios_base::ate);

                if (!file.good())
                {
                    MXS_ERROR("Failed to open log file '%s': %d, %s",
                              log_file.c_str(), errno, mxb_strerror(errno));
                }
            }

            return file;
        }
    };

    Config(const char* name, RegexInstance* instance)
        : mxs::config::Configuration(name, &s_spec)
        , m_instance(instance)
    {
        add_native(&Config::m_v, &Values::match, &s_match);
        add_native(&Config::m_v, &Values::replace, &s_replace);
        add_native(&Config::m_v, &Values::log_trace, &s_log_trace);
        add_native(&Config::m_v, &Values::source, &s_source);
        add_native(&Config::m_v, &Values::user, &s_user);
        add_native(&Config::m_v, &Values::log_file, &s_log_file);
        add_native(&Config::m_v, &Values::options, &s_options);
    }

    const Values& get_values() const
    {
        return *m_values;
    }

protected:
    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

private:
    RegexInstance*            m_instance;
    Values                    m_v;
    mxs::WorkerGlobal<Values> m_values;
};

class RegexInstance : public mxs::Filter
{
public:

    RegexInstance(const char* name);

    static RegexInstance* create(const char* name);
    mxs::FilterSession*   newSession(MXS_SESSION* session, SERVICE* service) override;

    json_t* diagnostics() const override
    {
        return nullptr;
    }

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_STMT_INPUT;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

    const Config::Values& config() const
    {
        return m_config.get_values();
    }

private:
    Config     m_config;
    FILE*      m_file = nullptr;
    std::mutex m_lock;          // Protects m_file
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
        , m_config(m_instance->config())
        , m_active(matching_connection(session))
        , m_file(m_config.open_file())
    {
    }

    json_t* diagnostics() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "altered", json_integer(m_no_change));
        json_object_set_new(rval, "unaltered", json_integer(m_replacements));
        return rval;
    }

    bool clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override
    {
        return mxs::FilterSession::clientReply(buffer, down, reply);
    }

    bool routeQuery(GWBUF* buffer) override;

private:
    bool matching_connection(MXS_SESSION* session);
    void log_match(const std::string& old, const std::string& newsql);
    void log_nomatch(const std::string& old);

    RegexInstance* m_instance;
    Config::Values m_config;
    int            m_no_change = 0;     /* No. of unchanged requests */
    int            m_replacements = 0;  /* No. of changed requests */
    bool           m_active;
    std::ofstream  m_file;
};

bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    if (!m_v.open_file())
    {
        return false;
    }

    m_v.match = mxs::config::RegexValue(m_v.match.pattern(), m_v.options);
    m_values.assign(m_v);
    return true;
}

// static
RegexInstance* RegexInstance::create(const char* name)
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

bool RegexSession::matching_connection(MXS_SESSION* session)
{
    return (m_config.source.empty() || session->client_remote() == m_config.source)
           && (m_config.user.empty() || session->user() == m_config.user);
}

bool RegexSession::routeQuery(GWBUF* queue)
{
    if (m_active)
    {
        const auto& sql = queue->get_sql();

        if (!sql.empty())
        {
            if (m_config.match.match(sql))
            {
                auto newsql = m_config.match.replace(sql, m_config.replace.c_str());
                gwbuf_free(queue);
                queue = modutil_create_query(newsql.c_str());
                log_match(sql, newsql);
                m_replacements++;
            }
            else
            {
                log_nomatch(sql);
                m_no_change++;
            }
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

void RegexSession::log_match(const std::string& old, const std::string& newsql)
{
    std::string msg = mxb::string_printf("Matched %s: [%s] -> [%s]\n", m_config.match.pattern().c_str(),
                                         old.c_str(), newsql.c_str());

    if (m_file.is_open() && m_file.good())
    {
        m_file.write(msg.c_str(), msg.size());
    }

    if (m_config.log_trace)
    {
        MXS_INFO("%s", msg.c_str());
    }
}

void RegexSession::log_nomatch(const std::string& old)
{
    std::string msg = mxb::string_printf("No match %s: [%s]\n", m_config.match.pattern().c_str(),
                                         old.c_str());

    if (m_file.is_open() && m_file.good())
    {
        m_file.write(msg.c_str(), msg.size());
    }

    if (m_config.log_trace)
    {
        MXS_INFO("%s", msg.c_str());
    }
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char description[] = "A query rewrite filter that uses regular "
                                      "expressions to rewrite queries";
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<RegexInstance>::s_api,
        NULL,
        NULL,
        NULL,
        NULL,
        &s_spec
    };

    return &info;
}
