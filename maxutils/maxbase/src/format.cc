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
#include <maxbase/assert.h>
#include <maxbase/log.hh>

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

std::string string_printf(const char* format, ...)
{
    /* Use 'vsnprintf' for the formatted printing. It outputs the optimal buffer length - 1. */
    va_list args;
    va_start(args, format);
    int characters = vsnprintf(NULL, 0, format, args);
    va_end(args);
    std::string rval;
    if (characters < 0)
    {
        // Encoding (programmer) error.
        mxb_assert(!true);
        MXB_ERROR("Could not format the string %s.", format);
    }
    else if (characters > 0)
    {
        // 'characters' does not include the \0-byte.
        int total_size = characters + 1;
        rval.reserve(total_size);
        rval.resize(characters);    // The final "length" of the string
        va_start(args, format);
        // Write directly to the string internal array, avoiding any temporary arrays.
        vsnprintf(&rval[0], total_size, format, args);
        va_end(args);
    }
    return rval;
}

}
