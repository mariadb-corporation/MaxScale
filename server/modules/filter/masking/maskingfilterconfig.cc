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

#define MXS_MODULE_NAME "masking"
#include "maskingfilterconfig.hh"

namespace
{

const char config_name_large_payload[]       = "large_payload";
const char config_name_rules[]               = "rules";
const char config_name_warn_type_mismatch[]  = "warn_type_mismatch";

const char config_value_abort[]  = "abort";
const char config_value_ignore[] = "ignore";
const char config_value_never[]  = "never";
const char config_value_always[] = "always";

const char config_name_prevent_function_usage[] = "prevent_function_usage";

const char config_value_true[] = "true";

}

/*
 * PARAM large_payload
 */

//static
const char* MaskingFilterConfig::large_payload_name = config_name_large_payload;

//static
const MXS_ENUM_VALUE MaskingFilterConfig::large_payload_values[] =
{
    { config_value_abort,  MaskingFilterConfig::LARGE_ABORT },
    { config_value_ignore, MaskingFilterConfig::LARGE_IGNORE },
    { NULL }
};

//static
const char* MaskingFilterConfig::large_payload_default = config_value_abort;

/*
 * PARAM rules
 */

//static
const char* MaskingFilterConfig::rules_name = config_name_rules;

/*
 * PARAM warn_type_mismatch
 */

//static
const char* MaskingFilterConfig::warn_type_mismatch_name = config_name_warn_type_mismatch;

//static
const MXS_ENUM_VALUE MaskingFilterConfig::warn_type_mismatch_values[] =
{
    { config_value_never,  MaskingFilterConfig::WARN_NEVER },
    { config_value_always, MaskingFilterConfig::WARN_ALWAYS },
    { NULL }
};

//static
const char* MaskingFilterConfig::warn_type_mismatch_default = config_value_never;

/*
 * PARAM prevent_function_usage
 */

//static
const char* MaskingFilterConfig::prevent_function_usage_name = config_name_prevent_function_usage;

//static
const char* MaskingFilterConfig::prevent_function_usage_default = config_value_true;

/*
 * MaskingFilterConfig
 */

//static
MaskingFilterConfig::large_payload_t
MaskingFilterConfig::get_large_payload(const MXS_CONFIG_PARAMETER* pParams)
{
    int value = config_get_enum(pParams, large_payload_name, large_payload_values);
    return static_cast<large_payload_t>(value);
}

//static
std::string MaskingFilterConfig::get_rules(const MXS_CONFIG_PARAMETER* pParams)
{
    return config_get_string(pParams, rules_name);
}

//static
MaskingFilterConfig::warn_type_mismatch_t
MaskingFilterConfig::get_warn_type_mismatch(const MXS_CONFIG_PARAMETER* pParams)
{
    int value = config_get_enum(pParams, warn_type_mismatch_name, warn_type_mismatch_values);
    return static_cast<warn_type_mismatch_t>(value);
}

//static
bool MaskingFilterConfig::get_prevent_function_usage(const MXS_CONFIG_PARAMETER* pParams)
{
    return config_get_bool(pParams, prevent_function_usage_name);
}

