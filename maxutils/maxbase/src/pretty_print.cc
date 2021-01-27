/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-01-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/pretty_print.hh>
#include <array>
#include <tuple>
#include <cmath>

namespace maxbase
{
static std::array<const char*, 9> byte_prefix {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB", "Zib", "YiB"};

static std::pair<double, const char*> pretty_number_split_binary(double dsize)
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

static std::array<const char*, 9> si_prefix_greater_1 {"", "k", "M", "G", "T", "P", "E", "Z", "Y"};
static std::array<const char*, 8> si_prefix_less_1 {"m", "u", "n", "p", "f", "a", "z", "y"};

static std::pair<double, const char*> pretty_number_split_decimal(double dsize)
{
    if (dsize == 0)
    {
        return {0, ""};
    }

    constexpr int ten_to_three = 1000;

    size_t index = 0;
    if (dsize >= 1)
    {
        while (index < si_prefix_greater_1.size() && dsize >= ten_to_three)
        {
            ++index;
            dsize /= ten_to_three;
        }
        return {dsize, si_prefix_greater_1[index]};
    }
    else
    {
        dsize *= ten_to_three;
        while (++index < si_prefix_less_1.size() && dsize < 1.0)
        {
            dsize *= ten_to_three;
        }
        --index;
        return {dsize, si_prefix_less_1[index]};
    }
}

std::pair<double, const char*> pretty_number_split(double value, NumberType size_type)
{
    int sign = 1;
    if (std::signbit(value))
    {
        sign = -1;
        value = -value;
    }

    std::pair<double, const char*> res = (size_type == NumberType::Byte) ?
        pretty_number_split_binary(value) : pretty_number_split_decimal(value);

    res.first *= sign;
    return res;
}

static std::string make_it_pretty(double dsize, const char* separator, NumberType size_type)
{
    char buf[64];

    const char* prefix;
    std::tie(dsize, prefix) = pretty_number_split(dsize, size_type);

    // format with two decimals
    auto len = std::sprintf(buf, "%.2f", dsize);

    // remove trailing 0-decimals
    char* ptr = buf + len - 1;
    while (*ptr == '0')
    {
        --ptr;
    }
    if (*ptr != '.')
    {
        ++ptr;
    }
    *ptr = '\0';

    return std::string {buf} + separator + prefix;
}

std::string pretty_size(size_t sz, const char* separator)
{
    return make_it_pretty(sz, separator, NumberType::Byte);
}

std::string pretty_number(double num, const char* separator, const char* suffix)
{
    auto pretty = make_it_pretty(num, separator, NumberType::Regular);
    return pretty + suffix;
}
}
