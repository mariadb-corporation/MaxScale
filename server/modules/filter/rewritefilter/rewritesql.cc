/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2026-06-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "rewritesql.hh"

RewriteSql::RewriteSql(const TemplateDef& template_def)
    : m_template_def(template_def)
    , m_regex_template(m_template_def.match_template)
    , m_replace_template(m_template_def.replace_template)
{
}

std::regex RewriteSql::make_regex(const TemplateDef& def, const std::string& regex_str)
{
    namespace rx = std::regex_constants;

    auto grammar = to_regex_grammar_flag(def.regex_grammar);
    auto flags = grammar | rx::optimize;

    if (def.case_sensitive == false)
    {
        flags |= rx::icase;
    }

    return std::regex{regex_str, flags};
}
