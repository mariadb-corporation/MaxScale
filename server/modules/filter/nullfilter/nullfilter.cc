/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2022-01-01
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

const char* DEFAULT_RCAP_TYPE_NAME = "RCAP_TYPE_NONE";
const uint64_t DEFAULT_RCAP_TYPE_VALUE = 0;

const MXS_ENUM_VALUE capability_values[] =
{
    {DEFAULT_RCAP_TYPE_NAME,           DEFAULT_RCAP_TYPE_VALUE       },
    {"RCAP_TYPE_STMT_INPUT",           RCAP_TYPE_STMT_INPUT          },
    {"RCAP_TYPE_CONTIGUOUS_INPUT",     RCAP_TYPE_CONTIGUOUS_INPUT    },
    {"RCAP_TYPE_TRANSACTION_TRACKING", RCAP_TYPE_TRANSACTION_TRACKING},
    {"RCAP_TYPE_STMT_OUTPUT",          RCAP_TYPE_STMT_OUTPUT         },
    {"RCAP_TYPE_CONTIGUOUS_OUTPUT",    RCAP_TYPE_CONTIGUOUS_OUTPUT   },
    {"RCAP_TYPE_RESULTSET_OUTPUT",     RCAP_TYPE_RESULTSET_OUTPUT    },
    {NULL,                             0                             }
};

size_t RCAP_TYPE_NAME_MAXLEN = 30;      // strlen(RCAP_TYPE_TRANSACTION_TRACKING)
size_t RCAP_TYPE_COUNT = sizeof(capability_values) / sizeof(capability_values[0]);
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
        MXS_NO_MODULE_CAPABILITIES,
        &NullFilter::s_object,
        NULL,   /* Process init. */
        NULL,   /* Process finish. */
        NULL,   /* Thread init. */
        NULL,   /* Thread finish. */
        {
            {
                CAPABILITIES_PARAM,
                MXS_MODULE_PARAM_ENUM,
                DEFAULT_RCAP_TYPE_NAME,
                MXS_MODULE_OPT_NONE,
                capability_values
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

//
// NullFilter
//

NullFilter::NullFilter(const char* zName, uint64_t capabilities)
    : m_capabilities(capabilities)
{
    const char format[] = "Null filter [%s] created, capabilities: ";

    char message[sizeof(format) + strlen(zName) + (RCAP_TYPE_NAME_MAXLEN + 1) * RCAP_TYPE_COUNT + 1];

    sprintf(message, format, zName);

    if (m_capabilities)
    {
        const MXS_ENUM_VALUE* i = capability_values;
        const MXS_ENUM_VALUE* end = i + RCAP_TYPE_COUNT;

        while (i != end)
        {
            if (i->enum_value != 0)
            {
                if ((m_capabilities & i->enum_value) == i->enum_value)
                {
                    strcat(message, " ");
                    strcat(message, i->name);
                }
            }

            ++i;
        }
    }
    else
    {
        strcat(message, " (none)");
    }

    MXS_NOTICE("%s", message);
}

NullFilter::~NullFilter()
{
}

// static
NullFilter* NullFilter::create(const char* zName, MXS_CONFIG_PARAMETER* pParams)
{
    NullFilter* pFilter = NULL;

    uint64_t capabilities = pParams->get_enum(CAPABILITIES_PARAM, capability_values);

    return new NullFilter(zName, capabilities);
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

// static
json_t* NullFilter::diagnostics_json() const
{
    return NULL;
}

uint64_t NullFilter::getCapabilities()
{
    return m_capabilities;
}
