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
#include <maxscale/gwdirs.h>
#include "maskingrules.hh"

using std::auto_ptr;
using std::string;

//
// Global symbols of the Module
//

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    MXS_NOTICE("Masking module %s initialized.", VERSION_STRING);

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        FILTER_VERSION,
        "A masking filter that is capable of masking/obfuscating returned column values.",
        "V1.0.0",
        &MaskingFilter::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}

//
// MaskingFilter
//

MaskingFilter::MaskingFilter(const Config& config, auto_ptr<MaskingRules> sRules)
    : m_config(config)
    , m_sRules(sRules)
{
    MXS_NOTICE("Masking filter [%s] created.", m_config.name().c_str());
}

MaskingFilter::~MaskingFilter()
{
}

// static
MaskingFilter* MaskingFilter::create(const char* zName, char** pzOptions, CONFIG_PARAMETER* ppParams)
{
    MaskingFilter* pFilter = NULL;

    MaskingFilter::Config config(zName);
    if (process_params(pzOptions, ppParams, config))
    {
        auto_ptr<MaskingRules> sRules = MaskingRules::load(config.rules_file().c_str());

        if (sRules.get())
        {
            pFilter = new MaskingFilter(config, sRules);
        }
    }

    return pFilter;
}


MaskingFilterSession* MaskingFilter::newSession(SESSION* pSession)
{
    return MaskingFilterSession::create(pSession, this);
}

// static
void MaskingFilter::diagnostics(DCB* pDcb)
{
    dcb_printf(pDcb, "Hello, World!\n");
}

// static
uint64_t MaskingFilter::getCapabilities()
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_STMT_OUTPUT;
}

std::tr1::shared_ptr<MaskingRules> MaskingFilter::rules() const
{
    return m_sRules;
}

// static
bool MaskingFilter::process_params(char **pzOptions, CONFIG_PARAMETER *ppParams, Config& config)
{
    bool error = false;

    for (int i = 0; ppParams[i]; ++i)
    {
        const FILTER_PARAMETER *pParam = ppParams[i];

        if (strcmp(pParam->name, "rules_file") == 0)
        {
            string rules_file;

            if (*pParam->value != '/')
            {
                // A relative path is interpreted relative to the data directory.
                rules_file += get_datadir();
                rules_file += "/";
            }

            rules_file += pParam->value;

            config.set_rules_file(rules_file);
        }
        else if (!filter_standard_parameter(pParam->name))
        {
            MXS_ERROR("Unknown configuration entry '%s'.", pParam->name);
            error = true;
        }
    }

    if (!error)
    {
        if (config.rules_file().empty())
        {
            MXS_ERROR("In order to use the masking filter, the location of the rules file "
                      "must be specified. Add a configuration entry 'rules_file=...' in "
                      "the section [%s], in the MariaDB MaxScale configuration file.",
                      config.name().c_str());
            error = true;
        }
    }

    return !error;
}
