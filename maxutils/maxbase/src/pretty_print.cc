/*
 * Copyright (c) 2019 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/pretty_print.hh>
#include <array>
#include <tuple>
#include <cmath>
#include <cstring>

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

std::string pretty_size(size_t sz)
{
    char buf[64];

    auto [dsize, suffix] = pretty_number_split_binary(sz);

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

    strcpy(ptr, suffix);

    return buf;
}
}
