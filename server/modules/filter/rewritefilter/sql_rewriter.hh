/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include "template_reader.hh"

#include <string>

/**
 * @brief RewriteSql is the base class for concrete rewriters, of which
 *        there are exactly two; one for the Native replacer and one for
 *        regex match and replace.
 */
class SqlRewriter
{
public:
    virtual ~SqlRewriter() = default;

    SqlRewriter(const TemplateDef& template_def);

    /**
     * @brief  replace
     * @param  sql - to be examined
     * @param  pSql - replacement is placed here
     * @return true if replacement was done (regex match)
     *
     * NOTE: replace() may be called with pSql pointing to sql.
     */
    virtual bool replace(const std::string& sql, std::string* pSql) const = 0;

    const std::string& match_template() const;
    const std::string& replace_template() const;
    const TemplateDef& template_def() const;

protected:
    // throws on error
    std::regex make_regex(const TemplateDef& def, const std::string& regex_str);

private:
    const TemplateDef m_template_def;
    const std::string m_regex_template;
    const std::string m_replace_template;
};

std::vector<std::unique_ptr<SqlRewriter>> create_rewriters(const std::vector<TemplateDef>& templates);

/**
 * @brief ignore_whitespace_in_regex
 * @param regex
 * @return regex with all stretches of whitespace replaced
 *         with the equivalent of extended regex "[[:space:]]+"
 */
std::string ignore_whitespace_in_regex(const std::string& regex);


// IMPL

inline const std::string& SqlRewriter::match_template() const
{
    return m_regex_template;
}

inline const std::string& SqlRewriter::replace_template() const
{
    return m_replace_template;
}

inline const TemplateDef& SqlRewriter::template_def() const
{
    return m_template_def;
}
