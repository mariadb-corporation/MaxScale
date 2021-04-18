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

#pragma once

#include <maxbase/ccdefs.hh>

#include <string>
#include <cstring>
#include <vector>
#include <immintrin.h>

namespace maxsimd::simd256
{

const int SIMD_BYTES = 32;

/**
 * @brief  to_string
 *
 * @param  reg, an __m256i
 * @return a string as if the register is 32 characters
 */
std::string to_string(__m256i reg);

/**
 * @brief  to_hex_string
 *
 * @param  reg, an __m256i
 * @return Space separated hex numbers for each byte of the register
 */
std::string to_hex_string(__m256i reg);

/**
 * @brief  make_ascii_bitmap - for classifying ASCII characters
 *
 *         A 16*8 bitmap defines chars to be classified.
 *         A bit is set for each character that needs classification.
 *         E.g. 'C' = 0b01000011, the low nibble is 0b0011, or decimal 3,
 *         which is the index (the 4th byte), in which the bit 1 << high_nibble
 *         is set. The high nibble is 0b0100 so the 5th bit is set. Look
 *         for the bitmask_lookup_const comment how a character is classified.
 *
 *         In AVX2 there are actually two independent 128bit lanes,
 *         so the __m256i has two identical 16*8 bitmaps.
 *
 * @param  chars - ASCII characters to classify. The msb should not be set,
 *                 and '\0' is not allowed. Obviously the characters should
 *                 be printable characters in the normal case.
 * @return A bitmap for classification of ASCII chars.
 */
__m256i make_ascii_bitmap(const std::string& chars);

/**
 * This is a static lookup table that when indexed with the high nibble
 * gives the bit position corresponding to that nibble.
 *
 * E.g. Given the character 'C' = 0b01000011, the low nibble is used to
 *      get the classification byte from the bitmap created above which
 *      is 0b00010000 (if 'C' is the only char in the bitmap, in any case,
 *      consider the 0's don't cares)
 *      Then the high nibble is used to index into the static lookup table
 *      below. The high nibble is 4, so we get 16 = 0b00010000.
 *      When the two values are anded together we get a non-zero value.
 *
 *  There are 4 copies of the table in the __m256i, again for
 *  architectural reasons. This table also works for 8-bit chars.
 */
static const __m256i bitmask_lookup_const = _mm256_setr_epi8(
    1, 2, 4, 8, 16, 32, 64, 128,
    1, 2, 4, 8, 16, 32, 64, 128,
    1, 2, 4, 8, 16, 32, 64, 128,
    1, 2, 4, 8, 16, 32, 64, 128
    );

/**
 * @brief  classify_ascii Identify classified characters in an __m256i. General
 *                        algo to do ASCII classification.
 *
 * @param  ascii_bitmap   Chars to classify, use make_ascii_bitmap() to create.
 * @param  input          __m256i, the 32 characters to be classified.
 * @return __m256i        The final mask. The high bit is set in bytes that are
 *                        classified.
 *                        Use _mm256_movemask_epi8(ret_value) to get an int32_t
 *                        bitmask where a bit is set corresponding to a classified
 *                        character in the input.
 */
inline __m256i classify_ascii(__m256i ascii_bitmap, __m256i input)
{
    // ascii_classification[i] = ascii_bitmap[input[i] & 0x1111)]
    const __m256i ascii_classification = _mm256_shuffle_epi8(ascii_bitmap, input);

    // shift high nibbles into place (into low nibble position for shuffle)
    const __m256i high_nibbles = _mm256_and_si256(_mm256_srli_epi16(input, 4), _mm256_set1_epi8(0x0f));

    // bits[i] = bitmask_lookup256[input[i]>>4],
    const __m256i bits = _mm256_shuffle_epi8(bitmask_lookup_const, high_nibbles);

    // classified[i] = ascii_classification[i] & bits[i],
    const __m256i classified = _mm256_and_si256(ascii_classification, bits);

    // To get a bitmask out, the msb must be set: set msb if non-zero
    __m256i mask = _mm256_cmpgt_epi8(classified, _mm256_set1_epi8(0x0));

    // 'or' the high bit back if it was set (classified bits > 0)
    // Is there a single instruction to set msb if byte!=0?
    //  _mm256_cmpneq_epi8_mask is AVX512VL + AVX512BW)
    mask = _mm256_or_si256(mask, classified);

    return mask;
}

using Markers = std::vector<const char*>;

/**
 * @brief  make_markers - create a vector of ptrs, pointing into the
 *         argument string for every classified char.
 *
 * @param  str      string
 * @return Pointers into argument string for every classified character.
 */
inline Markers make_markers(const std::string& str, __m256i ascii_bitmap)
{
    const char* pBegin = &*str.begin();
    const char* pSource = pBegin;
    const char* pEnd = &*str.end();

    std::vector<const char*> markers;
    // safe size guess, no empirical data, probably much lower.
    markers.reserve(str.size() / 10);
    size_t index_offset = 0;

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

        auto bitmask = _mm256_movemask_epi8(classify_ascii(ascii_bitmap, chunk));

        while (bitmask)
        {
            auto i = __builtin_ctz(bitmask);
            bitmask = bitmask & (bitmask - 1);      // clear the lowest bit
            markers.push_back(pBegin + index_offset + i);
        }

        index_offset += SIMD_BYTES;
    }

    return markers;
}
}
