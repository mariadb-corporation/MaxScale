/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "sql_rewriter.hh"

SqlRewriter::SqlRewriter(const TemplateDef& template_def)
    : m_template_def(template_def)
    , m_regex_template(m_template_def.match_template)
    , m_replace_template(m_template_def.replace_template)
{
}

std::regex SqlRewriter::make_regex(const TemplateDef& def, const std::string& regex_str)
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

std::string ignore_whitespace_in_regex(const std::string& regex)
{
    std::string new_regex;
    auto ite = begin(regex);
    const auto last = end(regex);

    while (ite != last)
    {
        if (std::isspace(*ite))
        {
            while (++ite != last && std::isspace(*ite))
            {
                // pass
            }

            if (ite != last)
            {
                new_regex += "[[:space:]]*";
            }
        }
        else
        {
            new_regex += *ite++;
        }
    }

    return new_regex;
}
