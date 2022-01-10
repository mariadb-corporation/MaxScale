/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingfilterconfig.hh"

namespace
{

const char config_name_check_subqueries[] = "check_subqueries";
const char config_name_check_unions[] = "check_unions";
const char config_name_check_user_variables[] = "check_user_variables";
const char config_name_large_payload[] = "large_payload";
const char config_name_prevent_function_usage[] = "prevent_function_usage";
const char config_name_require_fully_parsed[] = "require_fully_parsed";
const char config_name_rules[] = "rules";
const char config_name_warn_type_mismatch[] = "warn_type_mismatch";
const char config_name_treat_string_arg_as_field[] = "treat_string_arg_as_field";


const char config_value_abort[] = "abort";
const char config_value_always[] = "always";
const char config_value_ignore[] = "ignore";
const char config_value_never[] = "never";

const char config_value_true[] = "true";
}

/*
 * PARAM large_payload
 */

// static
const char* MaskingFilterConfig::large_payload_name = config_name_large_payload;

// static
const MXS_ENUM_VALUE MaskingFilterConfig::large_payload_values[] =
{
    {config_value_abort,  MaskingFilterConfig::LARGE_ABORT   },
    {config_value_ignore, MaskingFilterConfig::LARGE_IGNORE  },
    {NULL}
};

// static
const char* MaskingFilterConfig::large_payload_default = config_value_abort;

/*
 * PARAM rules
 */

// static
const char* MaskingFilterConfig::rules_name = config_name_rules;

/*
 * PARAM warn_type_mismatch
 */

const char* MaskingFilterConfig::warn_type_mismatch_name = config_name_warn_type_mismatch;
const char* MaskingFilterConfig::warn_type_mismatch_default = config_value_never;

const MXS_ENUM_VALUE MaskingFilterConfig::warn_type_mismatch_values[] =
{
    {config_value_never,  MaskingFilterConfig::WARN_NEVER   },
    {config_value_always, MaskingFilterConfig::WARN_ALWAYS  },
    {NULL}
};

/*
 * PARAM prevent_function_usage
 */
const char* MaskingFilterConfig::prevent_function_usage_name = config_name_prevent_function_usage;
const char* MaskingFilterConfig::prevent_function_usage_default = config_value_true;

/*
 * PARAM check_user_variables
 */
const char* MaskingFilterConfig::check_user_variables_name = config_name_check_user_variables;
const char* MaskingFilterConfig::check_user_variables_default = config_value_true;

/*
 * PARAM check_unions
 */
const char* MaskingFilterConfig::check_unions_name = config_name_check_unions;
const char* MaskingFilterConfig::check_unions_default = config_value_true;

/*
 * PARAM check_subqueries
 */
const char* MaskingFilterConfig::check_subqueries_name = config_name_check_subqueries;
const char* MaskingFilterConfig::check_subqueries_default = config_value_true;

/*
 * PARAM require_fully_parsed
 */
const char* MaskingFilterConfig::require_fully_parsed_name = config_name_require_fully_parsed;
const char* MaskingFilterConfig::require_fully_parsed_default = config_name_require_fully_parsed;

/*
 * PARAM treat_string_arg_as_field
 */
const char* MaskingFilterConfig::treat_string_arg_as_field_name = config_name_treat_string_arg_as_field;
const char* MaskingFilterConfig::treat_string_arg_as_field_default = config_value_true;
/*
 * MaskingFilterConfig
 */

// static
MaskingFilterConfig::large_payload_t MaskingFilterConfig::get_large_payload(
    const MXS_CONFIG_PARAMETER* pParams)
{
    int value = pParams->get_enum(large_payload_name, large_payload_values);
    return static_cast<large_payload_t>(value);
}

// static
std::string MaskingFilterConfig::get_rules(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_string(rules_name);
}

// static
MaskingFilterConfig::warn_type_mismatch_t MaskingFilterConfig::get_warn_type_mismatch(
    const MXS_CONFIG_PARAMETER* pParams)
{
    int value = pParams->get_enum(warn_type_mismatch_name, warn_type_mismatch_values);
    return static_cast<warn_type_mismatch_t>(value);
}

// static
bool MaskingFilterConfig::get_prevent_function_usage(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(prevent_function_usage_name);
}

// static
bool MaskingFilterConfig::get_check_user_variables(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(check_user_variables_name);
}

// static
bool MaskingFilterConfig::get_check_unions(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(check_unions_name);
}

// static
bool MaskingFilterConfig::get_check_subqueries(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(check_subqueries_name);
}

// static
bool MaskingFilterConfig::get_require_fully_parsed(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(require_fully_parsed_name);
}

// static
bool MaskingFilterConfig::get_treat_string_arg_as_field(const MXS_CONFIG_PARAMETER* pParams)
{
    return pParams->get_bool(treat_string_arg_as_field_name);
}
