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

#include "native_rewriter.hh"
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

NativeRewriter::NativeRewriter(const TemplateDef& def)
    : SqlRewriter(def)
    , m_replacer(def.replace_template)
{
    std::ostringstream error_stream;
    std::string error_str;

    if (!m_replacer.is_valid())
    {
        error_stream << m_replacer.error_str();
    }
    else
    {
        auto last = end(match_template());
        auto ite = begin(match_template());

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

                    int n{};
                    ite = read_placeholder(ite, last, &n);

                    if (n <= 0)
                    {
                        if (n < 0)
                        {
                            auto into_placeholder = (last - ite >= 5) ? 5 : (last - ite);
                            auto new_last = ite + into_placeholder;
                            error_stream << "Invalid placeholder \""
                                         << std::string(begin(match_template()), new_last)
                                         << "...\"";
                            ite = last;
                            break;
                        }
                        write_regex_char(&m_regex_str, *ite++);
                        continue;
                    }
                    m_max_ordinal = std::max(m_max_ordinal, n);
                    m_ordinals.push_back(n - 1);

                    const std::string group = "(.*?)";
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

    error_str = error_stream.str();

    if (error_str.empty())
    {
        error_str = make_ordinals();
    }

    if (error_str.empty())
    {
        try
        {
            m_regex = make_regex(template_def(), m_regex_str);
        }
        catch (const std::exception& ex)
        {
            error_str = ex.what();
        }
    }

    if (!error_str.empty())
    {
        set_error_string(error_str);
    }
}

std::string NativeRewriter::make_ordinals()
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
    std::vector<int> monotonical(unique_ordinals.size());
    std::iota(begin(monotonical), end(monotonical), 0);
    if (monotonical != unique_ordinals)
    {
        return "The placeholder numbers (not positions) must be strictly ordered (1,2,3,...)";
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

bool NativeRewriter::replace(const std::string& sql, std::string* pSql) const
{
    std::smatch match;
    bool matched = std::regex_match(sql, match, m_regex);

    if (matched && match.size() == m_nreplacements + 1)
    {
        // Check forward references if any
        for (const auto& p : m_match_pairs)
        {
            if (match[p.first + 1] != match[p.second + 1])
            {
                matched = false;
                break;
            }
        }

        if (matched)
        {
            std::vector<std::string> replacements;      // TODO, use string_view.

            for (size_t i = 0; i < m_map_ordinals.size(); ++i)
            {
                replacements.emplace_back(match[m_map_ordinals[i] + 1]);
            }

            *pSql = m_replacer.replace(replacements);
        }
    }

    return matched;
}
