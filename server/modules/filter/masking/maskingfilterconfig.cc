/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingfilterconfig.hh"

namespace
{

namespace masking
{

namespace config = mxs::config;

config::Specification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamEnum<MaskingFilterConfig::large_payload_t> large_payload(
    &specification,
    "large_payload",
    "How large, i.e. larger than 16MB, payloads should be handled.",
    {
        { MaskingFilterConfig::LARGE_IGNORE, "ignore" },
        { MaskingFilterConfig::LARGE_ABORT,  "abort" }
    },
    MaskingFilterConfig::LARGE_ABORT);

config::ParamPath rules(
    &specification,
    "rules",
    "Specifies the path of the file where the masking rules are stored.",
    MXS_MODULE_OPT_PATH_R_OK);

config::ParamEnum<MaskingFilterConfig::warn_type_mismatch_t> warn_type_mismatch(
    &specification,
    "warn_type_mismatch",
    "Log warning if rule matches a column that is not of expected type.",
    {
        { MaskingFilterConfig::WARN_NEVER, "never" },
        { MaskingFilterConfig::WARN_ALWAYS, "always" }
    },
    MaskingFilterConfig::WARN_NEVER);

config::ParamBool prevent_function_usage(
    &specification,
    "prevent_function_usage",
    "If true, then statements containing functions referring to masked "
    "columns will be blocked.",
    true);

config::ParamBool check_user_variables(
    &specification,
    "check_user_variables",
    "If true, then SET statemens that are defined using SELECT referring to "
    "masked columns will be blocked.",
    true);

config::ParamBool check_unions(
    &specification,
    "check_unions",
    "If true, then if the second SELECT in a UNION refers to a masked colums "
    "the statement will be blocked.",
    true);

config::ParamBool check_subqueries(
    &specification,
    "check_subqueries",
    "If true, then if a subquery refers to masked columns the statement will be blocked.",
    true);

config::ParamBool require_fully_parsed(
    &specification,
    "require_fully_parsed",
    "If true, then statements that cannot be fully parsed will be blocked.",
    true);

config::ParamBool treat_string_arg_as_field(
    &specification,
    "treat_string_arg_as_field",
    "If true, then strings given as arguments to function will be handles "
    "as if they were names.",
    true);

}

}

MaskingFilterConfig::MaskingFilterConfig(const char* zName)
    : mxs::config::Configuration(zName, &masking::specification)
{
    add_native(&MaskingFilterConfig::m_large_payload, &masking::large_payload);
    add_native(&MaskingFilterConfig::m_rules, &masking::rules);
    add_native(&MaskingFilterConfig::m_warn_type_mismatch, &masking::warn_type_mismatch);
    add_native(&MaskingFilterConfig::m_prevent_function_usage, &masking::prevent_function_usage);
    add_native(&MaskingFilterConfig::m_check_user_variables, &masking::check_user_variables);
    add_native(&MaskingFilterConfig::m_check_unions, &masking::check_unions);
    add_native(&MaskingFilterConfig::m_check_subqueries, &masking::check_subqueries);
    add_native(&MaskingFilterConfig::m_require_fully_parsed, &masking::require_fully_parsed);
    add_native(&MaskingFilterConfig::m_treat_string_arg_as_field, &masking::treat_string_arg_as_field);
}

//static
void MaskingFilterConfig::populate(MXS_MODULE& info)
{
    info.specification = &masking::specification;
}
