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

#define MXS_MODULE_NAME "masking"
#include "maskingfilter.hh"

//
// Global symbols of the Module
//

MODULE_INFO info =
{
    MODULE_API_FILTER,
    MODULE_IN_DEVELOPMENT,
    FILTER_VERSION,
    "A masking filter that is capable of masking/obfuscating returned column values.",
    "V1.0.0"
};

extern "C" void ModuleInit()
{
    MXS_NOTICE("Initialized masking module.");
}

extern "C" FILTER_OBJECT *GetModuleObject()
{
    return &MaskingFilter::s_object;
}

//
// MaskingFilter
//

MaskingFilter::MaskingFilter()
{
}

MaskingFilter::~MaskingFilter()
{
}

// static
MaskingFilter* MaskingFilter::create(const char* zName, char** pzOptions, FILTER_PARAMETER** ppParams)
{
    MaskingFilter* pFilter = new MaskingFilter;

    if (!process_params(pzOptions, ppParams, pFilter->m_config))
    {
        delete pFilter;
        pFilter = NULL;
    }

    return pFilter;
}


MaskingFilterSession* MaskingFilter::newSession(SESSION* pSession)
{
    return MaskingFilterSession::create(pSession);
}

// static
void MaskingFilter::diagnostics(DCB* pDcb)
{
    dcb_printf(pDcb, "Hello, World!\n");
}

// static
uint64_t MaskingFilter::getCapabilities()
{
    return 0;
}

// static
bool MaskingFilter::process_params(char **pzOptions, FILTER_PARAMETER **ppParams, Config& config)
{
    return true;
}
