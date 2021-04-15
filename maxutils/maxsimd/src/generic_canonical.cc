/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/canonical.hh>
#include <maxbase/assert.h>

#include <string>
#include <functional>
#include <algorithm>
#include <vector>

static inline bool is_next(uint8_t* it, uint8_t* end, const std::string& str)
{
    mxb_assert(it != end);
    for (auto s_it = str.begin(); s_it != str.end(); ++s_it, ++it)
    {
        if (it == end || *it != *s_it)
        {
            return false;
        }
    }

    return true;
}

const char IS_SPACE = 0b00000001;
const char IS_DIGIT = 0b00000010;
const char IS_ALPHA = 0b00000100;
const char IS_ALNUM = 0b00001000;
const char IS_XDIGIT = 0b00010000;
const char IS_SPECIAL = 0b00100000;

class LUT
{
public:
    LUT()
    {
        set(IS_SPACE, ::isspace);
        set(IS_DIGIT, ::isdigit);
        set(IS_ALPHA, ::isalpha);
        set(IS_ALNUM, ::isalnum);
        set(IS_XDIGIT, ::isxdigit);
        set(IS_SPECIAL, [](uint8_t c) {
                return isdigit(c) || std::string("\"'`#-/\\").find(
                    c) != std::string::npos;
            });
    }

    inline bool operator()(char bit, uint8_t c) const
    {
        return m_table[c] & bit;
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

static LUT lut;

static inline std::pair<bool, uint8_t*> probe_number(uint8_t* it, uint8_t* end)
{
    mxb_assert(it != end);
    mxb_assert(lut(IS_DIGIT, *it));
    std::pair<bool, uint8_t*> rval = std::make_pair(true, it);
    bool is_hex = *it == '0';
    bool allow_hex = false;

    // Skip the first character, we know it's a number
    it++;

    while (it != end)
    {
        if (lut(IS_DIGIT, *it) || (allow_hex && lut(IS_XDIGIT, *it)))
        {
            // Digit or hex-digit, skip it
        }
        else
        {
            // Non-digit character

            if (is_hex && (*it == 'x' || *it == 'X'))
            {
                /** A hexadecimal literal, mark that we've seen the `x` so that
                 * if another one is seen, it is treated as a normal character */
                is_hex = false;
                allow_hex = true;
            }
            else if (*it == 'e')
            {
                // Possible scientific notation number
                auto next_it = it + 1;

                if (next_it == end || (!lut(IS_DIGIT, *next_it) && *next_it != '-'))
                {
                    rval.first = false;
                    break;
                }

                // Skip over the minus if we have one
                if (*next_it == '-')
                {
                    it++;
                }
            }
            else if (*it == '.')
            {
                // Possible decimal number
                auto next_it = it + 1;

                if (next_it != end && !lut(IS_DIGIT, *next_it))
                {
                    /** The fractional part of a decimal is optional in MariaDB. */
                    rval.second = it;
                    break;
                }
                mxb_assert(lut(IS_DIGIT, *next_it));
            }
            else
            {
                // If we have a non-text character, we treat it as a number
                rval.first = !lut(IS_ALPHA, *it);
                break;
            }
        }

        // Store the previous iterator
        rval.second = it;
        it++;
    }

    return rval;
}

static inline uint8_t* find_char(uint8_t* it, uint8_t* end, char c)
{
    for (; it != end; ++it)
    {
        if (*it == '\\')
        {
            if (++it == end)
            {
                break;
            }
        }
        else if (*it == c)
        {
            return it;
        }
    }

    return it;
}

namespace maxscale
{

#define likely(x)   __builtin_expect (!!(x), 1)
#define unlikely(x) __builtin_expect (!!(x), 0)

std::string* get_canonical(std::string* pSql)
{
    auto& sql = *pSql;

    uint8_t* it = (uint8_t*) &*sql.begin();
    uint8_t* end = (uint8_t*) &*sql.end();

    auto it_out = (uint8_t*) &*sql.begin();
    uint8_t* it_out_begin = it_out;
    bool was_converted = false;

    for (; it != end; ++it)
    {
        bool did_conversion = false;

        if (likely(!lut(IS_SPECIAL, *it)))
        {
            // Normal character, no special handling required
            *it_out++ = *it;
        }
        else if (lut(IS_DIGIT, *it)
                 && (it_out != it_out_begin && !lut(IS_ALNUM, *(it_out - 1)) && *(it_out - 1) != '_'))
        {
            auto num_end = probe_number(it, end);

            if (num_end.first)
            {
                if (!was_converted && *(it_out - 1) == '-')
                {
                    // Remove the sign
                    --it_out;
                }
                *it_out++ = '?';
                it = num_end.second;
                did_conversion = true;
            }
        }
        else if (*it == '\'' || *it == '"')
        {
            char c = *it;
            if ((it = find_char(it + 1, end, c)) == end)
            {
                break;
            }
            *it_out++ = '?';
        }
        else if (*it == '\\')
        {
            // Jump over any escaped values
            *it_out++ = *it++;

            if (it != end)
            {
                *it_out++ = *it;
            }
            else
            {
                // Query that ends with a backslash
                break;
            }
        }
        else if (*it == '/' && is_next(it, end, "/*"))
        {
            auto comment_start = std::next(it, 2);
            if (comment_start == end)
            {
                break;
            }
            else if (*comment_start != '!' && *comment_start != 'M')
            {
                // Non-executable comment
                while (it != end)
                {
                    if (is_next(it, end, "*/"))
                    {
                        // Comment end marker, return to normal parsing
                        ++it;
                        break;
                    }
                    ++it;
                }

                if (it == end)
                {
                    break;
                }
            }
            else
            {
                // Executable comment, treat it as normal SQL
                *it_out++ = *it;
            }
        }
        else if (*it == '#' || (*it == '-' && is_next(it, end, "-- ")))
        {
            // End-of-line comment, jump to the next line if one exists
            while (it != end)
            {
                if (*it == '\n')
                {
                    break;
                }
                else if (*it == '\r')
                {
                    if ((is_next(it, end, "\r\n")))
                    {
                        ++it;
                    }
                    break;
                }

                ++it;
            }

            if (it == end)
            {
                break;
            }
        }
        else if (*it == '`')
        {
            auto start = it;
            if ((it = find_char(it + 1, end, '`')) == end)
            {
                break;
            }
            std::copy(start, it, &*it_out);
            it_out += it - start;
            *it_out++ = '`';
        }
        else
        {
            *it_out++ = *it;
        }

        was_converted = did_conversion;

        mxb_assert(it != end);
    }

    // Remove trailing whitespace
    while (lut(IS_SPACE, *(it_out - 1)))
    {
        --it_out;
    }

    // Shrink the buffer so that the internal bookkeeping of std::string remains up to date
    sql.resize(it_out - it_out_begin);

    return pSql;
}
}
