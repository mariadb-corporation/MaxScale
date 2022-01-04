/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "examplecppfilter"

/*
 * To use the filter in a configuration, add the following section to the config file:
 * [ExampleFilter]
 * type=filter
 * module=examplecppfilter
 * global_counts=true
 *
 * Then add the filter to a service:
 * [Read-Write-Service]
 * .
 * .
 * filters=ExampleFilter
 */

#include "examplefilter.hh"

static const char CN_COUNT_GLOBALS[] = "global_counts";

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char DESC[] = "An example filter that counts the number of queries and replies "
                               "it has routed";
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        DESC,
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,       // See getCapabilities() below
        &ExampleFilter::s_object,   // This is defined in the MaxScale filter template
        NULL,                       /* Process init. */
        NULL,                       /* Process finish. */
        NULL,                       /* Thread init. */
        NULL,                       /* Thread finish. */
        {
            {"an_example_parameter",MXS_MODULE_PARAM_STRING,    "a-default-value"},
            {CN_COUNT_GLOBALS,    MXS_MODULE_PARAM_BOOL,      "true"           },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

ExampleFilter::ExampleFilter(const MXS_CONFIG_PARAMETER* pParams)
{
    m_collect_global_counts = pParams->get_bool(CN_COUNT_GLOBALS);
}

ExampleFilter::~ExampleFilter()
{
}

// static
ExampleFilter* ExampleFilter::create(const char* zName, MXS_CONFIG_PARAMETER* pParams)
{
    return new ExampleFilter(pParams);
}

ExampleFilterSession* ExampleFilter::newSession(MXS_SESSION* pSession)
{
    return ExampleFilterSession::create(pSession, *this);
}

// static
void ExampleFilter::diagnostics(DCB* pDcb) const
{
    int queries = m_total_queries.load(std::memory_order_relaxed);
    int replies = m_total_replies.load(std::memory_order_relaxed);
    dcb_printf(pDcb, "\t\tTotal queries            %i\n", queries);
    dcb_printf(pDcb, "\t\tTotal replies            %i\n", replies);
}

// static
json_t* ExampleFilter::diagnostics_json() const
{
    json_t* rval = json_object();
    int queries = m_total_queries.load(std::memory_order_relaxed);
    int replies = m_total_replies.load(std::memory_order_relaxed);
    json_object_set_new(rval, "total_queries", json_integer(queries));
    json_object_set_new(rval, "total_replies", json_integer(replies));
    return rval;
}

// static
uint64_t ExampleFilter::getCapabilities()
{
    // Tells the protocol that the filter expects complete queries from client, that is, a query cannot be
    // sent in parts.
    return RCAP_TYPE_STMT_INPUT;

    // Try the following to also expect replies to be complete. This can cause problems if the server sends
    // a really big (e.g. 1 GB) resultset.
    // return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT;
}

void ExampleFilter::query_seen()
{
    if (m_collect_global_counts)
    {
        m_total_queries.fetch_add(1, std::memory_order_relaxed);
    }
}

void ExampleFilter::reply_seen()
{
    if (m_collect_global_counts)
    {
        m_total_replies.fetch_add(1, std::memory_order_relaxed);
    }
}
