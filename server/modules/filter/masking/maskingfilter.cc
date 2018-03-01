/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingfilter.hh"
#include <maxscale/paths.h>
#include <maxscale/modulecmd.h>
#include "maskingrules.hh"

using std::auto_ptr;
using std::string;

namespace
{

char VERSION_STRING[] = "V1.0.0";

/**
 * Implement "call command masking reload ..."
 *
 * @param pArgs  The arguments of the command.
 *
 * @return True, if the command was handled.
 */
bool masking_command_reload(const MODULECMD_ARG* pArgs)
{
    ss_dassert(pArgs->argc == 2);
    ss_dassert(MODULECMD_GET_TYPE(&pArgs->argv[0].type) == MODULECMD_ARG_OUTPUT);
    ss_dassert(MODULECMD_GET_TYPE(&pArgs->argv[1].type) == MODULECMD_ARG_FILTER);

    DCB* pDcb = pArgs->argv[0].value.dcb;
    ss_dassert(pDcb);

    const MXS_FILTER_DEF* pFilterDef = pArgs->argv[1].value.filter;
    ss_dassert(pFilterDef);
    MaskingFilter* pFilter = reinterpret_cast<MaskingFilter*>(filter_def_get_instance(pFilterDef));

    MXS_EXCEPTION_GUARD(pFilter->reload(pDcb));

    return true;
}

}

//
// Global symbols of the Module
//

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static modulecmd_arg_type_t reload_argv[] =
    {
        { MODULECMD_ARG_OUTPUT, "The output dcb" },
        { MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN, "Masking name" }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "reload", masking_command_reload,
                               MXS_ARRAY_NELEMS(reload_argv), reload_argv);

    MXS_NOTICE("Masking module %s initialized.", VERSION_STRING);

    typedef MaskingFilter::Config Config;

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_IN_DEVELOPMENT,
        MXS_FILTER_VERSION,
        "A masking filter that is capable of masking/obfuscating returned column values.",
        "V1.0.0",
        &MaskingFilter::s_object,
        NULL, /* Process init. */
        NULL, /* Process finish. */
        NULL, /* Thread init. */
        NULL, /* Thread finish. */
        {
            { Config::rules_name, MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED },
            { Config::warn_type_mismatch_name,
              MXS_MODULE_PARAM_ENUM, Config::warn_type_mismatch_default,
              MXS_MODULE_OPT_NONE, Config::warn_type_mismatch_values },
            { Config::large_payload_name,
              MXS_MODULE_PARAM_ENUM, Config::large_payload_default,
              MXS_MODULE_OPT_NONE, Config::large_payload_values },
            { MXS_END_MODULE_PARAMS }
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
MaskingFilter* MaskingFilter::create(const char* zName, char** pzOptions, MXS_CONFIG_PARAMETER* pParams)
{
    MaskingFilter* pFilter = NULL;

    Config config(zName, pParams);

    auto_ptr<MaskingRules> sRules = MaskingRules::load(config.rules().c_str());

    if (sRules.get())
    {
        pFilter = new MaskingFilter(config, sRules);
    }

    return pFilter;
}


MaskingFilterSession* MaskingFilter::newSession(MXS_SESSION* pSession)
{
    return MaskingFilterSession::create(pSession, this);
}

// static
void MaskingFilter::diagnostics(DCB* pDcb)
{
}

// static
uint64_t MaskingFilter::getCapabilities()
{
    return RCAP_TYPE_STMT_INPUT | RCAP_TYPE_CONTIGUOUS_OUTPUT;
}

std::tr1::shared_ptr<MaskingRules> MaskingFilter::rules() const
{
    return m_sRules;
}

void MaskingFilter::reload(DCB* pOut)
{
    auto_ptr<MaskingRules> sRules = MaskingRules::load(m_config.rules().c_str());

    if (sRules.get())
    {
        m_sRules = sRules;

        dcb_printf(pOut, "Rules reloaded.\n");
    }
    else
    {
        dcb_printf(pOut, "Could not reload the rules. Check the log file for more "
                   "detailed information.\n");
    }
}
