/*
 * Copyright (c) 2022 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxbase/ccdefs.hh>
#include <maxbase/assert.hh>
#include <string>
#include <vector>
#include <variant>

const char PLACEHOLDER_CHAR = '@';

/**
 * @brief The Replacer does the actual replacement of sql-parts.
 */
class NativeReplacer
{
public:
    NativeReplacer() = default;

    /**
     * @brief set_replace_template - Builds an internal structure from the replace_template.
     *                               start_auto and end_auto are for generating an error
     *                               if the user part of the replace_template uses auto
     *                               generated placeholders.
     * @param replace_template     - @see RewriteSql
     * @param start_auto           - Ordinal of auto start placeholder or -1 if not auto
     * @param end_auto             - Ordinal of auto end placeholder or -1 if not auto
     */
    void set_replace_template(const std::string& replace_template,
                              int start_auto,
                              int end_auto);

    size_t num_replacements() const;

    /**
     * @brief  replace
     * @param  replacements - An array of replacements corresponding to
     *         the placeholders @{1}, @{2},... in the match_template.
     *         Placeholders can be reused and eliminated
     *         in the replace_template
     * @return A string where the placeholders in the replace_template
     *         are replaced with strings from the replacements vector.
     */
    std::string replace(const std::vector<std::string>& replacements) const;
private:
    using StringOrOrdinal = std::variant<std::string, int>;
    /* If the replacement template is "select @{1} from @{2}"
     * then the vector of StringOrOrdinals is:
     * {"select ", 0, " from ", 1} (0-based, 1-based in the match_template).
     */
    std::vector<StringOrOrdinal> m_parts;
    size_t                       m_nreplacements = 0;
};

inline size_t NativeReplacer::num_replacements() const
{
    return m_nreplacements;
}

/**
 * @brief read_placeholder - read a placeholder of the form @{n[:regex]} where n is an integer.
 * @param cfirst   - beginning of input
 * @param last     - end of input
 * @param *ordinal - n on success.
 *                   0 not a placeholder, does not start with "@{".
 *                   <=0 invalid placeholder
 * @param regex    - regex if a regex was specified and the ordinal was read successfully
 * @return iterator to one passed the placeholder on success else cfirst
 */
template<typename Iterator>
Iterator read_placeholder(const Iterator cfirst, const Iterator last,
                          int* pOrdinal, std::string* pRegex)
{
    mxb_assert(cfirst != last && *cfirst == PLACEHOLDER_CHAR);

    pRegex->clear();
    auto first = cfirst;

    // Should start with "@{"
    if (*first != '@' || ++first == last || *first != '{' || ++first == last)
    {
        *pOrdinal = 0;      // not a placeholder
        return cfirst;
    }

    // Read the placeholder ordinal
    std::string nstr;
    for (; first != last; ++first)
    {
        if (std::isdigit(*first))
        {
            nstr.push_back(*first);
        }
        else
        {
            break;
        }
    }

    if (nstr.empty())
    {
        *pOrdinal = -1;
        return cfirst;
    }

    *pOrdinal = std::atoi(nstr.c_str());
    if (*pOrdinal <= 0)
    {
        return cfirst;
    }

    // Read the regex, if any.
    if (*first == ':')
    {
        while (++first != last)
        {
            // Right brace '}' has to be escaped by the user
            if (*first == '\\' && *first == '}')
            {
                if (++first != last)
                {
                    *pRegex += *first;
                }
            }
            else if (*first == '}')
            {
                break;
            }
            else
            {
                *pRegex += *first;
            }
        }

        if (pRegex->empty())
        {
            *pOrdinal = -1;
            return cfirst;
        }
    }

    // Should close with a '}'
    if (first == last || *first != '}')
    {
        *pOrdinal = -1;
        return cfirst;
    }

    return ++first;
}
