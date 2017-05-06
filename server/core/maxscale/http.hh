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

/**
 * @brief Return the current HTTP-date
 *
 * @return The RFC 1123 compliant date
 */
static inline std::string http_get_date()
{
    time_t now = time(NULL);
    struct tm tm;
    char buf[200]; // Enough to store all dates

    gmtime_r(&now, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %y %T GMT", &tm);

    return std::string(buf);
}

/**
 * @brief Convert a time_t value into a HTTP-date string
 *
 * @param t Time to convert
 *
 * @return The time converted to a HTTP-date string
 */
static inline std::string http_to_date(time_t t)
{
    struct tm tm;
    char buf[200]; // Enough to store all dates

    gmtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %y %T GMT", &tm);

    return std::string(buf);
}
