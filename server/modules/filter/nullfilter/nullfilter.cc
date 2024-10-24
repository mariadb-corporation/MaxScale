/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "nullfilter"
#include "nullfilter.hh"
#include <string>

using std::string;
namespace config = mxs::config;

#define VERSION_STRING "V1.0.0"

namespace
{
namespace nullfilter
{

config::Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);

config::ParamEnumMask<mxs_routing_capability_t> capabilities(
    &specification,
    "capabilities",
    "Combination of mxs_routing_capability_t values.",
        {
            {RCAP_TYPE_STMT_INPUT, "RCAP_TYPE_STMT_INPUT"},
            {RCAP_TYPE_TRANSACTION_TRACKING, "RCAP_TYPE_TRANSACTION_TRACKING"},
            {RCAP_TYPE_PACKET_OUTPUT, "RCAP_TYPE_PACKET_OUTPUT"},
            {RCAP_TYPE_REQUEST_TRACKING, "RCAP_TYPE_REQUEST_TRACKING"},
            {RCAP_TYPE_STMT_OUTPUT, "RCAP_TYPE_STMT_OUTPUT"},
            {RCAP_TYPE_RESULTSET_OUTPUT, "RCAP_TYPE_RESULTSET_OUTPUT"},
            {RCAP_TYPE_SESSION_STATE_TRACKING, "RCAP_TYPE_SESSION_STATE_TRACKING"},
            {RCAP_TYPE_QUERY_CLASSIFICATION, "RCAP_TYPE_QUERY_CLASSIFICATION"},
            {RCAP_TYPE_SESCMD_HISTORY, "RCAP_TYPE_SESCMD_HISTORY"},
            {RCAP_TYPE_MULTI_STMT_SQL, "RCAP_TYPE_MULTI_STMT_SQL"},
            {RCAP_TYPE_NO_THREAD_CHANGE, "RCAP_TYPE_NO_THREAD_CHANGE"}
        },
    0);
}
}

//
// Global symbols of the Module
//

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXB_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::GA,
        MXS_FILTER_VERSION,
        "A null filter that does nothing.",
        VERSION_STRING,
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::FilterApi<NullFilter>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        &nullfilter::specification
    };

    return &info;
}

//
// NullFilter
//

NullFilter::NullFilter::Config::Config(const std::string& name)
    : config::Configuration(name, &nullfilter::specification)
{
    add_native(&Config::capabilities, &nullfilter::capabilities);
}

NullFilter::NullFilter(const std::string& name)
    : m_config(name)
{
}

// static
std::unique_ptr<mxs::Filter> NullFilter::create(const char* zName)
{
    return std::make_unique<NullFilter>(zName);
}


std::shared_ptr<mxs::FilterSession> NullFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return std::make_shared<NullFilterSession>(pSession, pService, this);
}

// static
json_t* NullFilter::diagnostics() const
{
    return m_config.to_json();
}

uint64_t NullFilter::getCapabilities() const
{
    return m_config.capabilities;
}
