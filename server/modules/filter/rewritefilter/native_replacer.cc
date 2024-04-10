/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "native_replacer.hh"
#include "template_reader.hh"
#include <sstream>
#include <iostream>



void NativeReplacer::set_replace_template(const std::string& replace_template,
                                          int start_auto,
                                          int end_auto)

{
    // This does almost the same thing as RewriteSql::RewriteSql() but instead of
    // creating a regex string and regex, it creates a vector of
    // sql parts and placeholders ordinals.
    auto last = end(replace_template);
    auto ite = begin(replace_template);

    std::string current_sql_part;
    std::vector<int> ordinals;      // for error checking against start_auto and end_auto

    while (ite != last)
    {
        switch (*ite)
        {
        case PLACEHOLDER_CHAR:
            {
                ++m_nreplacements;

                int n{};
                std::string regex;
                ite = read_placeholder(ite, last, &n, &regex);

                if (!regex.empty())
                {
                    MXB_THROW(RewriteError,
                              "Cannot define placeholders with a regex in the replacement template"
                              << ": " << replace_template);
                }

                if (n <= 0)
                {
                    if (n < 0)
                    {
                        auto into_placeholder = (last - ite >= 5) ? 5 : (last - ite);
                        auto new_last = ite + into_placeholder;
                        MXB_THROW(RewriteError, "Invalid placeholder \""
                                  << std::string(begin(replace_template), new_last)
                                  << "...\" " << "Expected ']'");
                        ite = last;
                        break;
                    }
                    current_sql_part += *ite++;
                    continue;
                }

                ordinals.push_back(n);

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

    if (std::count(begin(ordinals), end(ordinals), start_auto) > 1
        || std::count(begin(ordinals), end(ordinals), end_auto) > 1)
    {
        MXB_THROW(RewriteError,
                  "The replacement template has larger placeholder"
                  "numbers than the match template");
    }
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
