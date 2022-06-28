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
namespace cfg = mxs::config;

namespace
{
namespace rewritefilter
{

cfg::Specification specification(MXB_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamBool nocase(
    &specification, "nocase", "Matching is case insensitive", false, cfg::Param::AT_RUNTIME);
}
}

static uint64_t CAPABILITIES = RCAP_TYPE_STMT_INPUT;

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
        CAPABILITIES,
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

RewriteFilter::RewriteFilter::Config::Config(const std::string& name, RewriteFilter& filter)
    : cfg::Configuration(name, &rewritefilter::specification)
    , m_filter(filter)

{
    add_native(&Config::m_settings, &Settings::nocase, &rewritefilter::nocase);
}

bool RewriteFilter::Config::post_configure(const std::map<std::string,
                                                          maxscale::ConfigParameters>& nested_params)
{
    m_filter.set_settings(std::make_unique<Settings>(m_settings));

    return true;
}

RewriteFilter::RewriteFilter(const std::string& name)
    : m_config(name, *this)
{
}

void RewriteFilter::set_settings(std::unique_ptr<Settings> settings)
{
    std::lock_guard<std::mutex> guard(m_settings_mutex);
    m_sSettings = std::move(settings);
}

std::shared_ptr<Settings> RewriteFilter::get_settings() const
{
    std::lock_guard<std::mutex> guard(m_settings_mutex);
    return m_sSettings;
}

// static
RewriteFilter* RewriteFilter::create(const char* zName)
{
    return new RewriteFilter(zName);
}

RewriteFilterSession* RewriteFilter::newSession(MXS_SESSION* pSession, SERVICE* pService)
{
    return RewriteFilterSession::create(pSession, pService, get_settings());
}

// static
json_t* RewriteFilter::diagnostics() const
{
    return m_config.to_json();
}

uint64_t RewriteFilter::getCapabilities() const
{
    return CAPABILITIES;
}
