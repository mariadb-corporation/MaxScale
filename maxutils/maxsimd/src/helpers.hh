/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <string>

namespace helpers
{

/** Fast lookup similar to std::isalpha & co for
 *  select lookups needed below.
 */
class LUT
{
public:
    static constexpr const char IS_SPACE = 0b00000001;
    static constexpr const char IS_DIGIT = 0b00000010;
    static constexpr const char IS_ALPHA = 0b00000100;
    static constexpr const char IS_ALNUM = 0b00001000;
    static constexpr const char IS_XDIGIT = 0b00010000;
    static constexpr const char IS_QUOTE = 0b00100000;
    static constexpr const char IS_COMMENT = 0b01000000;

    LUT()
    {
        set(IS_SPACE, ::isspace);
        set(IS_DIGIT, ::isdigit);
        set(IS_ALPHA, ::isalpha);
        set(IS_ALNUM, ::isalnum);
        set(IS_XDIGIT, ::isxdigit);
        set(IS_QUOTE, [](uint8_t c) {
            return std::string("\"'`").find(c) != std::string::npos;
        });
        set(IS_COMMENT, [](uint8_t c) {
            return std::string("/#-").find(c) != std::string::npos;
        });
    }

    inline bool operator()(char bit, uint8_t c) const
    {
        return m_table[c] & bit;
    }

    template<char Type>
    bool is_type( uint8_t c) const
    {
         return m_table[c] & Type;
    }

private:
    void set(char bit, std::function<bool(uint8_t)> is_type)
    {
        for (int i = 0; i <= std::numeric_limits<uint8_t>::max(); i++)
        {
            if (is_type(i))
            {
                m_table[i] |= bit;
            }
        }
    }

    std::array<char, 256> m_table = {};
};

template<class Iter>
const char* find_matching_delimiter(Iter& it, Iter& end, const char* read_begin, char ch)
{
    while (it != end)
    {
        auto pMarker = read_begin + *it;
        if (*pMarker == ch)
        {
            // See if a quote is escaped with a double quote. If so, skip over both of them and continue
            // looking for a delimiter.
            if (auto next_it = std::next(it); next_it != end)
            {
                auto pNext = read_begin + *next_it;

                if (pMarker + 1 == pNext && *pNext == ch)
                {
                    it = std::next(next_it);
                    continue;
                }
            }

            // End of the quote
            ++it;
            return pMarker;
        }
        else if (*pMarker == '\\')
        {
            // pop if what we are looking for is escaped, or an escape is escaped.
            // This looks suspicious but it works because the string which we're parsing is null-terminated.
            // Even if the string ends in a backslash, the lookahead will always read initialized memory.
            if (*++pMarker == ch || *pMarker == '\\')
            {
                if (std::next(it) != end)       // branch here to avoid it outside
                {
                    ++it;
                }
            }
        }

        ++it;
    }

    return nullptr;
}
}
