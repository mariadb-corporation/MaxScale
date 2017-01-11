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

    const FILTER_DEF* pFilterDef = pArgs->argv[1].value.filter;
    ss_dassert(pFilterDef);

    if (strcmp(pFilterDef->module, "masking") == 0)
    {
        MaskingFilter* pFilter = reinterpret_cast<MaskingFilter*>(pFilterDef->filter);

        MXS_EXCEPTION_GUARD(pFilter->reload(pDcb));
    }
    else
    {
        dcb_printf(pDcb, "Filter %s exists, but it is not a masking filter.", pFilterDef->name);
    }

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
        { MODULECMD_ARG_FILTER, "Masking name" }
    };

    modulecmd_register_command("masking", "reload", masking_command_reload,
                               MXS_ARRAY_NELEMS(reload_argv), reload_argv);

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
            {"rules_file", MXS_MODULE_PARAM_STRING, NULL, MXS_MODULE_OPT_REQUIRED},
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
    process_params(pzOptions, ppParams, config);
    auto_ptr<MaskingRules> sRules = MaskingRules::load(config.rules_file().c_str());

    if (sRules.get())
    {
        pFilter = new MaskingFilter(config, sRules);
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

void MaskingFilter::reload(DCB* pOut)
{
    auto_ptr<MaskingRules> sRules = MaskingRules::load(m_config.rules_file().c_str());

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

// static
void MaskingFilter::process_params(char **pzOptions, CONFIG_PARAMETER *pParams, Config& config)
{
    const char *value = config_get_string(pParams, "rules_file");
    string rules_file;

    if (*value != '/')
    {
        // A relative path is interpreted relative to the data directory.
        rules_file += get_datadir();
        rules_file += "/";
    }

    rules_file += value;
    config.set_rules_file(rules_file);
}
