/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#if defined (__x86_64__)

#include "../multistmt_impl.hh"
#include "../helpers.hh"
#include "simd256.hh"
#include <maxbase/assert.hh>
#include <maxbase/string.hh>
#include <array>
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

static const auto s_sql_ascii_bit_map = maxsimd::simd256::make_ascii_bitmap(R"(;"'`#-/\)");

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
        set(IS_ESCAPE, [](uint8_t c) {
            return c == '\\';
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
MXS_AVX2_FUNC bool is_multi_stmt_impl(std::string_view sql, maxsimd::Markers* pMarkers)
{
    // The characters that need to be classified.
    const auto sql_ascii_bit_map = _mm256_loadu_si256((__m256i*) s_sql_ascii_bit_map.data());
    make_markers(sql, sql_ascii_bit_map, pMarkers);

    bool has_semicolons = false;
    for (const uint32_t offset : *pMarkers)
    {
        if (sql[offset] == ';')
        {
            has_semicolons = true;
            break;
        }
    }

    if (!has_semicolons)
    {
        return false;
    }

    const char* read_begin = sql.data();
    const char* read_ptr = read_begin;
    const char* read_end = read_begin + sql.length();

    auto it = pMarkers->begin();
    auto end = pMarkers->end();

    if (it != end)
    {   // advance to the first marker
        read_ptr += *it;
    }

    bool is_multi = false;

    while (it != end)
    {
        auto pMarker = read_begin + *it++;

        while (read_ptr > pMarker)
        {
            if (it != end)
            {
                goto break_out;
            }
            pMarker = read_begin + *it++;
        }

        read_ptr += pMarker - read_ptr;

        mxb_assert(read_ptr == pMarker);

        switch (lut.bitmap(*pMarker))
        {
        case IS_QUOTE:
            {
                auto tmp_ptr = helpers::find_matching_delimiter(it, end, read_begin, *read_ptr);
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
                        [[fallthrough]];

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
