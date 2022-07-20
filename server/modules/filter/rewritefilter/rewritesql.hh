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
#pragma once

#include <maxbase/ccdefs.hh>
#include "replacer.hh"
#include "template_reader.hh"

#include <string>
#include <regex>
#include <memory>

class Replacer;

/**
 * @brief RewriteSql takes a "match template" where there are placeholders
 * for text that should be replaced in the corresponding "replace template".
 *
 * example:
 * match_template:   "select count(distinct @{1}) from @{2}"
 * replace_template: "select count(*) from (select distinct @{1} from @{2}) as t"
 */
class RewriteSql
{
public:
    RewriteSql(const TemplateDef& template_def);

    // Did parsing of the templates succeed?
    bool        is_valid() const;
    std::string error_str() const;

    /**
     * @brief  replace
     * @param  sql - to be examined
     * @param  pSql - replacement is placed here
     * @return true if replacement was done (regex match)
     */
    bool replace(const std::string& sql, std::string* pSql) const;

    const std::string& match_template() const;
    const std::string& regex_str() const;
    const std::string& replace_template() const;
    size_t             num_replacements() const;
    const TemplateDef& template_def() const;

private:
    std::string make_ordinals();
    std::regex  make_regex(const TemplateDef& def);

    const TemplateDef m_template_def;
    const std::string m_regex_template;
    const std::string m_replace_template;
    std::string       m_error_str;  // human readable error string, after construction
    std::string       m_regex_str;
    std::regex        m_regex;
    size_t            m_nreplacements = 0;

    size_t              m_max_ordinal = 0;
    std::vector<size_t> m_ordinals;

    // A mapping from an (implied) index to its respective index in m_ordinals.
    // This allows grabbing the match groups and placing them in
    // the replacement vector for the Replacer, where index 0 contains @1,
    // index 1 @2 etc.
    std::vector<size_t> m_map_ordinals;

    // Pairs in m_ordinals with the same ordinal (forward reference)
    std::vector<std::pair<size_t, size_t>> m_match_pairs;

    Replacer m_replacer;
};

inline bool RewriteSql::is_valid() const
{
    return m_error_str.empty();
}

inline std::string RewriteSql::error_str() const
{
    return m_error_str;
}

inline const std::string& RewriteSql::match_template() const
{
    return m_regex_template;
}

inline const std::string& RewriteSql::regex_str() const
{
    return m_regex_str;
}

inline const std::string& RewriteSql::replace_template() const
{
    return m_replace_template;
}

inline size_t RewriteSql::num_replacements() const
{
    return m_nreplacements;
}

inline const TemplateDef& RewriteSql::template_def() const
{
    return m_template_def;
}
