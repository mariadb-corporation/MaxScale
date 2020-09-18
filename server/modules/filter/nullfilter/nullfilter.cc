/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "nullfilter"
#include "nullfilter.hh"
#include <string>
#include <maxscale/utils.h>

using std::string;
namespace config = mxs::config;

#define VERSION_STRING "V1.0.0"

namespace
{
namespace nullfilter
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamEnumMask<mxs_routing_capability_t> capabilities(
    &specification,
    "capabilities",
    "Combination of mxs_routing_capabilitiy_t values.",
    {
        { RCAP_TYPE_STMT_INPUT, "RCAP_TYPE_STMT_INPUT" },
        { RCAP_TYPE_CONTIGUOUS_INPUT, "RCAP_TYPE_CONTIGUOUS_INPUT" },
        { RCAP_TYPE_TRANSACTION_TRACKING, "RCAP_TYPE_TRANSACTION_TRACKING" },
        { RCAP_TYPE_STMT_OUTPUT, "RCAP_TYPE_STMT_OUTPUT" },
        { RCAP_TYPE_CONTIGUOUS_OUTPUT, "RCAP_TYPE_CONTIGUOUS_OUTPUT" },
        { RCAP_TYPE_RESULTSET_OUTPUT, "RCAP_TYPE_RESULTSET_OUTPUT" },
        { RCAP_TYPE_PACKET_OUTPUT, "RCAP_TYPE_PACKET_OUTPUT" },
        { RCAP_TYPE_SESSION_STATE_TRACKING, "RCAP_TYPE_SESSION_STATE_TRACKING" },
        { RCAP_TYPE_REQUEST_TRACKING, "RCAP_TYPE_REQUEST_TRACKING" }
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
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A null filter that does nothing.",
        VERSION_STRING,
        MXS_NO_MODULE_CAPABILITIES,
        &NullFilter::s_object,
        nullptr,   /* Process init. */
        nullptr,   /* Process finish. */
        nullptr,   /* Thread init. */
        nullptr,   /* Thread finish. */
        {{nullptr}},
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

NullFilter::NullFilter(Config&& config)
    : m_config(std::move(config))
{
    std::ostringstream os;

    os << "Null filter [" << config.name() << "] created, capabilities:";

    if (m_config.capabilities)
    {
        const auto& values = nullfilter::capabilities.values();

        for (const auto& value : values)
        {
            if ((m_config.capabilities & value.first) == value.first)
            {
                os << " " << value.second;
            }
        }
    }
    else
    {
        os << " (none)";
    }

    MXS_NOTICE("%s", os.str().c_str());
}

NullFilter::~NullFilter()
{
}

// static
NullFilter* NullFilter::create(const char* zName, mxs::ConfigParameters* pParams)
{
    NullFilter* pFilter = NULL;

    Config config(zName);

    if (config.configure(*pParams))
    {
        pFilter = new NullFilter(std::move(config));
    }

    return pFilter;
}


NullFilterSession* NullFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return NullFilterSession::create(pSession, pService, this);
}

// static
json_t* NullFilter::diagnostics() const
{
    return NULL;
}

uint64_t NullFilter::getCapabilities() const
{
    return m_config.capabilities;
}
