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
#include <iostream>
#include <sstream>
#include <numeric>
#include <memory>
#include <set>

using namespace std::string_literals;

namespace
{
void write_regex_char(std::string* str, char ch)
{
    if (ch == '(' || ch == ')')
    {
        *str += '\\';
    }
    *str += ch;
}
}

RewriteSql::RewriteSql(const std::string& match_template,
                       const std::string& replace_template)
    : m_regex_template(match_template)
    , m_replace_template(replace_template)
    , m_replacer(replace_template)
{
    std::ostringstream error_stream;

    if (!m_replacer.is_valid())
    {
        error_stream << m_replacer.error_str();
    }
    else
    {
        auto last = end(m_regex_template);
        auto ite = begin(m_regex_template);

        while (ite != last)
        {
            switch (*ite)
            {
            case '\\':
                {
                    m_regex_str += *ite;
                    if (ite + 1 != last)
                    {
                        m_regex_str += *++ite;
                    }
                }
                break;

            case PLACEHOLDER_CHAR:
                {
                    ++m_nreplacements;
                    auto ite_before = ite - 1;      // for error output

                    size_t n{};
                    ite = read_placeholder(ite, last, &n);

                    if (n == 0)
                    {
                        error_stream << "Invalid placeholder at: " << std::string(ite_before, last);
                        ite = last;
                        continue;
                    }

                    m_max_ordinal = std::max(m_max_ordinal, n);
                    m_ordinals.push_back(n - 1);

                    const std::string group = "(.*)";
                    m_regex_str += group;

                    if (ite != last)
                    {
                        write_regex_char(&m_regex_str, *ite);
                    }
                }
                break;

            default:
                write_regex_char(&m_regex_str, *ite);
                break;
            }

            if (ite != last)
            {
                ++ite;
            }
        }
    }

    m_error_str = error_stream.str();

    if (m_error_str.empty())
    {
        m_error_str = make_ordinals();
    }

    if (m_error_str.empty())
    {
        try
        {
            m_regex = std::regex{m_regex_str};
        }
        catch (const std::exception& ex)
        {
            m_error_str = ex.what();
        }
    }
}

std::string RewriteSql::make_ordinals()
{
    if (m_replacer.max_placeholder_ordinal() > m_max_ordinal)
    {
        return "The replacement template has larger placeholder numbers than the match template";
    }

    auto ords = m_ordinals;
    std::sort(begin(ords), end(ords));

    // grab duplicates before making ords contain the unique ordinals
    std::set<size_t> duplicates;
    auto dup_ite = begin(ords);
    while (dup_ite != end(ords))
    {
        auto dup_ite2 = std::adjacent_find(dup_ite, end(ords));
        if (dup_ite2 != end(ords))
        {
            duplicates.insert(*dup_ite2++);
        }
        dup_ite = dup_ite2;
    }

    ords.erase(std::unique(begin(ords), end(ords)), end(ords));
    const auto& unique_ordinals = ords;
    std::vector<size_t> monotonical(unique_ordinals.size());
    std::iota(begin(monotonical), end(monotonical), 0);
    if (monotonical != unique_ordinals)
    {
        return "The placeholder numbers must be strictly ordered (1,2,3,...)";
    }

    // Make m_map_ordinals
    m_map_ordinals.resize(unique_ordinals.size());
    auto begin_ordinals = begin(m_ordinals);
    auto end_ordinals = end(m_ordinals);
    for (size_t i = 0; i < m_map_ordinals.size(); ++i)
    {
        auto ite = std::find(begin_ordinals, end_ordinals, i);
        mxb_assert(ite != end_ordinals);
        m_map_ordinals[i] = ite - begin_ordinals;
    }

    // Make m_match_pairs
    for (auto dup : duplicates)
    {
        auto ite = std::find(begin_ordinals, end_ordinals, dup);
        mxb_assert(ite != end_ordinals);
        while (ite != end_ordinals)
        {
            auto ite2 = std::find(ite + 1, end_ordinals, dup);
            if (ite2 != end_ordinals)
            {
                m_match_pairs.push_back(std::make_pair(ite - begin_ordinals, ite2 - begin_ordinals));
            }
            ite = ite2;
        }
    }

    return "";
}

bool RewriteSql::replace(const std::string& sql, std::string* pSql) const
{
    std::smatch match;
    bool matched = std::regex_search(sql, match, m_regex);

    if (!matched || match.size() != m_nreplacements + 1)
    {
        return false;
    }

    // Check forward references if any
    for (auto p : m_match_pairs)
    {
        if (match[p.first + 1] != match[p.second + 1])
        {
            matched = false;
            break;
        }
    }

    if (!matched)
    {
        return false;
    }

    std::vector<std::string> replacements;      // TODO, use string_view.

    for (size_t i = 0; i < m_map_ordinals.size(); ++i)
    {
        replacements.emplace_back(match[m_map_ordinals[i] + 1]);
    }

    *pSql = m_replacer.replace(replacements);

    return true;
}
