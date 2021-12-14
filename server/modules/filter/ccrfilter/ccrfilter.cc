/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "ccrfilter"

#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <string.h>
#include <maxbase/alloc.h>
#include <maxscale/config2.hh>
#include <maxscale/filter.hh>
#include <maxscale/hint.h>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/pcre2.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

using std::string;

namespace
{

const char PARAM_MATCH[] = "match";
const char PARAM_IGNORE[] = "ignore";
const char PARAM_GLOBAL[] = "global";

const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

namespace ccr
{

namespace config = mxs::config;

enum regex_options
{
    CCR_REGEX_CASE_INSENSITIVE = PCRE2_CASELESS,
    CCR_REGEX_CASE_SENSITIVE   = 0,
    CCR_REGEX_EXTENDED         = PCRE2_EXTENDED
};

class CCRSpecification : public config::Specification
{
public:
    using config::Specification::Specification;

protected:

    template<class Params>
    bool do_post_validate(Params params) const;

    bool post_validate(const mxs::ConfigParameters& params) const override final
    {
        return do_post_validate(params);
    }

    bool post_validate(json_t* json) const override final
    {
        return do_post_validate(json);
    }
};

CCRSpecification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamCount count(
    &specification,
    "count",
    "The number of SQL statements to route to master after detecting a data modifying SQL statement.",
    0, mxs::config::Param::AT_RUNTIME);

config::ParamSeconds time(
    &specification,
    "time",
    "The time window during which queries are routed to the master.",
    mxs::config::INTERPRET_AS_SECONDS,
    std::chrono::seconds {60}, mxs::config::Param::AT_RUNTIME);

config::ParamBool global(
    &specification,
    "global",
    "Specifies whether a write on one connection should have an impact on reads "
    "made on another connections. Note that 'global' and 'count' are mutually "
    "exclusive.",
    false, mxs::config::Param::AT_RUNTIME);

config::ParamRegex match(
    &specification,
    "match",
    "Regular expression used for matching statements.",
    "", mxs::config::Param::AT_RUNTIME);

config::ParamRegex ignore(
    &specification,
    "ignore",
    "Regular expression used for excluding statements.",
    "", mxs::config::Param::AT_RUNTIME);

config::ParamEnumMask<regex_options> options(
    &specification,
    "options",
    "Specificies additional options for the regular expressions; 'ignorecase' makes the "
    "matching case insensitive (on by default), 'case' makes the matching case sensitive "
    "and 'extended' causes whitespace to be ignored. They have been deprecated and you "
    "should instead use pattern settings in the regular expressions themselves.",
        {
            {CCR_REGEX_CASE_INSENSITIVE, "ignorecase"},
            {CCR_REGEX_CASE_SENSITIVE, "case"},
            {CCR_REGEX_EXTENDED, "extended"}
        },
    CCR_REGEX_CASE_INSENSITIVE, mxs::config::Param::AT_RUNTIME);

template<class Params>
bool CCRSpecification::do_post_validate(Params params) const
{
    bool rv = true;

    if (ccr::global.get(params) && ccr::count.get(params) != 0)
    {
        MXS_ERROR("'count' and 'global' cannot be used at the same time.");
        rv = false;
    }

    return rv;
}
}
}

class CCRConfig : public mxs::config::Configuration
{
public:
    CCRConfig(const CCRConfig&) = delete;
    CCRConfig& operator=(const CCRConfig&) = delete;

    CCRConfig(const std::string& name)
        : mxs::config::Configuration(name, &ccr::specification)
        , match(this, &ccr::match)
        , ignore(this, &ccr::ignore)
        , time(this, &ccr::time)
        , count(this, &ccr::count)
        , global(this, &ccr::global)
        , options(this, &ccr::options)
    {
    }

    CCRConfig(CCRConfig&& rhs) = default;

    mxs::config::Regex                        match;
    mxs::config::Regex                        ignore;
    mxs::config::Seconds                      time;
    mxs::config::Count                        count;
    mxs::config::Bool                         global;
    mxs::config::EnumMask<ccr::regex_options> options;
};

class CCRFilter;

class CCRSession : public mxs::FilterSession
{
public:
    CCRSession(const CCRSession&) = delete;
    CCRSession& operator=(const CCRSession&) = delete;

    static CCRSession* create(MXS_SESSION* session, SERVICE* service, CCRFilter* instance);
    bool               routeQuery(GWBUF* queue) override;

private:
    CCRFilter& m_instance;
    int        m_hints_left = 0;                    /* Number of hints left to add to queries */
    time_t     m_last_modification = 0;             /* Time of the last data modifying operation */

    mxs::config::RegexValue m_match;
    mxs::config::RegexValue m_ignore;
    std::chrono::seconds    m_time;
    uint64_t                m_count;
    bool                    m_global;

    enum CcrHintValue
    {
        CCR_HINT_NONE,
        CCR_HINT_MATCH,
        CCR_HINT_IGNORE
    };

    CCRSession(MXS_SESSION* session, SERVICE* service, CCRFilter* instance);

    static CcrHintValue search_ccr_hint(GWBUF* buffer);
};

class CCRFilter : public mxs::Filter
{
public:
    friend class CCRSession;    // Session needs to access & modify data in filter object

    static CCRFilter* create(const char* name)
    {
        return new CCRFilter(name);
    }

    ~CCRFilter()
    {
    }

    const CCRConfig& config() const
    {
        return m_config;
    }

    CCRSession* newSession(MXS_SESSION* session, SERVICE* service) override
    {
        return CCRSession::create(session, service, this);
    }

    json_t* diagnostics() const override
    {
        json_t* rval = json_object();
        m_config.fill(rval);
        json_object_set_new(rval, "data_modifications", json_integer(m_stats.n_modified));
        json_object_set_new(rval, "hints_added_count", json_integer(m_stats.n_add_count));
        json_object_set_new(rval, "hints_added_time", json_integer(m_stats.n_add_time));
        return rval;
    }

    uint64_t getCapabilities() const override
    {
        return RCAP_TYPE_STMT_INPUT;
    }

    mxs::config::Configuration& getConfiguration() override
    {
        return m_config;
    }

private:
    CCRFilter(const char* name)
        : m_config(name)
    {
    }

    struct LagStats
    {
        int n_add_count = 0;    /*< No. of statements diverted based on count */
        int n_add_time = 0;     /*< No. of statements diverted based on time */
        int n_modified = 0;     /*< No. of statements not diverted */
    };

    CCRConfig           m_config;
    std::atomic<time_t> m_last_modification {0};    /* Time of the last data modifying operation */
    LagStats            m_stats;
};

CCRSession::CCRSession(MXS_SESSION* session, SERVICE* service, CCRFilter* instance)
    : maxscale::FilterSession(session, service)
    , m_instance(*instance)
    , m_match(m_instance.config().match.get())
    , m_ignore(m_instance.config().ignore.get())
    , m_time(m_instance.config().time.get())
    , m_count(m_instance.config().count.get())
    , m_global(m_instance.config().global.get())
{
    if (int options = m_instance.config().options.get())
    {
        m_match = mxs::config::RegexValue(m_match.pattern(), options);
        m_ignore = mxs::config::RegexValue(m_ignore.pattern(), options);
    }
}

CCRSession* CCRSession::create(MXS_SESSION* session, SERVICE* service, CCRFilter* instance)
{
    return new CCRSession(session, service, instance);
}

bool CCRSession::routeQuery(GWBUF* queue)
{
    if (modutil_is_SQL(queue))
    {
        auto filter = &this->m_instance;
        time_t now = time(NULL);
        /* Not a simple SELECT statement, possibly modifies data. If we're processing a statement
         * with unknown query type, the safest thing to do is to treat it as a data modifying statement. */
        if (qc_query_is_type(qc_get_type_mask(queue), QUERY_TYPE_WRITE))
        {
            char* sql;
            int length;
            if (modutil_extract_SQL(queue, &sql, &length))
            {
                bool trigger_ccr = true;
                bool decided = false;   // Set by hints to take precedence.
                CcrHintValue ccr_hint_val = search_ccr_hint(queue);
                if (ccr_hint_val == CCR_HINT_IGNORE)
                {
                    trigger_ccr = false;
                    decided = true;
                }
                else if (ccr_hint_val == CCR_HINT_MATCH)
                {
                    decided = true;
                }
                if (!decided)
                {
                    trigger_ccr = (!m_match || m_match.match(sql, (size_t)length))
                        && (!m_ignore || !m_ignore.match(sql, (size_t)length));
                }
                if (trigger_ccr)
                {
                    if (m_count)
                    {
                        m_hints_left = m_count;
                        MXS_INFO("Write operation detected, next %ld queries routed to master",
                                 m_count);
                    }

                    if (m_time.count())
                    {
                        m_last_modification = now;
                        MXS_INFO("Write operation detected, queries routed to master for %ld seconds",
                                 m_time.count());

                        if (m_global)
                        {
                            filter->m_last_modification.store(now, std::memory_order_relaxed);
                        }
                    }

                    filter->m_stats.n_modified++;
                }
            }
        }
        else if (m_hints_left > 0)
        {
            queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
            m_hints_left--;
            filter->m_stats.n_add_count++;
            MXS_INFO("%d queries left", m_hints_left);
        }
        else if (m_time.count())
        {
            double dt = std::min(difftime(now, m_last_modification),
                                 difftime(now, filter->m_last_modification.load(std::memory_order_relaxed)));

            if (dt < m_time.count())
            {
                queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
                filter->m_stats.n_add_time++;
                MXS_INFO("%.0f seconds left", m_time.count() - dt);
            }
        }
    }

    return FilterSession::routeQuery(queue);
}

/**
 * Find the first CCR filter hint. The hint is removed from the buffer and the
 * contents returned.
 *
 * @param buffer Input buffer
 * @return The found ccr hint value
 */
CCRSession::CcrHintValue CCRSession::search_ccr_hint(GWBUF* buffer)
{
    const char CCR[] = "ccr";
    CcrHintValue rval = CCR_HINT_NONE;
    bool found_ccr = false;
    HINT** prev_ptr = &buffer->hint;
    HINT* hint = buffer->hint;

    while (hint && !found_ccr)
    {
        if (hint->type == HINT_PARAMETER && strcasecmp(static_cast<char*>(hint->data), CCR) == 0)
        {
            found_ccr = true;
            if (strcasecmp(static_cast<char*>(hint->value), "match") == 0)
            {
                rval = CCR_HINT_MATCH;
            }
            else if (strcasecmp(static_cast<char*>(hint->value), "ignore") == 0)
            {
                rval = CCR_HINT_IGNORE;
            }
            else
            {
                MXS_ERROR("Unknown value for hint parameter %s: '%s'.", CCR, (char*)hint->value);
            }
        }
        else
        {
            prev_ptr = &hint->next;
            hint = hint->next;
        }
    }
    // Remove the ccr-hint from the hint chain. Otherwise rwsplit will complain.
    if (found_ccr)
    {
        *prev_ptr = hint->next;
        hint_free(hint);
    }
    return rval;
}

// Global module object
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char DESCRIPTION[] = "A routing hint filter that sends queries to the master "
                                      "after data modification";
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        DESCRIPTION,
        "V1.1.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<CCRFilter>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        &ccr::specification
    };

    return &info;
}
