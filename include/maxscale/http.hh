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
#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <time.h>

/**
 * @brief Return the current HTTP-date
 *
 * @return The RFC 1123 compliant date
 */
static inline std::string http_get_date()
{
    time_t now = time(NULL);
    struct tm tm;
    char buf[200];      // Enough to store all dates

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
    char buf[200];      // Enough to store all dates

    gmtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%a, %d %b %Y %T GMT", &tm);

    return std::string(buf);
}

/**
 * @brief Convert a HTTP-date string into time_t
 *
 * @param str HTTP-date formatted string to convert
 *
 * @return The time converted to time_t
 */
static inline time_t http_from_date(const std::string& str)
{
    struct tm tm = {};

    /** First get the GMT time in time_t format */
    strptime(str.c_str(), "%a, %d %b %Y %T GMT", &tm);
    time_t t = mktime(&tm);

    /** Then convert it to local time by calculating the difference between
     * the local time and the GMT time */
    struct tm local_tm = {};
    struct tm gmt_tm = {};
    time_t epoch = 0;

    /** Call tzset() for the sake of portability */
    tzset();
    gmtime_r(&epoch, &gmt_tm);
    localtime_r(&epoch, &local_tm);

    time_t gmt_t = mktime(&gmt_tm);
    time_t local_t = mktime(&local_tm);

    /** The value of `(gmt_t - local_t)` will be the number of seconds west
     * from GMT. For timezones east of GMT, it will be negative. */
    return t - (gmt_t - local_t);
}
