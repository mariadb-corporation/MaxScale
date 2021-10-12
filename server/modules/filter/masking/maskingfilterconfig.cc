/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#define MXS_MODULE_NAME "masking"
#include "maskingfilterconfig.hh"
#include "maskingfilter.hh"
#include "maskingrules.hh"

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
            {MaskingFilterConfig::LARGE_IGNORE, "ignore"},
            {MaskingFilterConfig::LARGE_ABORT, "abort"}
        },
    MaskingFilterConfig::LARGE_ABORT,
    config::Param::AT_RUNTIME);

config::ParamPath rules(
    &specification,
    "rules",
    "Specifies the path of the file where the masking rules are stored.",
    MXS_MODULE_OPT_PATH_R_OK,
    config::Param::AT_RUNTIME);

config::ParamEnum<MaskingFilterConfig::warn_type_mismatch_t> warn_type_mismatch(
    &specification,
    "warn_type_mismatch",
    "Log warning if rule matches a column that is not of expected type.",
        {
            {MaskingFilterConfig::WARN_NEVER, "never"},
            {MaskingFilterConfig::WARN_ALWAYS, "always"}
        },
    MaskingFilterConfig::WARN_NEVER,
    config::Param::AT_RUNTIME);

config::ParamBool prevent_function_usage(
    &specification,
    "prevent_function_usage",
    "If true, then statements containing functions referring to masked "
    "columns will be blocked.",
    true,
    config::Param::AT_RUNTIME);

config::ParamBool check_user_variables(
    &specification,
    "check_user_variables",
    "If true, then SET statemens that are defined using SELECT referring to "
    "masked columns will be blocked.",
    true,
    config::Param::AT_RUNTIME);

config::ParamBool check_unions(
    &specification,
    "check_unions",
    "If true, then if the second SELECT in a UNION refers to a masked colums "
    "the statement will be blocked.",
    true,
    config::Param::AT_RUNTIME);

config::ParamBool check_subqueries(
    &specification,
    "check_subqueries",
    "If true, then if a subquery refers to masked columns the statement will be blocked.",
    true,
    config::Param::AT_RUNTIME);

config::ParamBool require_fully_parsed(
    &specification,
    "require_fully_parsed",
    "If true, then statements that cannot be fully parsed will be blocked.",
    true,
    config::Param::AT_RUNTIME);

config::ParamBool treat_string_arg_as_field(
    &specification,
    "treat_string_arg_as_field",
    "If true, then strings given as arguments to function will be handles "
    "as if they were names.",
    true,
    config::Param::AT_RUNTIME);
}
}

MaskingFilterConfig::MaskingFilterConfig(const char* zName, MaskingFilter& filter)
    : mxs::config::Configuration(zName, &masking::specification)
    , m_filter(filter)
{
    using Cfg = MaskingFilterConfig;

    add_native(&Cfg::m_v, &Values::large_payload, &masking::large_payload);
    add_native(&Cfg::m_v, &Values::rules, &masking::rules);
    add_native(&Cfg::m_v, &Values::warn_type_mismatch, &masking::warn_type_mismatch);
    add_native(&Cfg::m_v, &Values::prevent_function_usage, &masking::prevent_function_usage);
    add_native(&Cfg::m_v, &Values::check_user_variables, &masking::check_user_variables);
    add_native(&Cfg::m_v, &Values::check_unions, &masking::check_unions);
    add_native(&Cfg::m_v, &Values::check_subqueries, &masking::check_subqueries);
    add_native(&Cfg::m_v, &Values::require_fully_parsed, &masking::require_fully_parsed);
    add_native(&Cfg::m_v, &Values::treat_string_arg_as_field, &masking::treat_string_arg_as_field);
}

// static
void MaskingFilterConfig::populate(MXS_MODULE& info)
{
    info.specification = &masking::specification;
}

bool MaskingFilterConfig::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool ok = false;

    if (reload_rules())
    {
        ok = true;

        if (m_v.treat_string_arg_as_field)
        {
            QC_CACHE_PROPERTIES cache_properties;
            qc_get_cache_properties(&cache_properties);

            if (cache_properties.max_size != 0)
            {
                MXS_NOTICE("The parameter 'treat_string_arg_as_field' is enabled for %s, "
                           "disabling the query classifier cache.",
                           name().c_str());

                cache_properties.max_size = 0;
                qc_set_cache_properties(&cache_properties);
            }
        }
    }

    return ok;
}

bool MaskingFilterConfig::reload_rules()
{
    bool ok = false;

    if (auto sRules = MaskingRules::load(m_v.rules.c_str()))
    {
        ok = true;
        m_v.sRules = std::move(sRules);
        m_values.assign(m_v);
    }

    return ok;
}
