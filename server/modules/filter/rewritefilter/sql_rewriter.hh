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
#include "template_reader.hh"

#include <string>

class Replacer;

/**
 * @brief RewriteSql is the base class for concrete rewriters, of which
 *        there are exactly two; one for the Native replacer and one for
 *        regex match and replace.
 */
class SqlRewriter
{
public:
    SqlRewriter(const TemplateDef& template_def);

    // Did parsing of the templates succeed?
    bool        is_valid() const;
    std::string error_str() const;

    /**
     * @brief  replace
     * @param  sql - to be examined
     * @param  pSql - replacement is placed here
     * @return true if replacement was done (regex match)
     */
    virtual bool replace(const std::string& sql, std::string* pSql) const = 0;

    const std::string& match_template() const;
    const std::string& replace_template() const;
    const TemplateDef& template_def() const;

protected:
    void set_error_string(const std::string& err_msg);
    // throws on error
    std::regex make_regex(const TemplateDef& def, const std::string& regex_str);

private:
    const TemplateDef m_template_def;
    const std::string m_regex_template;
    const std::string m_replace_template;
    std::string       m_error_str;  // human readable error string, after construction
};

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

inline void SqlRewriter::set_error_string(const std::string& err_msg)
{
    m_error_str = err_msg;
}

inline bool SqlRewriter::is_valid() const
{
    return m_error_str.empty();
}

inline std::string SqlRewriter::error_str() const
{
    return m_error_str;
}
