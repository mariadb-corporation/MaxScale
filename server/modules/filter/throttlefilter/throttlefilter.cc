/*
 * Copyright (c) Niclas Antti
 *
 * This software is released under the MIT License.
 */

#define MXS_MODULE_NAME "throttlefilter"

#include <maxscale/cppdefs.hh>
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
const char* const TRIGGER_DURATION_CFG = "trigger_duration";
const char* const THROTTLE_DURATION_CFG = "throttle_duration";
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
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MAX_QPS_CFG,             MXS_MODULE_PARAM_INT},
            {TRIGGER_DURATION_CFG,    MXS_MODULE_PARAM_INT},
            {THROTTLE_DURATION_CFG,   MXS_MODULE_PARAM_INT},
            { MXS_END_MODULE_PARAMS }
        }
    };

    return &info;
}

namespace throttle
{

ThrottleFilter::ThrottleFilter(const ThrottleConfig &config) : m_config(config)
{
}

ThrottleFilter * ThrottleFilter::create(const char* zName, char * * pzOptions, MXS_CONFIG_PARAMETER * pParams)
{
    int max_qps         = config_get_integer(pParams, MAX_QPS_CFG);
    int trigger_secs    = config_get_integer(pParams, TRIGGER_DURATION_CFG);
    int throttle_secs   = config_get_integer(pParams, THROTTLE_DURATION_CFG);
    bool config_ok = true;

    if (max_qps < 2)
    {
        MXS_ERROR("Config value %s must be > 1", MAX_QPS_CFG);
        config_ok = false;
    }

    if (trigger_secs < 1)
    {
        MXS_ERROR("Config value %s must be > 0", TRIGGER_DURATION_CFG);
        config_ok = false;
    }

    if (throttle_secs < 0)
    {
        MXS_ERROR("Config value %s must be >= 0", THROTTLE_DURATION_CFG);
        config_ok = false;
    }

    Duration trigger_duration {std::chrono::seconds(trigger_secs)};
    Duration throttle_duration {std::chrono::seconds(throttle_secs)};

    ThrottleFilter* filter {NULL};
    if (config_ok)
    {
        ThrottleConfig config = {max_qps, trigger_duration, throttle_duration};

        std::ostringstream os1, os2;
        os1 << config.trigger_duration;
        os2 << config.throttle_duration;

        filter = new ThrottleFilter(config);
    }

    return filter;
}

ThrottleSession* ThrottleFilter::newSession(MXS_SESSION * mxsSession)
{
    return new ThrottleSession(mxsSession, *this);
}

void ThrottleFilter::diagnostics(DCB * pDcb)
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

const ThrottleConfig &ThrottleFilter::config() const
{
    return m_config;
}

} // throttle
