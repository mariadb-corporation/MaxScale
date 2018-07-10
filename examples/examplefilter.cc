/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// All log messages from this module are prefixed with this
#define MXS_MODULE_NAME "examplefilter"

#include "examplefilter.hh"

// This declares a module in MaxScale
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "An example filter that does nothing",
        "V1.0.0",
        RCAP_TYPE_NONE,
        &ExampleFilter::s_object, // This is defined in the MaxScale filter template
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            { "an_example_parameter", MXS_MODULE_PARAM_STRING, "a-default-value" },
            { MXS_END_MODULE_PARAMS }
        }
    };

    return &info;
}

ExampleFilter::ExampleFilter()
{
}

ExampleFilter::~ExampleFilter()
{
}

// static
ExampleFilter* ExampleFilter::create(const char* zName, MXS_CONFIG_PARAMETER* pParams)
{
    return new ExampleFilter();
}

ExampleFilterSession* ExampleFilter::newSession(MXS_SESSION* pSession)
{
    return ExampleFilterSession::create(pSession, this);
}

// static
void ExampleFilter::diagnostics(DCB* pDcb) const
{
}

// static
json_t* ExampleFilter::diagnostics_json() const
{
    return NULL;
}

// static
uint64_t ExampleFilter::getCapabilities()
{
    return RCAP_TYPE_NONE;
}
