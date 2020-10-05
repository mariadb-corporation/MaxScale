/*
 * Copyright (c) Niclas Antti
 *
 * This software is released under the MIT License.
 */

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/ccdefs.hh>
#include <maxscale/config2.hh>
#include <maxscale/utils.h>
#include <maxscale/json_api.hh>
#include <maxscale/jansson.hh>

#include "throttlefilter.hh"

#include <string>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <unistd.h>

namespace
{
const char* const MAX_QPS_CFG = "max_qps";
const char* const SAMPLING_DURATION_CFG = "sampling_duration";
const char* const THROTTLE_DURATION_CFG = "throttling_duration";
const char* const CONTINUOUS_DURATION_CFG = "continuous_duration";
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
        {
            {MAX_QPS_CFG,                   MXS_MODULE_PARAM_INT,       nullptr, MXS_MODULE_OPT_REQUIRED},
            {SAMPLING_DURATION_CFG,         MXS_MODULE_PARAM_DURATION,  "250ms"},
            {THROTTLE_DURATION_CFG,         MXS_MODULE_PARAM_DURATION,  nullptr, MXS_MODULE_OPT_REQUIRED},
            {CONTINUOUS_DURATION_CFG,       MXS_MODULE_PARAM_DURATION,  "2000ms"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

namespace throttle
{

ThrottleFilter::ThrottleFilter(const ThrottleConfig& config)
    : m_config(config)
{
}

ThrottleFilter* ThrottleFilter::create(const char* zName, mxs::ConfigParameters* pParams)
{
    int max_qps = pParams->get_integer(MAX_QPS_CFG);
    int sample_msecs =
        pParams->get_duration<std::chrono::milliseconds>(SAMPLING_DURATION_CFG).count();
    int throttle_msecs =
        pParams->get_duration<std::chrono::milliseconds>(THROTTLE_DURATION_CFG).count();
    int cont_msecs =
        pParams->get_duration<std::chrono::milliseconds>(CONTINUOUS_DURATION_CFG).count();
    bool config_ok = true;

    if (max_qps < 2)
    {
        MXS_ERROR("Config value %s must be > 1", MAX_QPS_CFG);
        config_ok = false;
    }

    // TODO: These checks are unnecessary as a MXS_MODULE_PARAM_DURATION is required to be positive.
    if (sample_msecs < 0)
    {
        MXS_ERROR("Config value %s must be >= 0", SAMPLING_DURATION_CFG);
        config_ok = false;
    }

    if (throttle_msecs <= 0)
    {
        MXS_ERROR("Config value %s must be > 0", THROTTLE_DURATION_CFG);
        config_ok = false;
    }

    if (cont_msecs < 0)
    {
        MXS_ERROR("Config value %s must be >= 0", CONTINUOUS_DURATION_CFG);
        config_ok = false;
    }

    ThrottleFilter* filter {NULL};
    if (config_ok)
    {
        maxbase::Duration sampling_duration {std::chrono::milliseconds(sample_msecs)};
        maxbase::Duration throttling_duration {std::chrono::milliseconds(throttle_msecs)};
        maxbase::Duration continuous_duration {std::chrono::milliseconds(cont_msecs)};

        ThrottleConfig config = {max_qps,             sampling_duration,
                                 throttling_duration, continuous_duration};

        filter = new ThrottleFilter(config);
    }

    return filter;
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
    return RCAP_TYPE_NONE;
}

mxs::config::Configuration* ThrottleFilter::getConfiguration()
{
    return nullptr;
}

const ThrottleConfig& ThrottleFilter::config() const
{
    return m_config;
}
}   // throttle
