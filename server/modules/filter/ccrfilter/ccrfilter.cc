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
#include <maxscale/query_classifier.hh>

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

config::Specification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamCount count(
    &specification,
    "count",
    "The number of SQL statements to route to master after detecting a data modifying SQL statement.",
    0);

config::ParamSeconds time(
    &specification,
    "time",
    "The time window during which queries are routed to the master.",
    mxs::config::INTERPRET_AS_SECONDS,
    std::chrono::seconds {60});

config::ParamBool global(
    &specification,
    "global",
    "Specifies whether a write on one connection should have an impact on reads "
    "made on another connections. Note that 'global' and 'count' are mutually "
    "exclusive.",
    false);

config::ParamRegex match(
    &specification,
    "match",
    "Regular expression used for matching statements.",
    "");

config::ParamRegex ignore(
    &specification,
    "ignore",
    "Regular expression used for excluding statements.",
    "");

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
    CCR_REGEX_CASE_INSENSITIVE);
}
}

class CCRConfig : public mxs::config::Configuration
{
public:
    CCRConfig(const CCRConfig&) = delete;
    CCRConfig& operator=(const CCRConfig&) = delete;

    CCRConfig(const std::string& name)
        : mxs::config::Configuration(name, &ccr::specification)
    {
        add_native(&CCRConfig::match, &ccr::match);
        add_native(&CCRConfig::ignore, &ccr::ignore);
        add_native(&CCRConfig::time, &ccr::time);
        add_native(&CCRConfig::count, &ccr::count);
        add_native(&CCRConfig::global, &ccr::global);
        add_native(&CCRConfig::options, &ccr::options);
    }

    CCRConfig(CCRConfig&& rhs) = default;

    bool post_configure()
    {
        bool rv = true;

        if (this->global && (this->count != 0))
        {
            MXS_ERROR("'count' and 'global' cannot be used at the same time.");
            rv = false;
        }

        if (rv)
        {
            this->ovector_size = std::max(this->match.ovec_size, this->ignore.ovec_size);

            if (this->options != 0)
            {
                this->match.set_options(options);
                this->ignore.set_options(options);
            }
        }

        return rv;
    }

    mxs::config::RegexValue match;
    mxs::config::RegexValue ignore;
    std::chrono::seconds    time;
    int64_t                 count;
    bool                    global;
    uint32_t                options;
    uint32_t                ovector_size {0};
};

class CCRFilter;

class CCRSession : public mxs::FilterSession
{
public:
    CCRSession(const CCRSession&) = delete;
    CCRSession& operator=(const CCRSession&) = delete;

    static CCRSession* create(MXS_SESSION* session, SERVICE* service, CCRFilter* instance);
    int                routeQuery(GWBUF* queue);

private:
    CCRFilter& m_instance;
    int        m_hints_left = 0;                    /* Number of hints left to add to queries */
    time_t     m_last_modification = 0;             /* Time of the last data modifying operation */

    enum CcrHintValue
    {
        CCR_HINT_NONE,
        CCR_HINT_MATCH,
        CCR_HINT_IGNORE
    };

    CCRSession(MXS_SESSION* session, SERVICE* service, CCRFilter* instance);

    static CcrHintValue search_ccr_hint(GWBUF* buffer);
};

class CCRFilter : public mxs::Filter<CCRFilter, CCRSession>
{
public:
    friend class CCRSession;    // Session needs to access & modify data in filter object

    static CCRFilter* create(const char* name, mxs::ConfigParameters* params)
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

    CCRSession* newSession(MXS_SESSION* session, SERVICE* service)
    {
        return CCRSession::create(session, service, this);
    }

    json_t* diagnostics() const
    {
        json_t* rval = json_object();
        m_config.fill(rval);
        json_object_set_new(rval, "data_modifications", json_integer(m_stats.n_modified));
        json_object_set_new(rval, "hints_added_count", json_integer(m_stats.n_add_count));
        json_object_set_new(rval, "hints_added_time", json_integer(m_stats.n_add_time));
        return rval;
    }

    uint64_t getCapabilities() const
    {
        return RCAP_TYPE_CONTIGUOUS_INPUT;
    }

    mxs::config::Configuration* getConfiguration()
    {
        return &m_config;
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
{
}

CCRSession* CCRSession::create(MXS_SESSION* session, SERVICE* service, CCRFilter* instance)
{
    return new CCRSession(session, service, instance);
}

int CCRSession::routeQuery(GWBUF* queue)
{
    if (modutil_is_SQL(queue))
    {
        auto filter = &this->m_instance;
        const CCRConfig& config = m_instance.config();
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
                    const auto& match = m_instance.config().match;
                    const auto& ignore = m_instance.config().ignore;

                    trigger_ccr = (!match || match.match(sql, (size_t)length))
                        && (!ignore || !ignore.match(sql, (size_t)length));
                }
                if (trigger_ccr)
                {
                    if (config.count)
                    {
                        m_hints_left = config.count;
                        MXS_INFO("Write operation detected, next %ld queries routed to master",
                                 config.count);
                    }

                    if (config.time.count())
                    {
                        m_last_modification = now;
                        MXS_INFO("Write operation detected, queries routed to master for %ld seconds",
                                 config.time.count());

                        if (config.global)
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
        else if (config.time.count())
        {
            double dt = std::min(difftime(now, m_last_modification),
                                 difftime(now, filter->m_last_modification.load(std::memory_order_relaxed)));

            if (dt < config.time.count())
            {
                queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
                filter->m_stats.n_add_time++;
                MXS_INFO("%.0f seconds left", dt);
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
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        DESCRIPTION,
        "V1.1.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &CCRFilter::s_object,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        &ccr::specification
    };

    return &info;
}
