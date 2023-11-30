/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#if defined (__x86_64__)

#include "../canonical_impl.hh"
#include "simd256.hh"
#include <maxbase/assert.hh>
#include <maxbase/string.hh>

#include <array>
#include <string>
#include <functional>
#include <algorithm>
#include <vector>
#include <limits>

namespace maxsimd
{
namespace simd256
{
namespace
{


// The characters that need to be classified. Digits are handled
// separately.
static const auto s_sql_ascii_bit_map = make_ascii_bitmap(R"("'`/#-\)");

// Characters that can start (and continue) an identifier.
static const auto s_ident_begin_bit_map =
    make_ascii_bitmap(R"($_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ)");

MXS_AVX2_FUNC inline __m256i sql_ascii_bit_map()
{
    return _mm256_loadu_si256((__m256i*) s_sql_ascii_bit_map.data());
}

MXS_AVX2_FUNC inline __m256i ident_begin_bit_map()
{
    return _mm256_loadu_si256((__m256i*) s_ident_begin_bit_map.data());
}

MXS_AVX2_FUNC inline __m256i small_zeros()
{
    const __m256i small_zeros = _mm256_set1_epi8('0' - 1);
    return small_zeros;
}
MXS_AVX2_FUNC inline __m256i large_nines()
{
    const __m256i large_nines = _mm256_set1_epi8('9' + 1);
    return large_nines;
}

/** This is a copy of make_markers(), but that does extra work to
 *  speed up get_canonical_impl().
 *  Where there is a sequence of digits, only add a marker to the
 *  leading digit. If the char preceding that digit is '_' or alpha,
 *  discard the digit as it cannot be a number.
 *  The shift language is how the chars naturally look, so a shift
 *  right of the chars is a shift left of the bitmaps.
 */
MXS_AVX2_FUNC inline void make_markers_sql_optimized(const std::string& sql, Markers* pMarkers)
{
    const char* pBegin = sql.data();
    const char* pSource = pBegin;
    const char* pEnd = pBegin + sql.length();

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
            chunk = _mm256_loadu_si256((const __m256i*)(pSource));
        }

        uint32_t ascii_bitmask = _mm256_movemask_epi8(classify_ascii(sql_ascii_bit_map(), chunk));
        uint32_t ident_bitmask = _mm256_movemask_epi8(classify_ascii(ident_begin_bit_map(), chunk));

        // Make a bitmap where a sequence of digits is replaced with only
        // the first, leading digit.
        const __m256i greater_eq_0 = _mm256_cmpgt_epi8(chunk, small_zeros());
        const __m256i less_eq_9 = _mm256_cmpgt_epi8(large_nines(), chunk);
        const __m256i all_digits = _mm256_and_si256(greater_eq_0, less_eq_9);

        // Only 32 bit bitmasks after this point
        const uint32_t all_digits_bitmask = _mm256_movemask_epi8(all_digits);
        const bool rightmost_is_ident_char = (ident_bitmask & 0x8000'0000)
                | (all_digits_bitmask & 0x8000'0000);

        const uint32_t left_shifted_bitmask = all_digits_bitmask << 1;
        const uint32_t xored_bitmask = all_digits_bitmask ^ left_shifted_bitmask;
        uint32_t leading_digit_bitmask = all_digits_bitmask & xored_bitmask;

        // Register boundary check.
        // If the previous rightmost char was an identifier char,
        // then if the current leftmost char is a digit, zero the marker out.
        uint32_t ident_shift_left = ident_bitmask << 1;
        uint32_t not_a_number = leading_digit_bitmask & ident_shift_left;
        leading_digit_bitmask ^= not_a_number;
        leading_digit_bitmask &= ~uint32_t(previous_rightmost_is_ident_char);

        previous_rightmost_is_ident_char = rightmost_is_ident_char;

        uint32_t bitmask = ascii_bitmask | leading_digit_bitmask;

        // The number of markers that will be added is the number of set bits in the bitmask. Allocating space
        // and then using a pointer to set the values saves us the capacity check that would otherwise be done
        // in std::vector::push_back().
        size_t added = __builtin_popcount(bitmask);
        auto old_size = pMarkers->size();
        pMarkers->resize(old_size + added);
        auto ptr = pMarkers->data() + old_size;

        for (size_t bits = 0; bits < added; bits++)
        {
            auto i = __builtin_ctz(bitmask);
            bitmask = bitmask & (bitmask - 1);      // clear the lowest bit
            *ptr++ = (index_offset + i);
        }

        index_offset += SIMD_BYTES;
    }
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

MXS_AVX2_FUNC inline const char* probe_number(const char* it, const char* const pEnd)
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
            else if (*it == 'e' || *it == 'E')
            {
                // Possible scientific notation number
                auto next_it = it + 1;

                if (next_it == pEnd || !(*next_it != '-' || *next_it != '+' || lut(IS_DIGIT, *next_it)))
                {
                    rval = nullptr;
                    break;
                }

                // Skip over the sign if there is one
                if (*next_it == '-' || *next_it == '+')
                {
                    it = next_it;
                }

                // There must be at least one digit
                if (++it == pEnd || !lut(IS_DIGIT, *it))
                {
                    rval = nullptr;
                    break;
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
MXS_AVX2_FUNC std::string* get_canonical_impl(std::string* pSql, Markers* pMarkers)
{
    /* The call &*pSql->begin() ensures that a non-confirming
     * std::string will copy the data (COW, CentOS7)
     */
    const char* read_begin = &*pSql->begin();
    const char* read_ptr = read_begin;
    const char* read_end = read_begin + pSql->length();

    const char* write_begin = read_begin;
    auto write_ptr = const_cast<char*>(write_begin);
    bool was_converted = false;     // differentiates between a negative number and subtraction

    make_markers_sql_optimized(*pSql, pMarkers);
    auto it = pMarkers->begin();
    auto end = pMarkers->end();

    if (it != end)
    {   // advance to the first marker
        auto len = *it;
        read_ptr += len;
        write_ptr += len;
    }

    while (it != end)
    {
        auto pMarker = read_begin + *it++;

        // The code further down can read passed pmarkers-> For example, a comment
        // can contain multiple markers, but the code that handles comments reads
        // to the end of the comment.
        while (read_ptr > pMarker)
        {
            if (it == end)
            {
                goto break_out;
            }
            pMarker = read_begin + *it++;
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
            auto tmp_ptr = find_matching_delimiter(it, end, read_begin, *read_ptr);
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
                *write_ptr++ = '?';
                read_ptr = num_end;
            }
        }
        else if (lut(IS_COMMENT, *pMarker))
        {
            auto before = read_ptr;
            read_ptr = maxbase::consume_comment(read_ptr, read_end, true);
            if (read_ptr - before == 4) // replace comment "/**/" with a space
            {
                *write_ptr++ = ' ';
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
    }

break_out:

    if (read_ptr < read_end)
    {
        auto len = read_end - read_ptr;
        std::memmove(write_ptr, read_ptr, len);
        write_ptr += len;
    }

    pSql->resize(write_ptr - write_begin);

    return pSql;
}
}
}
#endif
