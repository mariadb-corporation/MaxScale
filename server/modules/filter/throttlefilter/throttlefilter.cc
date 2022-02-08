/*
 * Copyright (c) Niclas Antti
 *
 * This software is released under the MIT License.
 */

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>

#include "throttlefilter.hh"

#include <string>
#include <algorithm>
#include <unistd.h>

namespace
{
namespace cfg = mxs::config;
using std::chrono::milliseconds;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamInteger s_max_qps(
    &s_spec, "max_qps", "Maximum queries per second",
    1, std::numeric_limits<int64_t>::max(), cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_throttling_duration(
    &s_spec, "throttling_duration",
    "How long a session is allowed to be throttled before MaxScale disconnects the session",
    cfg::INTERPRET_AS_MILLISECONDS, cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_sampling_duration(
    &s_spec, "sampling_duration", "The window of time over which QPS is measured",
    cfg::INTERPRET_AS_MILLISECONDS, milliseconds(250), cfg::Param::AT_RUNTIME);

cfg::ParamMilliseconds s_continuous_duration(
    &s_spec, "continuous_duration", "Continuous throttling window",
    cfg::INTERPRET_AS_MILLISECONDS, milliseconds(2000), cfg::Param::AT_RUNTIME);
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    auto description = "Prevents high frequency querying from monopolizing the system";

    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        MXS_MODULE_NAME,
        mxs::ModuleType::FILTER,
        mxs::ModuleStatus::IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        description,
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::FilterApi<throttle::ThrottleFilter>::s_api,
        NULL,                               /* Process init. */
        NULL,                               /* Process finish. */
        NULL,                               /* Thread init. */
        NULL,                               /* Thread finish. */
        &s_spec
    };

    return &info;
}

namespace throttle
{

ThrottleConfig::ThrottleConfig(const char* name)
    : mxs::config::Configuration(name, &s_spec)
    , max_qps(this, &s_max_qps)
    , sampling_duration(this, &s_sampling_duration)
    , throttling_duration(this, &s_throttling_duration)
    , continuous_duration(this, &s_continuous_duration)
{
}

ThrottleFilter::ThrottleFilter(const char* name)
    : m_config(name)
{
}

ThrottleFilter* ThrottleFilter::create(const char* zName)
{
    return new ThrottleFilter(zName);
}

ThrottleSession* ThrottleFilter::newSession(MXS_SESSION* mxsSession, SERVICE* service)
{
    return new ThrottleSession(mxsSession, service, *this);
}

json_t* ThrottleFilter::diagnostics() const
{
    return NULL;
}

uint64_t ThrottleFilter::getCapabilities() const
{
    return RCAP_TYPE_STMT_INPUT;
}

mxs::config::Configuration& ThrottleFilter::getConfiguration()
{
    return m_config;
}

const ThrottleConfig& ThrottleFilter::config() const
{
    return m_config;
}
}   // throttle
