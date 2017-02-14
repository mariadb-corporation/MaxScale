/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
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

namespace
{

#define VERSION_STRING "V1.0.0"

const char CAPABILITIES_PARAM[] = "capabilities";

MXS_ENUM_VALUE capability_values[] =
{
    { "RCAP_TYPE_STMT_INPUT",           RCAP_TYPE_STMT_INPUT },
    { "RCAP_TYPE_CONTIGUOUS_INPUT",     RCAP_TYPE_CONTIGUOUS_INPUT },
    { "RCAP_TYPE_TRANSACTION_TRACKING", RCAP_TYPE_TRANSACTION_TRACKING },
    { "RCAP_TYPE_STMT_OUTPUT",          RCAP_TYPE_STMT_OUTPUT },
    { "RCAP_TYPE_CONTIGUOUS_OUTPUT",    RCAP_TYPE_CONTIGUOUS_OUTPUT },
    { "RCAP_TYPE_RESULTSET_OUTPUT",     RCAP_TYPE_RESULTSET_OUTPUT },
    { NULL, 0 }
};

struct unit_variables
{
    uint64_t capabilities;
    bool     capabilities_set;
} this_unit =
{
    0,
    false
};

}

//
// Global symbols of the Module
//

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Nullfilter module %s initialized.", VERSION_STRING);

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A null filter that does nothing.",
        VERSION_STRING,
        &NullFilter::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            { CAPABILITIES_PARAM, MXS_MODULE_PARAM_ENUM, NULL, MXS_MODULE_OPT_REQUIRED, capability_values },
            { MXS_END_MODULE_PARAMS }
        }
    };

    return &info;
}

//
// NullFilter
//

NullFilter::NullFilter(const char* zName)
{
    MXS_NOTICE("Null filter [%s] created.", zName);
}

NullFilter::~NullFilter()
{
}

// static
NullFilter* NullFilter::create(const char* zName, char**, MXS_CONFIG_PARAMETER* pParams)
{
    NullFilter* pFilter = NULL;

    uint64_t capabilities = config_get_enum(pParams, CAPABILITIES_PARAM, capability_values);

    if (this_unit.capabilities_set)
    {
        MXS_WARNING("The capabilities reported by NullFilter are currently global, "
                    "and not specific for a particular NullFilter instance.");
    }

    this_unit.capabilities = capabilities;
    this_unit.capabilities_set = true;

    return new NullFilter(zName);
}


NullFilterSession* NullFilter::newSession(MXS_SESSION* pSession)
{
    return NullFilterSession::create(pSession, this);
}

// static
void NullFilter::diagnostics(DCB* pDcb)
{
    dcb_printf(pDcb, "Hello, World!\n");
}

uint64_t NullFilter::getCapabilities()
{
    if (!this_unit.capabilities_set)
    {
        MXS_ERROR("getCapabilities() called before they have been set.");
    }

    return this_unit.capabilities;
}
