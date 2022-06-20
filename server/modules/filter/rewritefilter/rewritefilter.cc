/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXB_MODULE_NAME "rewritefilter"
#include "rewritefilter.hh"
#include <string>

using std::string;
namespace config = mxs::config;

namespace
{
namespace rewritefilter
{

config::Specification specification(MXB_MODULE_NAME, config::Specification::FILTER);
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
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "Rewrite filter.",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &mxs::FilterApi<RewriteFilter>::s_api,
        nullptr,    /* Process init. */
        nullptr,    /* Process finish. */
        nullptr,    /* Thread init. */
        nullptr,    /* Thread finish. */
        &rewritefilter::specification
    };

    return &info;
}

//
// RewriteFilter
//

RewriteFilter::RewriteFilter::Config::Config(const std::string& name)
    : config::Configuration(name, &rewritefilter::specification)
{
}

RewriteFilter::RewriteFilter(const std::string& name)
    : m_config(name)
{
}

// static
RewriteFilter* RewriteFilter::create(const char* zName)
{
    return new RewriteFilter(zName);
}


RewriteFilterSession* RewriteFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return RewriteFilterSession::create(pSession, pService, this);
}

// static
json_t* RewriteFilter::diagnostics() const
{
    return m_config.to_json();
}

uint64_t RewriteFilter::getCapabilities() const
{
    return m_config.capabilities;
}
