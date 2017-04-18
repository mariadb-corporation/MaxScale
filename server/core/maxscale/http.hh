#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/cppdefs.hh>

#include <string>

#include <maxscale/debug.h>

using std::string;

/**
 * @brief Return the current HTTP-date
 *
 * @return The RFC 1123 compliant date
 */
static inline string http_get_date()
{
    time_t now = time(NULL);
    struct tm tm;
    char buf[200]; // Enough to store all dates

    gmtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %y %T GMT", &tm);

    return string(buf);
}
