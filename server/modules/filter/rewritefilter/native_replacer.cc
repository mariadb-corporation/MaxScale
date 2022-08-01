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

#include "native_replacer.hh"
#include <sstream>
#include <iostream>


void NativeReplacer::set_replace_template(const std::string& replace_template)
{
    // This does almost the same thing as RewriteSql::RewriteSql() but instead of
    // creating a regex string and regex, it creates a vector of
    // sql parts and placeholders ordinals.
    std::ostringstream error_stream;
    auto last = end(replace_template);
    auto ite = begin(replace_template);

    std::string current_sql_part;

    while (ite != last)
    {
        switch (*ite)
        {
        case '\\':
            {
                current_sql_part += *ite;
                if (ite + 1 != last)
                {
                    ++ite;
                    current_sql_part += *ite;
                }
            }
            break;

        case PLACEHOLDER_CHAR:
            {
                ++m_nreplacements;

                int n{};
                std::string regex;
                ite = read_placeholder(ite, last, &n, &regex);

                if (!regex.empty())
                {
                    error_stream << "Cannot define placeholders with a regex in the replacement template"
                                 << ": " << replace_template;
                }

                if (n <= 0)
                {
                    if (n < 0)
                    {
                        auto into_placeholder = (last - ite >= 5) ? 5 : (last - ite);
                        auto new_last = ite + into_placeholder;
                        error_stream << "Invalid placeholder \""
                                     << std::string(begin(replace_template), new_last)
                                     << "...\"";
                        ite = last;
                        break;
                    }
                    current_sql_part += *ite++;
                    continue;
                }

                m_max_placeholder_ordinal = std::max(m_max_placeholder_ordinal, n);

                if (!current_sql_part.empty())
                {
                    m_parts.push_back(current_sql_part);
                    current_sql_part.clear();
                }

                m_parts.push_back(n - 1);   // to zero-based
            }
            break;

        default:
            current_sql_part += *ite++;
            break;
        }
    }

    if (!current_sql_part.empty())
    {
        m_parts.push_back(current_sql_part);
    }

    m_error_str = error_stream.str();
}

std::string NativeReplacer::replace(const std::vector<std::string>& replacements) const
{
    std::string sql;
    for (auto part : m_parts)
    {
        if (std::holds_alternative<int>(part))
        {
            sql += replacements[std::get<int>(part)];
        }
        else
        {
            sql += std::get<std::string>(part);
        }
    }

    return sql;
}
