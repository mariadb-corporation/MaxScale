/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#if defined (__x86_64__)

#include "../multistmt_impl.hh"
#include "simd256.hh"
#include <maxbase/assert.h>
#include <maxbase/string.hh>
#include <functional>
#include <algorithm>
#include <limits>

namespace
{
const char IS_SPACE = 1 << 0;
const char IS_SEMICOLON = 1 << 1;
const char IS_QUOTE = 1 << 2;
const char IS_COMMENT = 1 << 3;
const char IS_ESCAPE = 1 << 4;

/** This LUT checks that a character can only have one classification
 *  which allows the bitmap to be used in a switch. Minimal class,
 *  but it would make sense to move it into maxbase. TODO.
 */
class LUT
{
public:
    LUT()
    {
        set(IS_SPACE, ::isspace);
        set(IS_SEMICOLON, [](uint8_t c) {
                return c == ';';
            });
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

    // Return the bits for the character.
    inline char bitmap(uint8_t c)
    {
        return m_table[c];
    }

private:
    void set(char bit, std::function<bool(uint8_t)> is_type)
    {
        for (int i = 0; i <= std::numeric_limits<uint8_t>::max(); i++)
        {
            if (is_type(i))
            {
                mxb_assert(m_table[i] == 0);
                m_table[i] |= bit;
            }
        }
    }

    std::array<char, 256> m_table = {};
};

static LUT lut;

using namespace maxsimd::simd256;

// Copy pasted from simd_canonical.
inline const char* find_matching_delimiter(Markers* pMarkers, char ch)
{
    while (!pMarkers->empty())
    {
        auto pMarker = pMarkers->back();
        if (*pMarker == ch)
        {
            // don't care if a quote is escaped with a double quote,
            // it will look like two quoted strings after each other.
            pMarkers->pop_back();
            return pMarker;
        }
        else if (*pMarker == '\\')
        {
            // pop if what we are looking for is escaped, or an escape is escaped.
            if (*++pMarker == ch || *pMarker == '\\')
            {
                if (pMarkers->size() > 1)       // branch here to avoid it outside
                {
                    pMarkers->pop_back();
                }
            }
        }

        pMarkers->pop_back();
    }

    return nullptr;
}
}

namespace maxsimd
{
namespace simd256
{
/** See maxsimd::simd256::get_canonical_impl() for a commented
 *  version of the same basic code. This is much simpler, though.
 *
 *  The algorithm is:
 *  1. find the first non-quoted, non-escaped, non-comment ';'
 *  2. If found and there are only spaces, semicolons and comments
 *     in the rest of the sql, it is NOT a multi-statement, else it is.
 */
bool is_multi_stmt_impl(const std::string& sql, std::vector<const char*>* pMarkers)
{
    // The characters that need to be classified.
    static const __m256i sql_ascii_bit_map = make_ascii_bitmap(R"(;"'`#-/\)");
    make_markers(sql, sql_ascii_bit_map, pMarkers);

    bool has_semicolons = false;
    for (const auto& ptr : *pMarkers)
    {
        if (*ptr == ';')
        {
            has_semicolons = true;
            break;
        }
    }

    if (!has_semicolons)
    {
        return false;
    }

    std::reverse(begin(*pMarkers), end(*pMarkers));

    const char* read_begin = sql.data();
    const char* read_ptr = read_begin;
    const char* read_end = read_begin + sql.length();

    if (!pMarkers->empty())
    {   // advance to the first marker
        read_ptr += pMarkers->back() - read_ptr;
    }

    bool is_multi = false;

    while (!pMarkers->empty())
    {
        auto pMarker = pMarkers->back();
        pMarkers->pop_back();

        while (read_ptr > pMarker)
        {
            if (pMarkers->empty())
            {
                goto break_out;
            }
            pMarker = pMarkers->back();
            pMarkers->pop_back();
        }

        read_ptr += pMarker - read_ptr;

        mxb_assert(read_ptr == pMarker);

        switch (lut.bitmap(*pMarker))
        {
        case IS_QUOTE:
            {
                auto tmp_ptr = find_matching_delimiter(pMarkers, *read_ptr);
                if (tmp_ptr == nullptr)
                {
                    goto break_out;
                }

                read_ptr = tmp_ptr + 1;
            }
            break;

        case IS_COMMENT:
            read_ptr = maxbase::consume_comment(read_ptr, read_end);
            break;

        case IS_ESCAPE:
            ++read_ptr;
            break;

        case IS_SEMICOLON:
            {
                // If the semicolon is followed by only space and comments, this
                // is not a multistatement. There is no longer a need to continue
                // the mainloop.
                ++read_ptr;
                is_multi = false;
                while (read_ptr < read_end)
                {
                    switch(lut.bitmap(*read_ptr))
                    {
                    case IS_ESCAPE:
                        ++read_ptr;
                        /* fallthrough */
                    case IS_SPACE:
                    case IS_SEMICOLON:
                        ++read_ptr;
                    break;

                    case IS_COMMENT:
                    {
                        auto ptr_before = read_ptr;
                        read_ptr = maxbase::consume_comment(read_ptr, read_end);
                        if (read_ptr == ptr_before)
                        {
                            is_multi = true;
                            goto break_out;
                        }
                    }
                    break;

                    default:
                        is_multi = true;
                        goto break_out;
                    }
                }
            }
            break;

        default:
            {
                mxb_assert(!true);
                is_multi = true;
                goto break_out;
            }
            break;
        }
    }

break_out:
    return is_multi;
}
}
}

#endif
