/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
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
#include <maxscale/config2.hh>

namespace
{
namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamString s_an_example_parameter(
    &s_spec, "an_example_parameter", "An example string parameter",
    "a-default-value", cfg::Param::AT_STARTUP);

cfg::ParamBool s_global_counts(
    &s_spec, "global_counts", "Whether sessions increment the global counters",
    true, cfg::Param::AT_STARTUP);
}

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char DESC[] = "An example filter that counts the number of queries and replies "
                               "it has routed";
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        DESC,
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,                   // See getCapabilities() below
        &mxs::FilterApi<ExampleFilter>::s_api,  // Exposes the create-function
        NULL,                                   /* Process init. */
        NULL,                                   /* Process finish. */
        NULL,                                   /* Thread init. */
        NULL,                                   /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        },
        &s_spec
    };

    return &info;
}

ExampleFilter::ExampleFilter(const std::string& name)
    : m_config(name)
{
}

ExampleFilter::ExampleConfig::ExampleConfig(const std::string& name)
    : mxs::config::Configuration(name, &s_spec)
{
    add_native(&ExampleConfig::collect_global_counts, &s_global_counts);
}

ExampleFilter::~ExampleFilter()
{
}

// static
ExampleFilter* ExampleFilter::create(const char* zName)
{
    return new ExampleFilter(zName);
}

ExampleFilterSession* ExampleFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return ExampleFilterSession::create(pSession, pService, *this);
}

// static
json_t* ExampleFilter::diagnostics() const
{
    json_t* rval = json_object();
    int queries = m_total_queries.load(std::memory_order_relaxed);
    int replies = m_total_replies.load(std::memory_order_relaxed);
    json_object_set_new(rval, "total_queries", json_integer(queries));
    json_object_set_new(rval, "total_replies", json_integer(replies));
    return rval;
}

// static
uint64_t ExampleFilter::getCapabilities() const
{
    // Tells the protocol that the filter expects complete queries from client, that is, a query cannot be
    // sent in parts.
    return RCAP_TYPE_STMT_INPUT;

    // Try the following to also expect replies to be complete. This can cause problems if the server sends
    // a really big (e.g. 1 GB) resultset.
    // return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_RESULTSET_OUTPUT;
}

mxs::config::Configuration& ExampleFilter::getConfiguration()
{
    return m_config;
}

void ExampleFilter::query_seen()
{
    if (m_config.collect_global_counts)
    {
        m_total_queries.fetch_add(1, std::memory_order_relaxed);
    }
}

void ExampleFilter::reply_seen()
{
    if (m_config.collect_global_counts)
    {
        m_total_replies.fetch_add(1, std::memory_order_relaxed);
    }
}
