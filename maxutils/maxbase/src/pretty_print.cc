/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

#include <maxbase/pretty_print.hh>
#include <maxbase/assert.hh>
#include <array>
#include <tuple>
#include <cmath>
#include <cstring>
#include <charconv>

namespace maxbase
{
static std::array<std::string_view, 9> byte_prefix {
    "B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "Zib", "YiB"
};

static std::pair<double, std::string_view> pretty_number_split_binary(double dsize)
{
    size_t index {0};
    constexpr int divisor = 1024;

    while (++index < byte_prefix.size() && dsize >= divisor)
    {
        dsize /= divisor;
    }
    --index;

    return {dsize, byte_prefix[index]};
}

std::string pretty_size(size_t sz)
{
    // The maximum length of a string that is generated is 10 characters: 4 for the whole part, 1 for the
    // decimal dot, 2 for the fractional part and 3 for the suffix. This also happens to be small enough that
    // it fits into the SSO space of all std::string implementations.
    char buf[16];

    auto [dsize, suffix] = pretty_number_split_binary(sz);

    // This rounds the numbers incorrectly but it is about an order of magnitude faster than using printf.
    // Since we're pretty-printing the values for human consumption, this is an acceptable amount of
    // inaccuracy.
    int digits = dsize;
    int decimals = (dsize - digits) * 100.0;

    auto rc = std::to_chars(buf, buf + sizeof(buf) - 4, digits);
    mxb_assert(rc.ec == std::errc {});
    char* ptr = rc.ptr;

    if (decimals)
    {
        *ptr++ = '.';

        if (decimals < 10)
        {
            *ptr++ = '0';
        }

        rc = std::to_chars(ptr, ptr + 3, decimals);
        mxb_assert(rc.ec == std::errc {});
        ptr = rc.ptr;

        if (ptr[-1] == '0')
        {
            --ptr;      // Remove the trailing zero
        }
    }

    memcpy(ptr, suffix.data(), suffix.size());
    ptr += suffix.size();

    return std::string(buf, ptr);
}
}
