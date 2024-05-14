/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-04-10
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
    const std::string special_chars = R"($().?+{}\[*)";
    if (special_chars.find(ch) != std::string::npos)
    {
        *str += '\\';
    }

    *str += ch;
}
}

NativeRewriter::NativeRewriter(const TemplateDef& def)
    : SqlRewriter(def)
{
    try
    {
        const std::string normal_group{"(.*?)"s};
        bool starts_with_placeholder = false;
        bool ends_with_placeholder = false;

        auto const first = begin(match_template());
        auto const last = end(match_template());
        auto ite = begin(match_template());

        while (ite != last)
        {
            switch (*ite)
            {
            case PLACEHOLDER_CHAR:
                {
                    ++m_nreplacements;

                    if (ite == first)
                    {
                        starts_with_placeholder = true;
                    }

                    int n{};
                    std::string regex;
                    ite = read_placeholder(ite, last, &n, &regex);

                    if (n <= 0)
                    {
                        if (n < 0)
                        {
                            auto into_placeholder = (last - ite >= 5) ? 5 : (last - ite);
                            auto new_last = ite + into_placeholder;
                            MXB_THROW(RewriteError, "Invalid placeholder \""
                                      << std::string(begin(match_template()), new_last)
                                      << "...\" " << "Expected ']' or ':'");
                            ite = last;
                            break;
                        }
                        write_regex_char(&m_regex_str, *ite++);
                        continue;
                    }
                    m_max_ordinal = std::max(m_max_ordinal, n);
                    m_ordinals.push_back(n - 1);

                    std::string group;
                    if (regex.empty())
                    {
                        group = normal_group;
                    }
                    else
                    {
                        group = "("s + regex + ")";
                    }
                    m_regex_str += group;

                    if (ite == last)
                    {
                        ends_with_placeholder = true;
                    }
                    continue;
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

        std::string replacement_str = def.replace_template;
        int start_auto = -1;
        int end_auto = -1;

        if (!starts_with_placeholder)
        {
            m_ordinals.push_front(m_max_ordinal);
            ++m_nreplacements;
            ++m_max_ordinal;
            m_regex_str = normal_group + m_regex_str;
            replacement_str = "@{" + std::to_string(m_max_ordinal) + '}' + replacement_str;
            start_auto = m_max_ordinal;
        }

        if (!ends_with_placeholder)
        {
            m_ordinals.push_back(m_max_ordinal);
            ++m_nreplacements;
            ++m_max_ordinal;
            m_regex_str += normal_group;
            replacement_str += "@{" + std::to_string(m_max_ordinal) + '}';
            end_auto = m_max_ordinal;
        }

        if (def.ignore_whitespace)
        {
            m_regex_str = ignore_whitespace_in_regex(m_regex_str);
        }

        MXB_SINFO("Native regex: " << m_regex_str);
        m_replacer.set_replace_template(replacement_str, start_auto, end_auto);
        if (!starts_with_placeholder || !ends_with_placeholder)
        {
            MXB_SINFO("Modified replacement: " << replacement_str);
        }

        make_ordinals();

        try
        {
            m_regex = make_regex(template_def(), m_regex_str);
        }
        catch (const std::exception& ex)
        {
            MXB_THROW(RewriteError, ex.what());
        }
    }
    catch (const std::exception& ex)
    {
        MXB_THROW(RewriteError, ex.what());
    }
}

void NativeRewriter::make_ordinals()
{
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
    std::deque<int> monotonical(unique_ordinals.size());
    std::iota(begin(monotonical), end(monotonical), 0);
    if (monotonical != unique_ordinals)
    {
        MXB_THROW(RewriteError,
                  "The placeholder numbers (not positions) must be strictly ordered (1,2,3,...)");
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
}

bool NativeRewriter::replace(const std::string& sql, std::string* pSql) const
{
    auto first = begin(sql);
    auto last = end(sql);

    // Skip trailing semicolons
    auto last_pos = sql.length();
    for (size_t pos = last_pos;
         pos > 0
         && (pos = sql.find_last_not_of(" \t\r\n\v\f", pos)) != std::string::npos
         && sql[pos] == ';';
         --pos)
    {
        last_pos = pos;
    }
    last = first + last_pos;

    std::smatch match;
    bool matched = std::regex_match(first, last, match, m_regex);

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
    else
    {
        matched = false;
    }

    return matched;
}
