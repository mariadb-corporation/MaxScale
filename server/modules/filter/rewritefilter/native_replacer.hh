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
#include <maxbase/assert.hh>
#include <string>
#include <vector>
#include <variant>

const char PLACEHOLDER_CHAR = '@';

/**
 * @brief The Replacer does the actual replacement of sql-parts.
 */
class Replacer
{
public:
    /**
     * @brief Replacer. Builds an internal structure from the replace_template
     * @param replace_template @see RewriteSql
     */
    Replacer(const std::string& replace_template);

    /* Is the replace_template valid */
    bool        is_valid() const;
    std::string error_str() const;

    size_t num_replacements() const;
    int    max_placeholder_ordinal() const;

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
    std::string m_replace_template;
    using StringOrOrdinal = std::variant<std::string, int>;
    /* If the replacement template is "select @{1} from @{2}"
     * then the vector of StringOrOrdinals is:
     * {"select ", 0, " from ", 1} (0-based, 1-based in the match_template).
     */
    std::vector<StringOrOrdinal> m_parts;
    std::string                  m_error_str;
    size_t                       m_nreplacements = 0;
    int                          m_max_placeholder_ordinal = 0;
};

inline bool Replacer::is_valid() const
{
    return m_error_str.empty();
}

inline std::string Replacer::error_str() const
{
    return m_error_str;
}

inline size_t Replacer::num_replacements() const
{
    return m_nreplacements;
}

inline int Replacer::max_placeholder_ordinal() const
{
    return m_max_placeholder_ordinal;
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

    // Should start with with "@{"
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
            if (*first == '\\')
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
