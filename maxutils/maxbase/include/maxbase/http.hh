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

#include <maxbase/ccdefs.hh>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace maxbase
{

namespace http
{

enum
{
    DEFAULT_CONNECT_TIMEOUT = 10, // @see https://curl.haxx.se/libcurl/c/CURLOPT_CONNECTTIMEOUT.html
    DEFAULT_TIMEOUT = 10          // @see https://curl.haxx.se/libcurl/c/CURLOPT_TIMEOUT.html
};

struct Config
{
    int connect_timeout_s = DEFAULT_CONNECT_TIMEOUT;
    int timeout_s         = DEFAULT_TIMEOUT;
};

struct Result
{
    int                                code = 0; // HTTP response code
    std::string                        body;     // Response body
    std::map<std::string, std::string> headers;  // Headers attached to the response
};

/**
 * Do a HTTP GET, when no user/password is required.
 *
 * @param url     The URL to GET.
 * @param config  The config to use.
 *
 * @return A @c Result.
 */
Result get(const std::string& url, const Config& config = Config());

/**
 * Do a HTTP GET
 *
 * @param url       The URL to GET.
 * @param user      Username to use.
 * @param password  Password for the user.
 * @param config    The config to use.
 *
 * @return A @c Result.
 */
Result get(const std::string& url,
           const std::string& user, const std::string& password,
           const Config& config = Config());

/**
 * Do a HTTP GET, when no user/password is required.
 *
 * @param urls    The URLs to GET.
 * @param config  The config to use.
 *
 * @return A @c Result.
 */
std::vector<Result> get(const std::vector<std::string>& urls,
                        const Config& config = Config());

/**
 * Do a HTTP GET
 *
 * @param urls      The URLs to GET.
 * @param user      Username to use.
 * @param password  Password for the user.
 * @param config    The config to use.
 *
 * @return Vector of @c Results, as many as there were @c urls.
 */
std::vector<Result> get(const std::vector<std::string>& urls,
                        const std::string& user, const std::string& password,
                        const Config& config = Config());
}

}

