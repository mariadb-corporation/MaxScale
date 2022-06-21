#pragma once

#include <maxbase/ccdefs.hh>
#include <string>
#include <regex>
#include <memory>
#include "replacer.hh"

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
    RewriteSql(const std::string& match_template, const std::string& replace_template);

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

private:
    const std::string m_regex_template;
    const std::string m_replace_template;
    std::string       m_error_str;  // human readable error string, after construction
    std::string       m_regex_str;
    std::regex        m_regex;
    size_t            m_nreplacements = 0;

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
