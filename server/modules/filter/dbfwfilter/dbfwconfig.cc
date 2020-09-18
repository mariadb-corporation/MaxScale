/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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

#include "dbfwconfig.hh"

namespace config = mxs::config;

namespace
{

namespace dbfwfilter
{

config::Specification specification(MXS_MODULE_NAME, config::Specification::FILTER);

config::ParamPath rules(
    &specification,
    "rules",
    "Mandatory parameter that specifies the path of the rules file.",
    config::ParamPath::R
    );

config::ParamBool log_match(
    &specification,
    "log_match",
    "Optional boolean parameters specifying whether a query that matches a rule should be logged. "
    "Default is false.",
    false
    );
config::ParamBool log_no_match(
    &specification,
    "log_no_match",
    "Optional boolean parameters specifying whether a query that does not match a rule should be logged. "
    "Default is false.",
    false
    );
config::ParamBool treat_string_as_field(
    &specification,
    "treat_string_as_field",
    "Optional boolean parameter specifying whether strings should be treated as fields. Causes "
    "column blocking rules to match even if ANSI_QUOTES has been enabled and \" is used instead of "
    "backtick. Default is true.",
    true
    );

config::ParamBool treat_string_arg_as_field(
    &specification,
    "treat_string_arg_as_field",
    "Optional boolean parameter specifying whether strings should be treated as fields when used "
    "as arguments to functions. Causes function column blocking rules to match even if ANSI_QUOTES "
    "has been enabled and \" is used instead of backtick. Default is true.",
    true
    );

config::ParamBool strict(
    &specification,
    "strict",
    "Whether to treat unsupported SQL or multi-statement SQL as an error.",
    true
    );

config::ParamEnum<fw_actions> action(
    &specification,
    "action",
    "Optional enumeration parameter specifying the action to be taken when a rule matches. "
    "Default is to block.",
        {
            {FW_ACTION_ALLOW, "allow"},
            {FW_ACTION_BLOCK, "block"},
            {FW_ACTION_IGNORE, "ignore"},
        },
    FW_ACTION_BLOCK
    );
}
}

DbfwConfig::DbfwConfig(const std::string& name)
    : config::Configuration(name, &dbfwfilter::specification)
{
    add_native(&DbfwConfig::rules, &dbfwfilter::rules);
    add_native(&DbfwConfig::log_match, &dbfwfilter::log_match);
    add_native(&DbfwConfig::log_no_match, &dbfwfilter::log_no_match);
    add_native(&DbfwConfig::treat_string_as_field, &dbfwfilter::treat_string_as_field);
    add_native(&DbfwConfig::treat_string_arg_as_field, &dbfwfilter::treat_string_arg_as_field);
    add_native(&DbfwConfig::action, &dbfwfilter::action);
    add_native(&DbfwConfig::strict, &dbfwfilter::strict);
}

// static
void DbfwConfig::populate(MXS_MODULE& module)
{
    module.specification = &dbfwfilter::specification;
}
