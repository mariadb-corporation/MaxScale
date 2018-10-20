/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/format.hh>

#include <cmath>
#include <cstdio>

namespace
{

const char* get_binary_size_suffix(int i)
{
    switch (i)
    {
    case 0:
        return "B";

    case 1:
        return "KiB";

    case 2:
        return "MiB";

    case 3:
        return "GiB";

    case 4:
        return "TiB";

    case 5:
        return "PiB";

    case 6:
        return "EiB";

    case 7:
        return "ZiB";

    default:
        return "YiB";
    }
}
}

namespace maxbase
{

std::string to_binary_size(int64_t size)
{
    // Calculate log1024(size) and round it up
    int idx = floor(log(size) / log(1024));
    double num = size / pow(1024, idx);
    char buf[200];      // Enough for all possible values
    snprintf(buf, sizeof(buf), "%.2lf%s", num, get_binary_size_suffix(idx));
    return buf;
}
}
