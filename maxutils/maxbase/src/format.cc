/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxbase/format.hh>

#include <cstdio>
#include <maxbase/assert.h>
#include <maxbase/log.h>

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

std::string string_printf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    std::string rval = mxb::string_vprintf(format, args);
    va_end(args);
    return rval;
}

std::string string_vprintf(const char* format, va_list args)
{
    /* Use 'vsnprintf' for the formatted printing. It outputs the optimal buffer length - 1. */
    va_list args_copy;
    va_copy(args_copy, args);
    int characters = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    std::string rval;
    if (characters < 0)
    {
        // Encoding (programmer) error.
        mxb_assert(!true);
        MXB_ERROR("Could not format '%s'.", format);
    }
    else if (characters > 0)
    {
        // 'characters' does not include the \0-byte.
        rval.resize(characters);    // The final "length" of the string
        // Write directly to the string internal array, avoiding any temporary arrays.
        vsnprintf(&rval[0], characters + 1, format, args);
    }
    return rval;
}
}
