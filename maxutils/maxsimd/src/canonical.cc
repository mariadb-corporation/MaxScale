/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxsimd/canonical.hh>
#include <maxbase/assert.hh>
#include <maxbase/cpuinfo.hh>
#include <maxbase/string.hh>
#include "canonical_impl.hh"
#include "helpers.hh"

#include <numeric>
#include <string>

namespace maxsimd
{

namespace
{
const maxbase::CpuInfo& cpu_info {maxbase::CpuInfo::instance()};

static helpers::LUT lut;

inline bool digit_or_dot(char c)
{
    return (c >= '0' && c <= '9') || c == '.';
}

inline const char* probe_number(const char* it, const char* const pEnd)
{
    bool is_hex = *it == '0';
    bool allow_hex = false;

    // Skip the first character, we know it's a number
    ++it;

    // Skip all digits and dots. This covers integer and decimals numbers and only the special cases like hex
    // numbers and scientific notation needs to be handled.
    while (it != pEnd && digit_or_dot(*it))
    {
        ++it;
    }

    const char* rval = it;

    while (it != pEnd)
    {
        if (lut(lut.IS_DIGIT, *it) || (allow_hex && lut(lut.IS_XDIGIT, *it)))
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

                if (next_it == pEnd || !(*next_it != '-' || *next_it != '+' || lut(lut.IS_DIGIT, *next_it)))
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
                if (++it == pEnd || !lut(lut.IS_DIGIT, *it))
                {
                    rval = nullptr;
                    break;
                }
            }
            else if (*it == '.')
            {
                // Possible decimal number
                auto next_it = it + 1;

                if (next_it != pEnd && !lut(lut.IS_DIGIT, *next_it))
                {
                    /** No number after the period, not a decimal number.
                     * The fractional part of the number is optional in MariaDB. */
                    break;
                }
            }
            else
            {
                // If we have a non-text character, we treat it as a number
                rval = lut(lut.IS_ALPHA, *it) ? nullptr : it;
                break;
            }
        }

        rval = it;
        ++it;
    }

    // If we got to the end, it's a number, else whatever rval was set to
    return it == pEnd ?  pEnd : rval;
}

template<class ArgParser>
constexpr bool not_nullptr()
{
    return !std::is_same_v<ArgParser, nullptr_t>;
}

/** In-place canonical.
 *  Note that where the sql is invalid the output should also be invalid so it cannot
 *  match a valid canonical TODO make sure.
 */
template<auto make_markers, class ArgParser = std::nullptr_t>
std::string* process_markers(std::string* pSql, maxsimd::Markers* pMarkers,
                             [[maybe_unused]] ArgParser arg_parser = {})
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

    make_markers(*pSql, pMarkers);
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
            memmove(write_ptr, read_ptr, len);
            read_ptr += len;
            write_ptr += len;
        }

        mxb_assert(read_ptr == pMarker);

        if (lut(lut.IS_QUOTE, *pMarker))
        {
            auto tmp_ptr = helpers::find_matching_delimiter(it, end, read_begin, *read_ptr);
            if (tmp_ptr == nullptr)
            {
                // Invalid SQL, copy the the rest to make canonical invalid.
                goto break_out;
            }

            read_ptr = tmp_ptr + 1;

            if (*pMarker == '`')
            {   // copy verbatim
                auto len = read_ptr - pMarker;
                memmove(write_ptr, pMarker, len);
                write_ptr += len;
            }
            else
            {
                if constexpr (not_nullptr<ArgParser>())
                {
                    uint32_t pos = write_ptr - write_begin;
                    auto len = read_ptr - pMarker;
                    arg_parser(CanonicalArgument {pos, std::string(pMarker, len)});
                }

                *write_ptr++ = '?';
            }
        }
        else if (lut(lut.IS_DIGIT, *pMarker))
        {
            auto num_end = probe_number(read_ptr, read_end);

            if (num_end)
            {
                if constexpr (not_nullptr<ArgParser>())
                {
                    uint32_t pos = write_ptr - write_begin;
                    auto len = num_end - pMarker;
                    arg_parser(CanonicalArgument {pos, std::string(pMarker, len)});
                }

                *write_ptr++ = '?';
                read_ptr = num_end;
            }
        }
        else if (lut(lut.IS_COMMENT, *pMarker))
        {
            auto before = read_ptr;
            read_ptr = maxbase::consume_comment(read_ptr, read_end, true);
            // Replace comment "/**/" with a space. Comparing to the actual value avoids a corner case where
            // the `-- a` comment is converted into a space and `-- aa` is simply removed.
            if (read_ptr - before == 4 && memcmp(before, "/**/", 4) == 0)
            {
                *write_ptr++ = ' ';
            }
        }
        else if (*pMarker == '\\')
        {
            if (it != end && read_begin + *it == pMarker + 1)
            {
                // Ignore the next marker as it's escaped by this backslash.
                ++it;
            }
            else
            {
                // pass, memmove will handle it
            }
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

namespace generic
{
std::string* get_canonical(std::string* pSql)
{
    return process_markers<maxsimd::generic::make_markers>(pSql, maxsimd::markers());
}

std::string* get_canonical_args(std::string* pSql, CanonicalArgs* pArgs)
{
    auto fn = [&](CanonicalArgument arg){
        pArgs->push_back(std::move(arg));
    };

    return process_markers<maxsimd::generic::make_markers>(pSql, maxsimd::markers(), fn);
}

std::string* get_canonical_old(std::string* pSql)
{
    return generic::get_canonical_old(pSql, maxsimd::markers());
}
}

#if defined (__x86_64__)
std::string* get_canonical(std::string* pSql)
{
    if (cpu_info.has_avx2)
    {
        return process_markers<maxsimd::simd256::make_markers>(pSql, maxsimd::markers());
    }
    else
    {
        return generic::get_canonical(pSql);
    }
}

std::string* get_canonical_args(std::string* pSql, CanonicalArgs* pArgs)
{
    auto fn = [&](CanonicalArgument arg){
        pArgs->push_back(std::move(arg));
    };

    if (cpu_info.has_avx2)
    {
        return process_markers<maxsimd::simd256::make_markers>(pSql, maxsimd::markers(), fn);
    }
    else
    {
        return generic::get_canonical_args(pSql, pArgs);
    }
}
#else

std::string* get_canonical(std::string* pSql)
{
    return generic::get_canonical(pSql);
}

std::string* get_canonical_args(std::string* pSql, CanonicalArgs* pArgs)
{
    return generic::get_canonical_args(pSql, pArgs);
}
#endif

std::string canonical_args_to_sql(std::string_view canonical, const CanonicalArgs& args)
{
    if (args.empty())
    {
        return std::string(canonical);
    }

    // The question marks do not need to be taken into account so we can subtract the size of the arguments
    // from the canonical length to get it.
    size_t total_bytes =
        std::accumulate(args.begin(), args.end(), canonical.size() - args.size(), [](auto& acc, auto& arg){
        return acc + arg.value.size();
    });

    std::string rval;
    rval.reserve(total_bytes);

    auto it_in = canonical.begin();

    for (const auto& arg : args)
    {
        auto it_arg = canonical.begin() + arg.pos;
        // Copy the constant part from the canonical
        rval.insert(rval.end(), it_in, it_arg);
        // Replace the question mark with the argument
        rval.append(arg.value);
        // Skip the question mark
        it_in = it_arg + 1;
    }

    if (it_in != canonical.end())
    {
        // Append the tail end of the canonical
        rval.insert(rval.end(), it_in, canonical.end());
    }

    mxb_assert_message(total_bytes == rval.size(),
                       "Expected %lu bytes but got %lu", total_bytes, rval.size());
    return rval;
}
}
