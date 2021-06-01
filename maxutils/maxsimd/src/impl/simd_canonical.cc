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

#include "canonical_impl.hh"
#include <maxsimd/simd256.hh>
#include <maxbase/assert.h>

#include <string>
#include <functional>
#include <algorithm>
#include <vector>
#include <limits>

namespace maxsimd::simd256
{
namespace
{


// The characters that need to be classified. Digits are handled
// separately.
static const __m256i sql_ascii_bit_map = make_ascii_bitmap(R"("'`/#-\)");

// Characters that can start (and continue) an identifier.
static const __m256i ident_begin_bit_map =
    make_ascii_bitmap(R"(_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ)");

const __m256i small_zeros = _mm256_set1_epi8('0' - 1);
const __m256i large_nines = _mm256_set1_epi8('9' + 1);

/** This is a copy of make_markers(), but that does extra work to
 *  speed up get_canonical_impl().
 *  Where there is a sequence of digits, only add a marker to the
 *  leading digit. If the char preceding that digit is '_' or alpha,
 *  discard the digit as it cannot be a number.
 *  The shift language is how the chars naturally look, so a shift
 *  right of the chars is a shift left of the bitmaps.
 */
inline Markers* make_markers_sql_optimized(const std::string& sql, Markers* pMarkers)
{
    const char* pBegin = &*sql.begin();
    const char* pSource = pBegin;
    const char* pEnd = &*sql.end();

    pMarkers->clear();
    size_t index_offset = 0;

    // By setting this initially to true there can be no digit marker
    // for the first char, so the parsing does not need to check for it.
    bool previous_rightmost_is_ident_char = true;

    for (; pSource < pEnd; pSource += SIMD_BYTES)
    {
        __m256i chunk;

        if (pEnd - pSource < SIMD_BYTES)
        {
            chunk = _mm256_set1_epi8(0);
            std::memcpy((void*)&chunk, pSource, pEnd - pSource);
        }
        else
        {
            chunk = _mm256_loadu_si256 ((const __m256i*)(pSource));
        }

        auto ascii_bitmask = _mm256_movemask_epi8(classify_ascii(sql_ascii_bit_map, chunk));
        auto ident_bitmask = _mm256_movemask_epi8(classify_ascii(ident_begin_bit_map, chunk));

        // Make a bitmap where a sequence of digits is replaced with only
        // the first, leading digit.
        const __m256i greater_eq_0 = _mm256_cmpgt_epi8(chunk, small_zeros);
        const __m256i less_eq_9 = _mm256_cmpgt_epi8(large_nines, chunk);
        const __m256i all_digits = _mm256_and_si256(greater_eq_0, less_eq_9);

        auto pDigs = reinterpret_cast<const unsigned char*>(&all_digits);
        bool rightmost_is_ident_char = pDigs[SIMD_BYTES - 1] || (ident_bitmask & 0x80000000);

        const __m256i rshifted = _mm256_slli_si256(all_digits, 1);
        const __m256i xored = _mm256_xor_si256(all_digits, rshifted);
        const __m256i leading_digits = _mm256_and_si256(all_digits, xored);

        auto digit_bitmask = _mm256_movemask_epi8(leading_digits);

        // If a leading digit is preceded by a char that can
        // start or continue an identifier, drop the digit.
        auto ident_shftr = ident_bitmask << 1;
        auto not_a_number = digit_bitmask & ident_shftr;
        digit_bitmask ^= not_a_number;

        // Register boundary check.
        // If the previous rightmost char was an identifier char,
        // then if the current leftmost char is a digit, zero it out.
        digit_bitmask &= ~int32_t(previous_rightmost_is_ident_char);

        previous_rightmost_is_ident_char = rightmost_is_ident_char;

        auto bitmask = ascii_bitmask | digit_bitmask;

        while (bitmask)
        {
            auto i = __builtin_ctz(bitmask);
            bitmask = bitmask & (bitmask - 1);      // clear the lowest bit
            pMarkers->push_back(pBegin + index_offset + i);
        }

        index_offset += SIMD_BYTES;
    }

    return pMarkers;
}

// A make_markers version optimized for strings

const char IS_SPACE = 0b00000001;
const char IS_DIGIT = 0b00000010;
const char IS_ALPHA = 0b00000100;
const char IS_ALNUM = 0b00001000;
const char IS_XDIGIT = 0b00010000;
const char IS_QUOTE = 0b00100000;
const char IS_COMMENT = 0b01000000;

/** Fast lookup similar to std::isalpha & co for
 *  select lookups needed below.
 */
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

inline const char* find_matching_delimiter(Markers* pMarkers, char ch)
{
    while (!pMarkers->empty())
    {
        auto pMarker = pMarkers->back();
        if (*pMarker == ch)
        {
            // don't care if a quote is escaped with a double quote,
            // two questions marks instead of one.
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

inline const char* probe_number(const char* it, const char* const pEnd)
{
    bool is_hex = *it == '0';
    bool allow_hex = false;

    // Skip the first character, we know it's a number
    ++it;
    const char* rval = it;

    while (it != pEnd)
    {
        if (lut(IS_DIGIT, *it) || (allow_hex && lut(IS_XDIGIT, *it)))
        {   // Digit or hex-digit, skip it
        }
        else
        {   // Non-digit character
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

                if (next_it == pEnd || (!lut(IS_DIGIT, *next_it) && *next_it != '-'))
                {
                    rval = nullptr;
                    break;
                }

                // Skip over the dash if we have one
                if (*next_it == '-')
                {
                    it++;
                }
            }
            else if (*it == '.')
            {
                // Possible decimal number
                auto next_it = it + 1;

                if (next_it != pEnd && !lut(IS_DIGIT, *next_it))
                {
                    /** No number after the period, not a decimal number.
                     * The fractional part of the number is optional in MariaDB. */
                    break;
                }
            }
            else
            {
                // If we have a non-text character, we treat it as a number
                rval = lut(IS_ALPHA, *it) ? nullptr : it;
                break;
            }
        }

        rval = it;
        ++it;
    }

    // If we got to the end, it's a number, else whatever rval was set to
    return it == pEnd ?  pEnd : rval;
}
}

/** In-place canonical.
 *  Note that where the sql is invalid the output should also be invalid so it cannot
 *  match a valid canonical TODO make sure.
 */
std::string* get_canonical_impl(std::string* pSql, Markers* pMarkers)
{
    auto& sql = *pSql;
    auto markers = make_markers_sql_optimized(sql, pMarkers);

    std::reverse(begin(*pMarkers), end(*pMarkers));     // for pop_back(), an index would likely be better.

    const char* read_begin = &*sql.begin();
    const char* read_ptr = read_begin;
    const char* read_end = &*sql.end();

    const char* write_begin = read_begin;
    auto write_ptr = &*sql.begin();
    bool was_converted = false;     // differentiates between a negative number and subtraction

    if (!pMarkers->empty())
    {   // advance to the first marker
        auto len = pMarkers->back() - read_ptr;
        read_ptr += len;
        write_ptr += len;
    }

    while (!pMarkers->empty())
    {
        bool did_conversion = false;
        auto pMarker = pMarkers->back();
        pMarkers->pop_back();

        // The code further down can read passed pmarkers-> For example, a comment
        // can contain multiple markers, but the code that handles comments reads
        // to the end of the comment.
        while (read_ptr > pMarker)
        {
            if (pMarkers->empty())
            {
                goto break_out;
            }
            pMarker = pMarkers->back();
            pMarkers->pop_back();
        }

        // With "select 1 from T where id=42", the first marker would
        // point to the '1', and was handled above. It also happens when
        // a marker had to be checked, like the '1', after which
        // " from T where id=" is memmoved.
        if (read_ptr < pMarker)
        {
            auto len = pMarker - read_ptr;
            std::memmove(write_ptr, read_ptr, len);
            read_ptr += len;
            write_ptr += len;
        }

        mxb_assert(read_ptr == pMarker);

        if (lut(IS_QUOTE, *pMarker))
        {
            auto tmp_ptr = find_matching_delimiter(markers, *read_ptr);
            if (tmp_ptr == nullptr)
            {
                // Invalid SQL, copy the the rest to make canonical invalid.
                goto break_out;
            }

            read_ptr = tmp_ptr + 1;

            if (*pMarker == '`')
            {   // copy verbatim
                auto len = read_ptr - pMarker;
                std::memmove(write_ptr, pMarker, len);
                write_ptr += len;
            }
            else
            {
                *write_ptr++ = '?';
            }
        }
        else if (lut(IS_DIGIT, *pMarker))
        {
            auto num_end = probe_number(read_ptr, read_end);

            if (num_end)
            {
                if (!was_converted && *(write_ptr - 1) == '-')
                {
                    // Remove the sign
                    --write_ptr;
                }
                *write_ptr++ = '?';
                read_ptr = num_end;
                did_conversion = true;
            }
        }
        else if (lut(IS_COMMENT, *pMarker))
        {
            // These hard to read conditionals are what one pays for branchless code. Unfortunately
            // -1 is a popular number, so this code is hit before the above IS_DIGIT is.
            bool end_of_line_comment = *read_ptr == '#'
                || (*read_ptr == '-' && read_ptr + 1 != read_end && *(read_ptr + 1) == '-'
                    && read_ptr + 2 != read_end && *(read_ptr + 2) == ' ');
            bool regular_comment = *read_ptr == '/' && read_ptr + 1 != read_end && *(read_ptr + 1) == '*';

            if (end_of_line_comment)
            {
                while (++read_ptr != read_end)
                {
                    if (*read_ptr == '\n')
                    {
                        break;
                    }
                    else if (*read_ptr == '\r' && ++read_ptr != read_end && *read_ptr == '\n')
                    {
                        ++read_ptr;
                        break;
                    }
                }
            }
            else if (regular_comment)
            {
                ++read_ptr;
                if (++read_ptr == read_end)
                {
                    break;
                }
                else if (*read_ptr != '!' && *read_ptr != 'M')
                {
                    while (++read_ptr != read_end)
                    {
                        if (*read_ptr == '*' && read_ptr + 1 != read_end && *++read_ptr == '/')
                        {
                            // end of comment
                            ++read_ptr;
                            break;
                        }
                    }
                }
                else
                {
                    read_ptr -= 2;
                    // Executable comment, treat it as normal SQL
                    *write_ptr++ = *read_ptr++;
                }
            }
            else
            {
                // pass, not a comment, memmove will handle it
            }
        }
        else if (*pMarker == '\\')
        {
            // pass, memmove will handle it
        }
        else
        {
            mxb_assert(!true);
        }

        was_converted = did_conversion;
    }

break_out:

    if (read_ptr < read_end)
    {
        auto len = read_end - read_ptr;
        std::memmove(write_ptr, read_ptr, len);
        write_ptr += len;
    }

    sql.resize(write_ptr - write_begin);

    return pSql;
}
}
