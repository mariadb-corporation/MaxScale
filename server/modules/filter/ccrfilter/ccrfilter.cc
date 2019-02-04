/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "ccrfilter"

#include <maxscale/ccdefs.hh>

#include <stdio.h>
#include <string.h>
#include <maxscale/alloc.h>
#include <maxscale/filter.hh>
#include <maxscale/hint.h>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/pcre2.h>
#include <maxscale/query_classifier.hh>

using std::string;

namespace
{

const char PARAM_MATCH[] = "match";
const char PARAM_IGNORE[] = "ignore";
const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

}

class CCRFilter;

class CCRSession : public mxs::FilterSession
{
public:
    CCRSession(const CCRSession&) = delete;
    CCRSession& operator=(const CCRSession&) = delete;

    ~CCRSession()
    {
        pcre2_match_data_free(m_md);
    }

    static CCRSession* create(MXS_SESSION* session, CCRFilter* instance);
    int                routeQuery(GWBUF* queue);

private:
    CCRFilter&        m_instance;
    int               m_hints_left = 0;             /* Number of hints left to add to queries */
    time_t            m_last_modification = 0;      /* Time of the last data modifying operation */
    pcre2_match_data* m_md = nullptr;                  /* PCRE2 match data */

    enum CcrHintValue
    {
        CCR_HINT_NONE,
        CCR_HINT_MATCH,
        CCR_HINT_IGNORE
    };

    CCRSession(MXS_SESSION* session, CCRFilter* instance)
        : maxscale::FilterSession(session)
        , m_instance(*instance)
    {
    }
    static CcrHintValue search_ccr_hint(GWBUF* buffer);
};

class CCRFilter : public mxs::Filter<CCRFilter, CCRSession>
{
public:
    friend class CCRSession;    // Session needs to access & modify data in filter object

    static CCRFilter* create(const char* name, MXS_CONFIG_PARAMETER* params)
    {
        CCRFilter* new_instance = new(std::nothrow) CCRFilter;
        if (new_instance)
        {
            new_instance->m_count = params->get_integer("count");
            new_instance->m_time = params->get_integer("time");
            new_instance->m_match = params->get_string(PARAM_MATCH);
            new_instance->m_nomatch = params->get_string(PARAM_IGNORE);

            int cflags = params->get_enum("options", option_values);
            const char* keys[] = {PARAM_MATCH, PARAM_IGNORE};
            pcre2_code** code_arr[] = {&new_instance->re, &new_instance->nore};
            if (!config_get_compiled_regexes(params, keys, sizeof(keys) / sizeof(char*),
                                             cflags, &new_instance->ovector_size,
                                             code_arr))
            {
                delete new_instance;
                new_instance = nullptr;
            }
        }
        return new_instance;
    }

    ~CCRFilter()
    {
        pcre2_code_free(re);
        pcre2_code_free(nore);
    }

    CCRSession* newSession(MXS_SESSION* session)
    {
        return CCRSession::create(session, this);
    }

    void diagnostics(DCB* dcb) const
    {
        dcb_printf(dcb, "Configuration:\n\tCount: %d\n", m_count);
        dcb_printf(dcb, "\tTime: %d seconds\n", m_time);

        if (!m_match.empty())
        {
            dcb_printf(dcb, "\tMatch regex: %s\n", m_match.c_str());
        }
        if (!m_nomatch.empty())
        {
            dcb_printf(dcb, "\tExclude regex: %s\n", m_nomatch.c_str());
        }

        dcb_printf(dcb, "\nStatistics:\n");
        dcb_printf(dcb, "\tNo. of data modifications: %d\n", m_stats.n_modified);
        dcb_printf(dcb, "\tNo. of hints added based on count: %d\n", m_stats.n_add_count);
        dcb_printf(dcb, "\tNo. of hints added based on time: %d\n", m_stats.n_add_time);
    }

    json_t* diagnostics_json() const
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "count", json_integer(m_count));
        json_object_set_new(rval, "time", json_integer(m_time));

        if (!m_match.empty())
        {
            json_object_set_new(rval, PARAM_MATCH, json_string(m_match.c_str()));
        }
        if (!m_nomatch.empty())
        {
            json_object_set_new(rval, "nomatch", json_string(m_nomatch.c_str()));
        }

        json_object_set_new(rval, "data_modifications", json_integer(m_stats.n_modified));
        json_object_set_new(rval, "hints_added_count", json_integer(m_stats.n_add_count));
        json_object_set_new(rval, "hints_added_time", json_integer(m_stats.n_add_time));
        return rval;
    }

    uint64_t getCapabilities()
    {
        return RCAP_TYPE_NONE;
    }

private:
    struct LagStats
    {
        int n_add_count = 0;    /*< No. of statements diverted based on count */
        int n_add_time = 0;     /*< No. of statements diverted based on time */
        int n_modified = 0;     /*< No. of statements not diverted */
    };

    string m_match;     /* Regular expression to match */
    string m_nomatch;   /* Regular expression to ignore */
    int    m_time = 0;  /* The number of seconds to wait before routing queries to slave servers after
                         * a data modification operation is done. */
    int m_count = 0;    /* Number of hints to add after each operation that modifies data. */

    LagStats    m_stats;
    pcre2_code* re = nullptr;          /* Compiled regex text of match */
    pcre2_code* nore = nullptr;        /* Compiled regex text of ignore */
    uint32_t    ovector_size = 0;   /* PCRE2 match data ovector size */
};

CCRSession* CCRSession::create(MXS_SESSION* session, CCRFilter* instance)
{
    CCRSession* new_session = new(std::nothrow) CCRSession(session, instance);
    if (new_session)
    {
        auto ovec_size = instance->ovector_size;
        if (ovec_size)
        {
            new_session->m_md = pcre2_match_data_create(ovec_size, NULL);
            if (!new_session->m_md)
            {
                delete new_session;
                new_session = nullptr;
            }
        }
    }
    return new_session;
}

int CCRSession::routeQuery(GWBUF* queue)
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
                    trigger_ccr = mxs_pcre2_check_match_exclude(filter->re, filter->nore, m_md,
                                                                sql, length, MXS_MODULE_NAME);
                }
                if (trigger_ccr)
                {
                    if (filter->m_count)
                    {
                        m_hints_left = filter->m_count;
                        MXS_INFO("Write operation detected, next %d queries routed to master",
                                 filter->m_count);
                    }

                    if (filter->m_time)
                    {
                        m_last_modification = now;
                        MXS_INFO("Write operation detected, queries routed to master for %d seconds",
                                 filter->m_time);
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
            MXS_INFO("%d queries left", filter->m_time);
        }
        else if (filter->m_time)
        {
            double dt = difftime(now, m_last_modification);

            if (dt < filter->m_time)
            {
                queue->hint = hint_create_route(queue->hint, HINT_ROUTE_TO_MASTER, NULL);
                filter->m_stats.n_add_time++;
                MXS_INFO("%.0f seconds left", dt);
            }
        }
    }

    return m_down.routeQuery(queue);
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
        NULL,                       /* Process init. */
        NULL,                       /* Process finish. */
        NULL,                       /* Thread init. */
        NULL,                       /* Thread finish. */
        {
            {"count",              MXS_MODULE_PARAM_COUNT,  "0"          },
            {"time",               MXS_MODULE_PARAM_COUNT,  "60"         },
            {PARAM_MATCH,          MXS_MODULE_PARAM_REGEX},
            {PARAM_IGNORE,         MXS_MODULE_PARAM_REGEX},
            {"options",            MXS_MODULE_PARAM_ENUM,   "ignorecase", MXS_MODULE_OPT_NONE, option_values},
            {MXS_END_MODULE_PARAMS}
        }
    };
    return &info;
}
