/*
 * Copyright (c) Niclas Antti
 *
 * This software is released under the MIT License.
 */

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/ccdefs.hh>
#include <maxscale/utils.h>
#include <maxscale/json_api.h>
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
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "Prevents high frequency querying from monopolizing the system",
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,
        &throttle::ThrottleFilter::s_object,
        NULL,                                                           /* Process init. */
        NULL,                                                           /* Process finish. */
        NULL,                                                           /* Thread init. */
        NULL,                                                           /* Thread finish. */
        {
            {MAX_QPS_CFG,                                               MXS_MODULE_PARAM_INT },
            {SAMPLING_DURATION_CFG,                                     MXS_MODULE_PARAM_INT, "250"},
            {THROTTLE_DURATION_CFG,                                     MXS_MODULE_PARAM_INT },
            {CONTINUOUS_DURATION_CFG,                                   MXS_MODULE_PARAM_INT, "2000"},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

namespace throttle
{

ThrottleFilter::ThrottleFilter(const ThrottleConfig& config) : m_config(config)
{
}

ThrottleFilter* ThrottleFilter::create(const char* zName, MXS_CONFIG_PARAMETER* pParams)
{
    int max_qps = pParams->get_integer(MAX_QPS_CFG);
    int sample_msecs = pParams->get_integer(SAMPLING_DURATION_CFG);
    int throttle_msecs = pParams->get_integer(THROTTLE_DURATION_CFG);
    int cont_msecs = pParams->get_integer(CONTINUOUS_DURATION_CFG);
    bool config_ok = true;

    if (max_qps < 2)
    {
        MXS_ERROR("Config value %s must be > 1", MAX_QPS_CFG);
        config_ok = false;
    }

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

ThrottleSession* ThrottleFilter::newSession(MXS_SESSION* mxsSession)
{
    return new ThrottleSession(mxsSession, *this);
}

void ThrottleFilter::diagnostics(DCB* pDcb)
{
}

json_t* ThrottleFilter::diagnostics_json() const
{
    return NULL;
}

uint64_t ThrottleFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}

const ThrottleConfig& ThrottleFilter::config() const
{
    return m_config;
}
}   // throttle
