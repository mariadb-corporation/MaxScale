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

#include "regex_rewriter.hh"
#include <maxbase/log.hh>

using namespace std::string_literals;


RegexRewriter::RegexRewriter(const TemplateDef& def)
    : SqlRewriter(def)
{
    try
    {
        m_match_regex = make_regex(template_def(), template_def().match_template);
    }
    catch (const std::exception& ex)
    {
        set_error_string(ex.what());
    }
}

bool RegexRewriter::replace(const std::string& sql, std::string* pSql) const
{
    namespace rx = std::regex_constants;
    bool matched = false;
    try
    {
        // This is inefficient, regex_replace() does not indicate if a match
        // happened but instead returns the original sql when there is no
        // match (what was the standards comittee thinking?).
        std::smatch match;
        matched = std::regex_search(sql, match, m_match_regex);

        if (matched)
        {
            *pSql = std::regex_replace(sql, m_match_regex, replace_template());
        }
    }
    catch (const std::exception& ex)
    {
        MXB_SERROR("Regex replacement error: " << ex.what());
        matched = false;
    }

    return matched;
}
