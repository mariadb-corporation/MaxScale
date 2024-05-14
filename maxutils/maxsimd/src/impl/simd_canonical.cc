/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
}

/** This is a copy of make_markers(), but that does extra work to
 *  speed up get_canonical_impl().
 *  Where there is a sequence of digits, only add a marker to the
 *  leading digit. If the char preceding that digit is '_' or alpha,
 *  discard the digit as it cannot be a number.
 *  The shift language is how the chars naturally look, so a shift
 *  right of the chars is a shift left of the bitmaps.
 */
MXS_AVX2_FUNC void make_markers(const std::string& sql, Markers* pMarkers)
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
}
}
#endif
